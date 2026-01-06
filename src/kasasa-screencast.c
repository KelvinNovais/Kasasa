/* kasasa-screencast.c
 *
 * Copyright 2025-2026 Kelvin Novais
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

#include <gst/gst.h>
#include <glib/gi18n.h>

#include "kasasa-screencast.h"

#define CROP_CHEK_INTERVAL 5              // seconds
#define FIRST_CROP_CHECK_INTERVAL 200     // miliseconds

// Default dimensions
#define DEFAULT_WIDTH  360
#define DEFAULT_HEIGHT 200

enum
{
  CROP_TOP,
  CROP_RIGHT,
  CROP_BOTTOM,
  CROP_LEFT,

  CROP_N_ELEMENTS
};

enum
{
  DIMENSION_WIDTH,
  DIMENSION_HEIGHT,

  DIMENSION_N_ELEMENTS
};

// Signals
enum
{
  SIGNAL_NEW_DIMENSION,
  SIGNAL_EOS,

  N_SIGNALS
};

static guint obj_signals[N_SIGNALS];

struct _KasasaScreencast
{
  AdwBin                   parent_instance;
  GtkStack                *stack;
  AdwStatusPage           *no_screencast_page;
  GtkPicture              *picture;

  /* Instance variables */
  GstElement              *pipeline;
  XdpSession              *session;
  gulong                   closed_handler_id;
  guint                    cropping_source;
  gint                     crop[CROP_N_ELEMENTS];
  gint                     dimension[DIMENSION_N_ELEMENTS];
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
  *width = self->dimension[DIMENSION_WIDTH];
  *height = self->dimension[DIMENSION_HEIGHT];
}

static void
set_no_screencast (KasasaScreencast *self)
{
  self->dimension[DIMENSION_WIDTH] = DEFAULT_WIDTH;
  self->dimension[DIMENSION_HEIGHT] = DEFAULT_HEIGHT;

  gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->no_screencast_page));
}

static void
kasasa_screencast_finish (KasasaContent *content)
{
  KasasaScreencast *self = NULL;

  g_return_if_fail (KASASA_IS_SCREENCAST (content));

  self = KASASA_SCREENCAST (content);

  set_no_screencast (self);

  if (self->pipeline)
    gst_element_set_state (self->pipeline, GST_STATE_READY);
}

static void
on_session_closed (XdpSession *session,
                   gpointer    user_data)
{
  g_info ("Session closed");
  g_signal_emit (user_data,
                 obj_signals[SIGNAL_EOS],
                 0);
}

static void
eos_cb (GstBus           *bus,
        GstMessage       *msg,
        KasasaScreencast *self)
{
  g_info ("End-Of-Stream reached");
  g_signal_emit (self,
                 obj_signals[SIGNAL_EOS],
                 0);
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

  adw_status_page_set_title (self->no_screencast_page,
                             _("Screencast ended with error"));
  set_no_screencast (self);

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

  gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
  g_object_set (videocrop,
                "top", self->crop[CROP_TOP],
                "right", self->crop[CROP_RIGHT],
                "bottom", self->crop[CROP_BOTTOM],
                "left", self->crop[CROP_LEFT],
                NULL);
  gst_element_set_state (self->pipeline, GST_STATE_PLAYING);

  gst_object_unref (videocrop);
}

static void
new_dimension (KasasaScreencast *self,
               gint              new_width,
               gint              new_height)
{
  if (self->dimension[DIMENSION_WIDTH] != new_width
      || self->dimension[DIMENSION_HEIGHT] != new_height)
    {
      g_signal_emit (self,
                     obj_signals[SIGNAL_NEW_DIMENSION],
                     0,
                     new_width,
                     new_height);
    }

  // We expect a window following the GTK/ADW minimal dimensions
  self->dimension[DIMENSION_WIDTH] = MAX (new_width, DEFAULT_WIDTH);
  self->dimension[DIMENSION_HEIGHT] = MAX (new_height, DEFAULT_HEIGHT);
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

      // Crop values
      self->crop[CROP_TOP] = top;
      self->crop[CROP_RIGHT] = width - right;
      self->crop[CROP_BOTTOM] = height - bottom;
      self->crop[CROP_LEFT] = left;

      new_dimension (self,
                     (right - left),          // width
                     (bottom - top));         // height

      gst_buffer_unmap (buffer, &map);
    }

  g_debug ("Crop values: top: %d, bottom: %d, left: %d, right: %d",
           self->crop[CROP_TOP], self->crop[CROP_BOTTOM],
           self->crop[CROP_LEFT], self->crop[CROP_RIGHT]);

  g_debug ("Dimensions: width %d, height: %d",
           self->dimension[DIMENSION_WIDTH], self->dimension[DIMENSION_HEIGHT]);

  set_crop (self);

  return G_SOURCE_CONTINUE;
}

