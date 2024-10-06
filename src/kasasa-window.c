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
#include <math.h>

#include "kasasa-window.h"

// Screens with this resolution or smaller are handled as small
#define SMALL_SCREEN_AREA (1280 * 1024)
// For small monitors, occupy X% of the screen area
#define SMALL_OCCUPY_SCREEN 0.10
// For large enough monitors, occupy Y% of the screen area when opening a window with a video
#define DEFAULT_OCCUPY_SCREEN 0.15

enum Opacity
{
  OPACITY_INCREASE,
  OPACITY_DECREASE
};

struct _KasasaWindow
{
  AdwApplicationWindow  parent_instance;

  /* Template widgets */
  GtkPicture          *picture;
  GtkBox              *picture_container;
  GtkButton           *retake_screenshot_button;
  GtkButton           *copy_button;
  AdwToastOverlay     *toast_overlay;
  GtkRevealer         *menu_revealer;
  GtkWindowHandle     *vertical_menu;
  GtkMenuButton       *menu_button;

  /* Settings */
  GSettings           *settings;
  gboolean             auto_hide_menu;
  gboolean             change_opacity;
  gdouble              opacity;

  /* State variables */
  gboolean             hide_menu_requested;
  gboolean             mouse_over_window;

  /* Instance variables */
  XdpPortal           *portal;
  GtkEventController  *window_event_controller;
  GtkEventController  *menu_event_controller;
  GFile               *file;
  AdwAnimation        *window_opacity_animation;
  gint                 default_height;
  gint                 default_width;
  gdouble              nat_width;
  gdouble              nat_height;
};

G_DEFINE_FINAL_TYPE (KasasaWindow, kasasa_window, ADW_TYPE_APPLICATION_WINDOW)

static void
resize_width_animated (AdwAnimation* previous_animation,
                       gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autoptr (AdwAnimation) animation = NULL;
  AdwAnimationTarget *target = NULL;

  target =
    adw_property_animation_target_new (G_OBJECT (self), "default-width");

  // Animation for resing height
  animation = adw_timed_animation_new (
    GTK_WIDGET (self),                      // widget
    (gdouble) self->default_width,          // from
    self->nat_width,                        // to
    500,                                    // duration
    target                                  // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation), ADW_EASE_OUT_EXPO);
  adw_animation_play (animation);
}

static void
resize_height_animated (KasasaWindow *self)
{
  g_autoptr (AdwAnimation) animation = NULL;
  AdwAnimationTarget *target = NULL;

  target =
    adw_property_animation_target_new (G_OBJECT (self), "default-height");

  // Animation for resing height
  animation = adw_timed_animation_new (
    GTK_WIDGET (self),                      // widget
    (gdouble) self->default_height,         // from
    self->nat_height,                       // to
    500,                                    // duration
    target                                  // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation), ADW_EASE_OUT_EXPO);
  adw_animation_play (animation);

  g_signal_connect (animation, "done", G_CALLBACK (resize_width_animated), self);
}

static void
resize_window (KasasaWindow *self)
{
  // Based on:
  // https://gitlab.gnome.org/GNOME/Incubator/showtime/-/blob/main/showtime/window.py?ref_type=heads#L836
  // https://gitlab.gnome.org/GNOME/loupe/-/blob/4ca5f9e03d18667db5d72325597cebc02887777a/src/widgets/image/rendering.rs#L151
  g_autoptr (GdkTexture) texture = NULL;
  g_autoptr (GError) error = NULL;
  GtkNative *native = NULL;
  GdkDisplay *display = NULL;
  GdkSurface *surface = NULL;
  GdkMonitor *monitor = NULL;
  GdkRectangle monitor_geometry;
  // gints
  gint monitor_area, hidpi_scale, logical_monitor_area,
  image_height, image_width, image_area, max_width, max_height;
  // gdoubles
  gdouble occupy_area_factor, size_scale, target_scale;

  texture = gdk_texture_new_from_file (self->file, &error);
  if (error != NULL)
    {
      g_warning ("%s", error->message);
      return;
    }

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay, can't find the best window size");
      return;
    }

  native = gtk_widget_get_native (GTK_WIDGET (self));
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative, can't find the best window size");
      return;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface, can't find the best window size");
      return;
    }

  monitor = gdk_display_get_monitor_at_surface (display, surface);
  if (monitor == NULL)
    {
      g_warning ("Couldn't get GdkMonitor, can't find the best window size");
      return;
    }

  gtk_window_get_default_size (GTK_WINDOW (self), &self->default_width, &self->default_height);

  // AREAS
  image_height = gdk_texture_get_height (texture);
  image_width = gdk_texture_get_width (texture);
  image_area = image_height * image_width;

  hidpi_scale = gdk_surface_get_scale_factor (surface);

  gdk_monitor_get_geometry (monitor, &monitor_geometry);
  monitor_area = monitor_geometry.width * monitor_geometry.height;

  logical_monitor_area = monitor_area * hidpi_scale * hidpi_scale;

  // TODO allow the user change this percentage
  occupy_area_factor = (logical_monitor_area <= SMALL_SCREEN_AREA) ?
                       SMALL_OCCUPY_SCREEN : DEFAULT_OCCUPY_SCREEN;

  // factor for width and height that will achieve the desired area
  // occupation derived from:
  // monitor_area * occupy_area_factor ==
  //   (image_width * size_scale) * (image_height * size_scale)
  size_scale = sqrt (monitor_area / image_area * occupy_area_factor);
  // ensure that we never increase image size
  target_scale = MIN (1, size_scale);
  self->nat_width = image_width * target_scale;
  self->nat_height = image_height * target_scale;

  // Scale down if targeted occupation does not fit horizontally
  // Add some margin to not touch corners
  max_width = monitor_geometry.width - 20;
  if (self->nat_width > max_width)
    {
      self->nat_width = max_width;
      self->nat_height = image_height * self->nat_width / image_width;
    }

  // Same for vertical size
  // Additionally substract some space for HeaderBar and Shell bar
  max_height = monitor_geometry.height - (50 + 35 + 20) * hidpi_scale;
  if (self->nat_height > max_height)
    {
      self->nat_height = max_height;
      self->nat_width = image_width * self->nat_height / image_height;
    }

  resize_height_animated (self);
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
  return FALSE;
}

