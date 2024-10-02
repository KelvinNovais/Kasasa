/* kasasa-window.c
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

#include "config.h"

#include <libportal-gtk4/portal-gtk4.h>
#include <glib/gi18n.h>

#include "kasasa-window.h"

// Size request of the window
#define WIDTH_REQUEST   360
#define HEIGHT_REQUEST  200

struct _KasasaWindow
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
  GtkWindowHandle     *picture_container;
  GtkButton           *retake_screenshot_button;
  GtkButton           *copy_button;
  AdwToastOverlay     *toast_overlay;
};

G_DEFINE_FINAL_TYPE (KasasaWindow, kasasa_window, ADW_TYPE_APPLICATION_WINDOW)

// If (picture.height > picture.width), the window gets its height wrong (much
// taller than should be)
static void
resize_window (KasasaWindow *self)
{
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  gint image_height, image_width;

  texture = gdk_texture_new_from_file (self->file, &error);

  if (error != NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  image_height = gdk_texture_get_height (texture);
  image_width = gdk_texture_get_width (texture);

  if (image_height > image_width)
    gtk_window_set_default_size (GTK_WINDOW (self),
                                 -1,                   // width unset
                                 MAX (HEIGHT_REQUEST, image_height));
}

// Set an "missing image" icon when screenshoting fails
static void
on_fail (KasasaWindow *self, const gchar *error_message)
{
  GtkIconTheme *icon_theme;
  g_autoptr (GtkIconPaintable) icon = NULL;
  AdwDialog *dialog;

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

  // Present a dialog with the message
  dialog = adw_alert_dialog_new (_("Error"), NULL);
  adw_alert_dialog_format_body (ADW_ALERT_DIALOG (dialog), "%s", error_message);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "ok",  _("Ok"),
                                  NULL);
  adw_alert_dialog_set_default_response (ADW_ALERT_DIALOG (dialog), "ok");
  adw_alert_dialog_set_close_response (ADW_ALERT_DIALOG (dialog), "ok");
  adw_dialog_present (dialog, GTK_WIDGET (self));

  // Disable opacity decrease
  self->change_opacity = FALSE;

  // Make the copy button insensitive
  gtk_widget_set_sensitive (GTK_WIDGET (self->copy_button), FALSE);
}

// Load the screenshot to the GtkPicture widget
static gboolean
load_screenshot (KasasaWindow *self, const gchar *uri)
{
  if (uri == NULL)
    return TRUE;

  self->file = g_file_new_for_uri (uri);

  gtk_picture_set_file (self->picture, self->file);

  resize_window (self);

  return FALSE;
}

static void
load_settings (KasasaWindow *self)
{
  self->change_opacity = g_settings_get_boolean (self->settings, "change-opacity");
  self->opacity = g_settings_get_double (self->settings, "opacity");
}

// Callback for xdp_portal_take_screenshot ()
static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;
  g_autofree gchar *error_message = NULL;
  gboolean failed = FALSE;

  uri =  xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  // If failed to get the URI, set the error message
  if (error != NULL)
    {
      error_message = g_strdup_printf (
        "\"%s\"\n\n%s", error->message,
        _("Ensure Screenshot permission is enabled in Settings → Apps → Kasasa")
      );
      g_warning ("%s", error->message);
      failed = TRUE;
    }
  // Try to load the image and handle possible errors
  else
    {
      if ((failed = load_screenshot (self, uri)))
        error_message = _("Couldn't load the screenshot");
    }

  if (failed)
    on_fail (self, error_message);

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
  KasasaWindow *window = KASASA_WINDOW (user_data);

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
  KasasaWindow *window = KASASA_WINDOW (user_data);

  if (window->change_opacity == FALSE)
    return;

  gtk_widget_set_opacity (GTK_WIDGET (window), 1.00);
}

// Retake screenshot
static void
on_retake_screenshot_button_clicked (GtkButton *button,
                                     gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    self
  );
}

// Copy the image to the clipboard
static void
on_copy_button_clicked (GtkButton *button,
                        gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  GdkClipboard *clipboard;
  g_autoptr (GdkTexture) texture = NULL;

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  texture = gdk_texture_new_from_file (self->file, &error);

  if (error != NULL)
    {
      g_autofree gchar *msg = g_strdup_printf ("%s %s", _("Error:"), error->message);

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
  load_settings (KASASA_WINDOW (user_data));
}

static void
kasasa_window_dispose (GObject *kasasa_window)
{
  KasasaWindow *self = KASASA_WINDOW (kasasa_window);

  g_clear_object (&self->portal);
  g_clear_object (&self->settings);
  if (self->file != NULL)
    g_object_unref (self->file);

  G_OBJECT_CLASS (kasasa_window_parent_class)->dispose (kasasa_window);
}

static void
kasasa_window_class_init (KasasaWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = kasasa_window_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Kasasa/kasasa-window.ui");
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture_container);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, retake_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, copy_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, toast_overlay);
}

static void
kasasa_window_init (KasasaWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  // Initialize variables
  self->portal = xdp_portal_new ();
  self->file = NULL;
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  // Read settings
  load_settings (self);

  // Set size request
  gtk_widget_set_size_request (GTK_WIDGET (self), WIDTH_REQUEST, HEIGHT_REQUEST);

  // Connect signal to track when settings are changed
  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

  // Connect buttons to the callbacks
  g_signal_connect (self->retake_screenshot_button, "clicked",
                    G_CALLBACK (on_retake_screenshot_button_clicked), self);
  g_signal_connect (self->copy_button, "clicked",
                    G_CALLBACK (on_copy_button_clicked), self);

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

