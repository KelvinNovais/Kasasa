/* kasasa-screencast.c
 *
 * Copyright 2025 Kelvin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

// FIXME apply crop even with no frame update

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include "kasasa-screencast.h"
#include "kasasa-picture-container.h"

#define CROP_CHEK_INTERVAL 5              // seconds
#define FIRST_CROP_CHECK_INTERVAL 200     // miliseconds

// Default dimensions
#define DEFAULT_WIDTH  360
#define DEFAULT_HEIGHT 200

enum
{
  C_TOP,
  C_RIGHT,
  C_BOTTOM,
  C_LEFT,

  C_N_ELEMENTS
};

enum
{
  D_WIDTH,
  D_HEIGHT,

  D_N_ELEMENTS
};

struct _KasasaScreencast
{
  AdwBin                   parent_instance;

  /* Instance variables */
  GtkPicture              *picture;
  GstElement              *pipeline;
  XdpSession              *session;
  guint                    crop_check_source;
  guint                    crop[C_N_ELEMENTS];
  guint                    dimension[D_N_ELEMENTS];
};

static void kasasa_screencast_content_interface_init (KasasaContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (KasasaScreencast, kasasa_screencast, ADW_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE (KASASA_TYPE_CONTENT,
                                                kasasa_screencast_content_interface_init))

static void
kasasa_screencast_get_dimensions (KasasaContent *content,
                                  gint          *height,
                                  gint          *width)
{
  KasasaScreencast *self = NULL;

  g_return_if_fail (KASASA_IS_SCREENCAST (content));

  self = KASASA_SCREENCAST (content);
  *width = self->dimension[D_WIDTH];
  *height = self->dimension[D_HEIGHT];
}

static void
kasasa_screencast_finish (KasasaContent *content)
{
  KasasaScreencast *self = NULL;

  g_return_if_fail (KASASA_IS_SCREENCAST (content));

  self = KASASA_SCREENCAST (content);

  gst_element_set_state (self->pipeline, GST_STATE_READY);
  xdp_session_close (self->session);
  // TODO close self
}

static void
on_session_closed (XdpSession *self,
                   gpointer    user_data)
{
  g_info ("Session closed.");
  kasasa_screencast_finish (KASASA_CONTENT (user_data));
}

static void
eos_cb (GstBus           *bus,
        GstMessage       *msg,
        KasasaScreencast *self)
{
  g_info ("End-Of-Stream reached.");
  kasasa_screencast_finish (KASASA_CONTENT (self));
}

static void
error_cb (GstBus           *bus,
          GstMessage       *msg,
          KasasaScreencast *self)
{
  g_autoptr (GError) error = NULL;
  g_autofree gchar *debug_info = NULL;

  gst_message_parse_error (msg, &error, &debug_info);
  g_warning ("Error received from element %s: %s",
             GST_OBJECT_NAME (msg->src), error->message);
  g_warning ("Debugging information: %s", debug_info ? debug_info : "none");

  gst_element_set_state (self->pipeline, GST_STATE_READY);
}

static void
set_crop (KasasaScreencast *self)
{
  GstElement *videocrop = gst_bin_get_by_name (GST_BIN (self->pipeline),
                                               "videocrop");

  if (!videocrop)
    {
      g_warning ("Failed to set video crop");
      return;
    }

  g_object_set (videocrop,
                "top", self->crop[C_TOP],
                "right", self->crop[C_RIGHT],
                "bottom", self->crop[C_BOTTOM],
                "left", self->crop[C_LEFT],
                NULL);

  gst_object_unref (videocrop);
}

static gboolean
compute_crop_values (gpointer user_data)
{
  KasasaScreencast *self = NULL;
  g_autoptr (GstSample) sample = NULL;
  g_autoptr (GstElement) fakesink = NULL;
  GstBuffer *buffer = NULL;
  const GstCaps *caps = NULL;
  GstMapInfo map;

  gint width = 0;
  gint height = 0;

  self = KASASA_SCREENCAST (user_data);

  // Get fakesink
  fakesink = gst_bin_get_by_name (GST_BIN (self->pipeline), "fakesink");
  if (fakesink == NULL)
    {
      g_warning ("Got fakesink == NULL while processing crop dimensions. "\
                 "Unable to crop to window size.");
      return G_SOURCE_REMOVE;
    }

  // Get sample
  g_object_get (fakesink,
                "last-sample", &sample,
                NULL);
  if (sample == NULL)
    {
      g_debug ("sample == NULL while processing crop dimensions");
      return G_SOURCE_CONTINUE;
    }

  // Get sample info
  caps = gst_sample_get_caps (sample);
  if (caps)
    {
      const GstStructure *structure;
      const gchar *format;

      structure = gst_caps_get_structure (caps, 0);
      format = gst_structure_get_string (structure, "format");

      // Check if the format is BGRx
      if (g_strcmp0 (format, "BGRx") == 0)
        {
          gst_structure_get_int (structure, "width", &width);
          gst_structure_get_int (structure, "height", &height);
        }
      else
        {
          g_warning ("Expected format BGRx, but received: %s. "\
                     "Unable to crop to window size.", format);
          gst_buffer_unmap (buffer, &map);
          return G_SOURCE_REMOVE;
        }
    }

  // Ensure the width and height of the sample is ok
  if (width < 100 || height < 100)
    {
      g_warning ("Sample is too small, crop skipped");
      return G_SOURCE_REMOVE;
    }

  // Get crop dimensions
  buffer = gst_sample_get_buffer (sample);
  if (gst_buffer_map (buffer, &map, GST_MAP_READ))
    {
      gint top = height, bottom = 0, left = width, right = 0;

      // Analyze the pixel data
      // (I) Columns
      for (gint y = 0; y < height; y++)
        {
          for (gint x = 0; x < width; x = (x+1)*4)
            {
              // 4 bytes per pixel (B, G, R, X)
              gint index = (y * width + x) * 4;

              guchar b = map.data[index];
              guchar g = map.data[index + 1];
              guchar r = map.data[index + 2];

              // Check if the pixel is not black (ignoring the alpha channel)
              if (b != 0 || g != 0 || r != 0)
                {
                  if (y < top) top = y;
                  if (y > bottom) bottom = y;
                }
            }
        }

      for (gint y = 0; y < height; y = (y+1)*4)
        {
          for (gint x = 0; x < width; x++)
            {
              gint index = (y * width + x) * 4;

              guchar b = map.data[index];
              guchar g = map.data[index + 1];
              guchar r = map.data[index + 2];

              if (b != 0 || g != 0 || r != 0)
                {
                  if (x < left) left = x;
                  if (x > right) right = x;
                }
            }
        }

      self->crop[C_TOP] = top;
      self->crop[C_RIGHT] = width - right;
      self->crop[C_BOTTOM] = height - bottom;
      self->crop[C_LEFT] = left;

      self->dimension[D_WIDTH] = right - left;
      self->dimension[D_HEIGHT] = bottom - top;

      gst_buffer_unmap (buffer, &map);
    }

  // Crop values
  g_debug ("Crop values: top: %d, bottom: %d, left: %d, right: %d",
           self->crop[C_TOP], self->crop[C_BOTTOM],
           self->crop[C_LEFT], self->crop[C_RIGHT]);

  g_debug ("Dimensions: width %d, height: %d",
           self->dimension[D_WIDTH], self->dimension[D_HEIGHT]);

  set_crop (self);

  return G_SOURCE_CONTINUE;
}

static void
compute_first_crop_values (gpointer user_data)
{
  compute_crop_values (user_data);
}

static void
show_screencast (KasasaScreencast *self,
                 gint              fd,
                 guint             node_id)
{
  g_autofree gchar *node_id_str = NULL;
  GstElement *pipewire_element = NULL;
  GstElement *filter = NULL, *videocrop = NULL, *gtksink = NULL, *sink = NULL;
  g_autoptr (GstCaps) caps = NULL;

  GstElement *tee, *queue1, *queue2, *fakesink;

  GdkGLContext *gl_context = NULL;
  GdkPaintable *paintable = NULL;

  GstBus *bus = NULL;
  GstStateChangeReturn ret;

  node_id_str = g_strdup_printf ("%d", node_id);

  // Create the elements
  self->pipeline = gst_pipeline_new ("pipeline");
  pipewire_element = gst_element_factory_make ("pipewiresrc", "pipewire_element");
  gtksink = gst_element_factory_make ("gtk4paintablesink", "sink");

  videocrop = gst_element_factory_make ("videocrop", "videocrop");

  caps = gst_caps_from_string ("video/x-raw");
  filter = gst_element_factory_make ("capsfilter", "filter");
  g_object_set (filter,
                "caps", caps,
                NULL);

  tee = gst_element_factory_make ("tee", "tee");
  queue1 = gst_element_factory_make ("queue", "queue1");
  queue2 = gst_element_factory_make ("queue", "queue2");

  // Create a fakesink to retrieve original frames
  fakesink = gst_element_factory_make ("fakesink", "fakesink");

  if (!self->pipeline || !pipewire_element || !tee
      || !queue1 || !filter || !videocrop || !gtksink
      || !queue2 || !fakesink)
    {
      g_warning ("Not all elements could be created.");
      return;
    }

  // Set the fd and node ID
  g_object_set (pipewire_element,
                "fd",  fd,
                "path", node_id_str,
                NULL);

  g_debug ("fd: %d; node_id: %s", fd, node_id_str);

  // Get the GLContex and GdkPaintable
  g_object_get (gtksink,
                "paintable", &paintable,
                NULL);

  g_object_get (paintable,
                "gl-context", &gl_context,
                NULL);

  // Check for GLContext
  if (gl_context)
    {
      g_info ("Using GL");
      sink = gst_element_factory_make ("glsinkbin", "glsinkbin");
      g_object_set (sink,
                    "sink", gtksink,
                    NULL);
    }
  else
    {
      GstElement *convert = NULL;

      g_info ("Not using GL");
      convert = gst_element_factory_make ("videoconvert", "convert");
      sink = gst_bin_new ("gst_bin");
      gst_bin_add_many (GST_BIN (sink), convert, gtksink, NULL);
      gst_element_link (convert, gtksink);
    }

  // Build the pipeline
  gst_bin_add_many (GST_BIN (self->pipeline),
                    pipewire_element, tee, queue1, filter, videocrop, sink,
                    queue2, fakesink, NULL);
  if (!gst_element_link_many (pipewire_element,
                              tee, queue1, filter, videocrop, sink, NULL)
       || !gst_element_link_many (tee, queue2, fakesink, NULL)
      )
    {
      g_warning ("Elements could not be linked.");
      return;
    }

  // Set the paintable
  gtk_picture_set_paintable (self->picture, paintable);

  // Configure the bus
  bus = gst_element_get_bus (self->pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, self);
  gst_object_unref (bus);

  // Start playing
  ret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE)
    {
      g_warning ("Unable to set the pipeline to the playing state.");
      return;
    }

  // TODO remove first check?
  g_timeout_add_once (FIRST_CROP_CHECK_INTERVAL, compute_first_crop_values, self);
  self->crop_check_source = g_timeout_add_seconds (CROP_CHEK_INTERVAL,
                                                   compute_crop_values,
                                                   self);
}