static void
compute_first_crop_values (gpointer user_data)
{
  compute_crop_values (user_data);
}

void
kasasa_screencast_show (KasasaScreencast *self,
                        XdpSession       *session,
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

  self->session = session;
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
  gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->picture));

  self->closed_handler_id = g_signal_connect (self->session,
                                              "closed",
                                              G_CALLBACK (on_session_closed),
                                              self);

  g_timeout_add_once (FIRST_CROP_CHECK_INTERVAL, compute_first_crop_values, self);
  self->cropping_source = g_timeout_add_seconds (CROP_CHEK_INTERVAL,
                                                 compute_crop_values,
                                                 self);
}

static void
kasasa_screencast_dispose (GObject *object)
{
  KasasaScreencast *self = KASASA_SCREENCAST (object);

  if (self->pipeline)
    {
      gst_element_set_state (self->pipeline, GST_STATE_NULL);
      gst_object_unref (self->pipeline);
      self->pipeline = NULL;
    }

  if (self->session)
    {
      g_signal_handler_disconnect (self->session, self->closed_handler_id);
      xdp_session_close (self->session);

      g_clear_object (&self->session);
    }

  if (self->cropping_source > 0)
    g_source_remove (self->cropping_source);

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

  // Signals
  obj_signals[SIGNAL_NEW_DIMENSION] =
    g_signal_new ("new-dimension",
                  KASASA_TYPE_SCREENCAST,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,            // no return value
                  2,                      // 2 arguments
                  G_TYPE_INT,             // width
                  G_TYPE_INT);            // height

  obj_signals[SIGNAL_EOS] =
    g_signal_new ("eos",
                  KASASA_TYPE_SCREENCAST,
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE,            // no return value
                  0);                     // no argument

  object_class->dispose = kasasa_screencast_dispose;
}

static void
kasasa_screencast_init (KasasaScreencast *self)
{
  self->pipeline = NULL;

  // Initial dimension to avoid 0 value
  self->dimension[DIMENSION_WIDTH] = DEFAULT_WIDTH;
  self->dimension[DIMENSION_HEIGHT] = DEFAULT_HEIGHT;

  self->stack = GTK_STACK (gtk_stack_new ());

  // Page 1 - No screencast
  self->no_screencast_page = ADW_STATUS_PAGE (adw_status_page_new ());
  adw_status_page_set_icon_name (self->no_screencast_page,
                                 "screencast-recorded-symbolic");
  adw_status_page_set_title (self->no_screencast_page, _("No screencast"));
  gtk_widget_add_css_class (GTK_WIDGET (self->no_screencast_page), "compact");
  gtk_stack_add_child (self->stack, GTK_WIDGET (self->no_screencast_page));

  // Page 2 - Screencast
  self->picture = GTK_PICTURE (gtk_picture_new ());
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_END);
  gtk_stack_add_child (self->stack, GTK_WIDGET (self->picture));

  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->stack));
}

KasasaScreencast *
kasasa_screencast_new (void)
{
  return KASASA_SCREENCAST (g_object_new (KASASA_TYPE_SCREENCAST, NULL));
}

// https://gitlab.freedesktop.org/gstreamer/gst-plugins-rs/-/tree/main/video/gtk4/examples?ref_type=heads
// https://github.com/bilelmoussaoui/ashpd/blob/master/examples/screen_cast_gstreamer.rs

