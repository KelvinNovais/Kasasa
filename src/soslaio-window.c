/* soslaio-window.c
 *
 * Copyright 2024 Kelvin
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

#include <libportal-gtk4/portal-gtk4.h>

#include "config.h"

#include "soslaio-window.h"

struct _SoslaioWindow
{
  AdwApplicationWindow  parent_instance;

  XdpPortal *portal;
  GtkEventController  *motion_event_controller;

  /* Template widgets */
  GtkPicture          *picture;
  GtkBox              *picture_container;
};

G_DEFINE_FINAL_TYPE (SoslaioWindow, soslaio_window, ADW_TYPE_APPLICATION_WINDOW)

static gboolean
load_screenshot (SoslaioWindow *self, const gchar *uri)
{
  g_autoptr (GFile) file = NULL;

  if (uri == NULL)
    return TRUE;

  file = g_file_new_for_uri (uri);

  gtk_picture_set_file (self->picture, file);

  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);

  return FALSE;
}

static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  SoslaioWindow *self = SOSLAIO_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;
  gboolean failed = FALSE;
  GtkIconTheme *icon_theme;
  g_autoptr (GtkIconPaintable) icon;

  uri =  xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      failed = TRUE;
    }
  else
    {
      failed = load_screenshot (self, uri);
    }

  if (failed)
    {
      // Set error icon
      icon_theme = gtk_icon_theme_get_for_display (
        gtk_widget_get_display (GTK_WIDGET (self))
      );
      icon = gtk_icon_theme_lookup_icon (
        icon_theme,                         // icon theme
        "image-missing-symbolic",           // icon name
        NULL,                               // fallbacks
        200,                                // icon size
        1,                                  // scale
        GTK_TEXT_DIR_NONE,                  // text direction
        GTK_ICON_LOOKUP_FORCE_SYMBOLIC      // flags
      );
      gtk_widget_set_margin_top (GTK_WIDGET (self->picture), 20);
      gtk_widget_set_margin_bottom (GTK_WIDGET (self->picture), 20);
      gtk_widget_set_margin_start (GTK_WIDGET (self->picture), 20);
      gtk_widget_set_margin_end (GTK_WIDGET (self->picture), 20);
      gtk_picture_set_paintable (self->picture, GDK_PAINTABLE (icon));
      gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
    }
}

static void
on_mouse_enter (GtkEventControllerMotion *self,
                gdouble                   x,
                gdouble                   y,
                gpointer                  user_data)
{
  SoslaioWindow *window = SOSLAIO_WINDOW (user_data);

  /*
   * Setting an element to visible/invisible seems to be needed to correctly
   * "update" the window
   */
  gtk_widget_set_visible (GTK_WIDGET (window->picture), FALSE);
  gtk_widget_set_opacity (GTK_WIDGET (window), 0.35);
  gtk_widget_set_visible (GTK_WIDGET (window->picture), TRUE);
}

static void
on_mouse_leave (GtkEventControllerMotion *self,
                gpointer                  user_data)
{
  SoslaioWindow *window = SOSLAIO_WINDOW (user_data);
  gtk_widget_set_opacity (GTK_WIDGET (window), 1.00);
}

static void
soslaio_window_dispose (GObject *soslaio_window)
{
    SoslaioWindow *self = SOSLAIO_WINDOW (soslaio_window);

    g_clear_object (&self->portal);

    G_OBJECT_CLASS (soslaio_window_parent_class)->dispose (soslaio_window);
}

static void
soslaio_window_finalize (GObject *soslaio_window)
{
    G_OBJECT_CLASS (soslaio_window_parent_class)->finalize (soslaio_window);
}

static void
soslaio_window_class_init (SoslaioWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = soslaio_window_dispose;
  object_class->finalize = soslaio_window_finalize;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Soslaio/soslaio-window.ui");
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, picture);
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, picture_container);
}

static void
soslaio_window_init (SoslaioWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->portal = xdp_portal_new ();

  self->motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->motion_event_controller, "enter", G_CALLBACK (on_mouse_enter), self);
  g_signal_connect (self->motion_event_controller, "leave", G_CALLBACK (on_mouse_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_container), self->motion_event_controller);

  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    self
  );
}