static void
load_settings (KasasaWindow *self)
{
  self->auto_hide_menu = g_settings_get_boolean (self->settings, "auto-hide-menu");
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

  // Set opacity to 100%, if the retake_screenshot_button was pressed, opactiy gets decreased
  gtk_widget_set_opacity (GTK_WIDGET (self), 1.00);
  // Finally, present the window after the screenshot was taken
  gtk_window_present (GTK_WINDOW (self));
  if (!failed) resize_window (KASASA_WINDOW (user_data));
}

static void
change_opacity_cb (double         value,
                   KasasaWindow  *self)
{
  gtk_widget_set_opacity (GTK_WIDGET (self), value);
}

static void
change_opacity_animated (KasasaWindow *self, enum Opacity opacity)
{
  AdwAnimationTarget *target = NULL;

  // Set from and to target values, according to the mode (increase or decrease opacity)
  gdouble from  = (opacity == OPACITY_INCREASE) ? self->opacity : 1.00;
  gdouble to    = (opacity == OPACITY_INCREASE) ? 1.00          : self->opacity;

  // Return if this option is disabled
  if (!self->change_opacity)
    return;

  // Return if the opacity is already 100%
  if (opacity == OPACITY_INCREASE
      && gtk_widget_get_opacity (GTK_WIDGET (self)) == 1.00)
    return;

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       self,
                                       NULL);

  self->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (self),    // widget
    from, to,             // opacity from to
    250,                  // duration
    target                // target
  );

  adw_animation_play (self->window_opacity_animation);
}

static void
hide_vertical_menu_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  self->hide_menu_requested = FALSE;

  // Hidding was queried because at some moment the mouse pointer left the window,
  // however, don't hide the menu if it returned and is still over the window
  if (self->mouse_over_window)
    return;

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), FALSE);
}

static void
hide_vertical_menu (KasasaWindow *self)
{
  // Hide the vertical menu if this option is enabled
  if (self->auto_hide_menu)
    // As soon as this action has a delay:
    // if already requested, do nothing; else, request hiding
    if (self->hide_menu_requested == FALSE)
      {
        self->hide_menu_requested = TRUE;
        g_timeout_add_seconds_once (2, hide_vertical_menu_cb, self);
      }
}

static void
on_mouse_enter_picture_container (GtkEventControllerMotion *event_controller_motion,
                                  gdouble                   x,
                                  gdouble                   y,
                                  gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = TRUE;

  // If the mouse is over the window, do not decrease opacity or do other action
  // if a dialog (preferences/about) is visible
  if (adw_application_window_get_visible_dialog (ADW_APPLICATION_WINDOW (self)) != NULL)
    return;

  change_opacity_animated (self, OPACITY_DECREASE);

  if (self->auto_hide_menu)
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), TRUE);
}

static void
on_mouse_leave_picture_container (GtkEventControllerMotion *event_controller_motion,
                                  gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = FALSE;
  change_opacity_animated (self, OPACITY_INCREASE);
  hide_vertical_menu (self);
}

static void
on_mouse_enter_menu (GtkEventControllerMotion *event_controller_motion,
                     gdouble                   x,
                     gdouble                   y,
                     gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = TRUE;
  change_opacity_animated (self, OPACITY_INCREASE);
}

// Increase window opacity when the pointer leaves it
static void
on_mouse_leave_menu (GtkEventControllerMotion *event_controller_motion,
                     gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = FALSE;
  hide_vertical_menu (self);
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
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_revealer);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, vertical_menu);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_button);
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

  // Connect signal to track when settings are changed
  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

  // Connect buttons to the callbacks
  g_signal_connect (self->retake_screenshot_button, "clicked",
                    G_CALLBACK (on_retake_screenshot_button_clicked), self);
  g_signal_connect (self->copy_button, "clicked",
                    G_CALLBACK (on_copy_button_clicked), self);

  // Create motion event controllers to monitor when the mouse cursor is over the picture container or the menu
  // picture container
  self->window_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->window_event_controller, "enter", G_CALLBACK (on_mouse_enter_picture_container), self);
  g_signal_connect (self->window_event_controller, "leave", G_CALLBACK (on_mouse_leave_picture_container), self);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_container), self->window_event_controller);
  // menu
  self->menu_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->menu_event_controller, "enter", G_CALLBACK (on_mouse_enter_menu), self);
  g_signal_connect (self->menu_event_controller, "leave", G_CALLBACK (on_mouse_leave_menu), self);
  gtk_widget_add_controller (GTK_WIDGET (self->vertical_menu), self->menu_event_controller);

  // Request a screenshot
  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    self
  );

  // Hide the vertical menu if this option is enabled
  if (self->auto_hide_menu)
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), FALSE);
}


/* [1] Note:
 * The menu button seemd to behave differently from the other widgets, so it can
 * make the mouse "enter/leave" the window when activated; ignore these situations
 */