static void
on_session_start (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      data)
{
  gint fd;
  guint node_id;

  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) streams = NULL;
  g_autoptr (GVariant) stream = NULL;

  KasasaScreencast *self = KASASA_SCREENCAST (data);
  gboolean success = xdp_session_start_finish (self->session,
                                               res,
                                               &error);

  if (error != NULL || !success)
    {
      g_warning ("Couldn't start the session successfully: %s", error->message);
      return;
    }

  fd = xdp_session_open_pipewire_remote (self->session);

  streams = xdp_session_get_streams (self->session);
  stream = g_variant_get_child_value (streams, 0);
  g_variant_get (stream,
                 "(ua{sv})", &node_id, NULL);

  g_debug ("Streams: %s", g_variant_print (streams, TRUE));

  show_screencast (self, fd, node_id);
}

static void
on_create_screencast_session (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      data)
{
  g_autoptr (GError) error = NULL;
  KasasaScreencast *self = KASASA_SCREENCAST (data);

  self->session =
    xdp_portal_create_screencast_session_finish (XDP_PORTAL (source_object),
                                                 res,
                                                 &error);

  if (error != NULL)
    {
      g_warning ("Failed to create screencast session: %s", error->message);
      return;
    }

  xdp_session_start (self->session,
                     NULL,
                     NULL,
                     on_session_start,
                     self);

  g_signal_connect (self->session, "closed", G_CALLBACK (on_session_closed), self);
}

