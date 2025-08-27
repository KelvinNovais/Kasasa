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
  XdpSession              *session;
  // TODO
  XdpPortal               *portal;
};

G_DEFINE_FINAL_TYPE (KasasaScreencast, kasasa_screencast, ADW_TYPE_BIN)

static void
on_session_start (GObject      *source_object,
                  GAsyncResult *res,
                  gpointer      data)
{
  gint fd;

  g_autoptr (GError) error = NULL;
  KasasaScreencast *self = KASASA_SCREENCAST (data);
  gboolean success = xdp_session_start_finish (self->session,
                                               res,
                                               &error);

  if (error != NULL)
    {
      // TODO
      g_message ("failed 2");
      return;
    }

  if (!success)
    {
      // TODO
      g_message ("failed 3");
      return;
    }

  fd = xdp_session_open_pipewire_remote (self->session);

  GstElement *pipeline, *source, *sink = NULL;
  GdkPaintable *paintable;
  GstStateChangeReturn ret;

  // TODO: should be moved from this file
  gst_init (NULL, NULL);

  // Create the elements
  source = gst_element_factory_make ("fdsrc", "source");
  sink = gst_element_factory_make ("gtk4paintablesink", "sink");

  g_object_set (source,
                "fd", fd,
                NULL);

  GVariant *variant = xdp_session_get_streams (self->session);
  guint u_value;
  g_variant_get (g_variant_get_child_value(variant, 0),
                 "(ua{sv})", &u_value, NULL);

  g_message ("u_value %d", u_value);

  g_object_set (source,
                "path", u_value,
                NULL);

  g_object_get (sink,
                "paintable", &paintable,
                NULL);

  gtk_picture_set_paintable (self->picture, paintable);

  // Create the empty pipeline
  pipeline = gst_pipeline_new ("test-pipeline");

  if (!pipeline || !source || !sink) {
    g_printerr ("Not all elements could be created.\n");
    return;
  }

  // Build the pipeline
  gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
  if (gst_element_link (source, sink) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return;
  }

  // Start playing
  ret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (ret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Unable to set the pipeline to the playing state.\n");
    gst_object_unref (pipeline);
    return;
  }
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
      // TODO
      g_message ("failed 1");
      return;
    }

  xdp_session_start (self->session,
                     NULL,
                     NULL,
                     on_session_start,
                     self);
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
kasasa_screencast_class_init (KasasaScreencastClass *klass)
{
  /* GObjectClass *object_class = G_OBJECT_CLASS (klass); */

  /* object_class->dispose = kasasa_screenshot_dispose; */
}

static void
kasasa_screencast_init (KasasaScreencast *self)
{
  // TODO
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

