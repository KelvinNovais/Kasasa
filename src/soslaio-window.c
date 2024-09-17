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

  XdpPortal           *portal;
  GtkEventController  *motion_event_controller;
  GFile               *file;
  GSettings           *settings;
  gboolean             change_opacity;
  gdouble              opacity;

  /* Template widgets */
  GtkPicture          *picture;
  GtkBox              *picture_container;
  GtkButton           *copy_button;
  GtkLabel            *error_label;
  AdwToastOverlay     *toast_overlay;
};

G_DEFINE_FINAL_TYPE (SoslaioWindow, soslaio_window, ADW_TYPE_APPLICATION_WINDOW)

/* static void */
/* trash_screenshot (SoslaioWindow *self, const gchar *uri) */
/* { */
/*   g_auto (GStrv) strv; */
/*   g_autofree gchar *new_uri = NULL; */
/*   g_autoptr (GFile) file = NULL; */
/*   guint length = 0; */
/*   g_autoptr (GError) error = NULL; */

/*   // Split the path to the screenshot */
/*   strv = g_strsplit (uri, "/", -1); */
/*   length = g_strv_length (strv); */

/*   // Trash the screenshot corresponding to the name */
/*   new_uri = g_strconcat ("~/Pictures/", strv[length-1], NULL); */
/*   file = g_file_new_for_uri (new_uri); */
/*   g_file_delete (file, NULL, &error); */

/*   if (error != NULL) */
/*     g_warning ("%s", error->message); */
/* } */

// Set an "missing image" icon when screenshoting fails
static void
on_fail (SoslaioWindow *self)
{
  GtkIconTheme *icon_theme;
  g_autoptr (GtkIconPaintable) icon;

  // Set error icon
  icon_theme = gtk_icon_theme_get_for_display (
    gtk_widget_get_display (GTK_WIDGET (self))
  );

  icon = gtk_icon_theme_lookup_icon (
    icon_theme,                         // icon theme
    "image-missing-symbolic",           // icon name
    NULL,                               // fallbacks
    180,                                // icon size
    1,                                  // scale
    GTK_TEXT_DIR_NONE,                  // text direction
    GTK_ICON_LOOKUP_FORCE_SYMBOLIC      // flags
  );

  // Add margins
  gtk_widget_set_margin_top (GTK_WIDGET (self->picture), 25);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->picture), 25);
  gtk_widget_set_margin_start (GTK_WIDGET (self->picture), 25);
  gtk_widget_set_margin_end (GTK_WIDGET (self->picture), 25);

  // Set icon
  gtk_picture_set_paintable (self->picture, GDK_PAINTABLE (icon));

  // Make the error label visible
  gtk_widget_set_visible (GTK_WIDGET (self->error_label), TRUE);

  // Disable opacity decrease
  self->change_opacity = FALSE;

  // Make the copy button insensitive
  gtk_widget_set_sensitive (GTK_WIDGET (self->copy_button), FALSE);
}

// Load the screenshot to the GtkPicture widget
static gboolean
load_screenshot (SoslaioWindow *self, const gchar *uri)
{
  if (uri == NULL)
    return TRUE;

  self->file = g_file_new_for_uri (uri);

  gtk_picture_set_file (self->picture, self->file);

  return FALSE;
}

static void
load_settings (SoslaioWindow *self)
{
  self->change_opacity = g_settings_get_boolean (self->settings, "change-opacity");
  self->opacity = g_settings_get_double (self->settings, "opacity");
}

// Callback xdp_portal_take_screenshot ()
static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  SoslaioWindow *self = SOSLAIO_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;
  gboolean failed = FALSE;

  uri =  xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  if (error != NULL)
    {
      g_autofree gchar *msg = g_strdup_printf ("Error: %s", error->message);
      g_warning ("%s", error->message);
      gtk_label_set_label (self->error_label, msg);
      failed = TRUE;
    }
  else
    {
      if ((failed = load_screenshot (self, uri)))
        gtk_label_set_label (self->error_label, "Error: couldn't load screenshot");
    }

  if (failed)
    on_fail (self);

  // Finally, present the window after the screenshot was taken
  gtk_window_present (GTK_WINDOW (self));
}

// Decrease window opacity when the pointer enters it
static void
on_mouse_enter (GtkEventControllerMotion *self,
                gdouble                   x,
                gdouble                   y,
                gpointer                  user_data)
{
  SoslaioWindow *window = SOSLAIO_WINDOW (user_data);

  if (window->change_opacity == FALSE)
    return;

  /*
   * Setting an element to visible/invisible seems to be needed to correctly
   * "update" the window
   */
  gtk_widget_set_visible (GTK_WIDGET (window->picture), FALSE);
  gtk_widget_set_opacity (GTK_WIDGET (window), window->opacity);
  gtk_widget_set_visible (GTK_WIDGET (window->picture), TRUE);
}

// Increase window opacity when the pointer leaves it
static void
on_mouse_leave (GtkEventControllerMotion *self,
                gpointer                  user_data)
{
  SoslaioWindow *window = SOSLAIO_WINDOW (user_data);

  if (window->change_opacity == FALSE)
    return;

  gtk_widget_set_opacity (GTK_WIDGET (window), 1.00);
}

// Copy the image to the clipboard
static void
on_copy_button_clicked (GtkButton *button,
                        gpointer   user_data)
{
  SoslaioWindow *self = SOSLAIO_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  GdkClipboard *clipboard;
  g_autoptr (GdkTexture) texture = NULL;

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  texture = gdk_texture_new_from_file (self->file, &error);

  if (error != NULL)
    {
      g_autofree gchar *msg = g_strdup_printf ("Error: %s", error->message);

      // Make the copy button insensitive on failure
      gtk_widget_set_sensitive (GTK_WIDGET (self->copy_button), FALSE);
      g_warning ("%s", error->message);

      // Show error on UI
      adw_toast_overlay_add_toast (self->toast_overlay, adw_toast_new (msg));

      return;
    }

  gdk_clipboard_set_texture (clipboard, texture);
}

static void
on_settings_updated (GSettings* self,
                     gchar* key,
                     gpointer user_data)
{
  load_settings (SOSLAIO_WINDOW (user_data));
}

static void
soslaio_window_dispose (GObject *soslaio_window)
{
  SoslaioWindow *self = SOSLAIO_WINDOW (soslaio_window);

  g_clear_object (&self->portal);
  g_clear_object (&self->settings);
  if (self->file != NULL)
    g_object_unref (self->file);

  G_OBJECT_CLASS (soslaio_window_parent_class)->dispose (soslaio_window);
}

static void
soslaio_window_class_init (SoslaioWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = soslaio_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Soslaio/soslaio-window.ui");
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, picture);
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, picture_container);
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, copy_button);
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, error_label);
  gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, toast_overlay);
}

static void
soslaio_window_init (SoslaioWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  // Initialize variables
  self->portal = xdp_portal_new ();
  self->file = NULL;
  self->settings = g_settings_new ("io.github.kelvinnovais.Soslaio");

  // Read settings
  load_settings (self);

  // Connect signal to track when settings are changed
  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

  // Connect copy button callback
  g_signal_connect (self->copy_button, "clicked", G_CALLBACK (on_copy_button_clicked), self);

  // Create a motion event controller to monitor when the mouse cursor is over the window
  self->motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->motion_event_controller, "enter", G_CALLBACK (on_mouse_enter), self);
  g_signal_connect (self->motion_event_controller, "leave", G_CALLBACK (on_mouse_leave), self);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_container), self->motion_event_controller);

  // Request a screenshot
  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    self
  );
}
