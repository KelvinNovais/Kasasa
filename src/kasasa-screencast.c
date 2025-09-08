/* kasasa-screencast.h
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

// TODO
#include <libportal/portal.h>

#include <gst/gst.h>

#include "kasasa-screencast.h"

struct _KasasaScreencast
{
  AdwBin                   parent_instance;

  /* Instance variables */
  GtkPicture              *picture;
  GstElement              *pipeline;
  XdpSession              *session;

  // TODO just for tests
  XdpPortal               *portal;
};

G_DEFINE_FINAL_TYPE (KasasaScreencast, kasasa_screencast, ADW_TYPE_BIN)

static void
finish (KasasaScreencast *self)
{
  gst_element_set_state (self->pipeline, GST_STATE_READY);
  // TODO close self
}

static void
on_session_closed (XdpSession *self,
                   gpointer    user_data)
{
  g_info ("Session closed.");
  finish (KASASA_SCREENCAST (user_data));
}

static void
eos_cb (GstBus           *bus,
        GstMessage       *msg,
        KasasaScreencast *self)
{
  g_info ("End-Of-Stream reached.");
  finish (self);
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
show_screencast (KasasaScreencast *self,
                 gint              fd,
                 guint             node_id)
{
  g_autofree gchar *node_id_str = NULL;
  GstElement *pipewire_element, *gtksink;
  GstElement *sink = NULL;
  GstCaps *caps = NULL;

  GdkGLContext *gl_context = NULL;
  GdkPaintable *paintable = NULL;

  GstBus *bus = NULL;
  GstStateChangeReturn ret;

  node_id_str = g_strdup_printf ("%d", node_id);

  // Create the elements
  self->pipeline = gst_pipeline_new ("pipeline");
  pipewire_element = gst_element_factory_make ("pipewiresrc", "pipewire_element");
  caps = gst_caps_from_string ("video/x-raw");
  gtksink = gst_element_factory_make ("gtk4paintablesink", "sink");

  if (!self->pipeline || !pipewire_element || !caps || !gtksink)
    {
      g_warning ("Not all elements could be created.");
      return;
    }

  // Set the fd and node ID
  g_object_set (pipewire_element,
                "fd",  fd,
                NULL);

  g_object_set (pipewire_element,
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
  gst_bin_add_many (GST_BIN (self->pipeline), pipewire_element, sink, NULL);
  if (gst_element_link_filtered (pipewire_element, sink, caps) != TRUE) {
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
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_warning ("Unable to set the pipeline to the playing state.");
    return;
  }
}


// TODO use https://libportal.org/method.Session.get_streams.html to get the window size and position
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

  self->session = xdp_portal_create_screencast_session_finish (self->portal,
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
kasasa_screencast_set_window (KasasaScreencast *self)
{
  xdp_portal_create_screencast_session (self->portal,
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

  G_OBJECT_CLASS (kasasa_screencast_parent_class)->dispose (object);
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
  // TODO use a single portal object?
  self->portal = xdp_portal_new ();

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