void
kasasa_screencast_set_window (KasasaScreencast *self,
                              XdpPortal        *portal)
{
  xdp_portal_create_screencast_session (portal,
                                        XDP_OUTPUT_WINDOW,
                                        XDP_SCREENCAST_FLAG_NONE,
                                        XDP_CURSOR_MODE_HIDDEN,
                                        XDP_PERSIST_MODE_TRANSIENT,
                                        NULL,
                                        NULL,
                                        on_create_screencast_session,
                                        self);
}

static void
kasasa_screencast_dispose (GObject *object)
{
  KasasaScreencast *self = KASASA_SCREENCAST (object);

  if (self->pipeline != NULL)
    {
      gst_element_set_state (self->pipeline, GST_STATE_NULL);
      gst_object_unref (self->pipeline);
    }

  if (self->session != NULL)
    g_clear_object (&self->session);

  g_source_remove (self->crop_check_source);

  G_OBJECT_CLASS (kasasa_screencast_parent_class)->dispose (object);
}

static void
kasasa_screencast_content_interface_init (KasasaContentInterface *iface)
{
  iface->get_dimensions = kasasa_screencast_get_dimensions;
  iface->finish = kasasa_screencast_finish;
}

static void
kasasa_screencast_class_init (KasasaScreencastClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  gst_init (NULL, NULL);

  object_class->dispose = kasasa_screencast_dispose;
}

static void
kasasa_screencast_init (KasasaScreencast *self)
{
  self->pipeline = NULL;

  // Initial dimension to avoid 0 value
  self->dimension[D_WIDTH] = DEFAULT_WIDTH;
  self->dimension[D_HEIGHT] = DEFAULT_HEIGHT;

  self->picture = GTK_PICTURE (gtk_picture_new ());
  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->picture));
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_END);
}

KasasaScreencast *
kasasa_screencast_new (void)
{
  return KASASA_SCREENCAST (g_object_new (KASASA_TYPE_SCREENCAST, NULL));
}

// https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/tree/main/video/gtk4/examples?ref_type=heads
// https://github.com/bilelmoussaoui/ashpd/blob/master/examples/screen_cast_gstreamer.rs
