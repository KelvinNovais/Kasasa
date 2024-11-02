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

/* No implemented (needs improvments):
 * feat: ### Auto delete screenshot ###
 * TODO: search at most on a subfolder, try without adding a permission, delete with portal
 */

#include "config.h"

#include <libportal-gtk4/portal-gtk4.h>
#include <glib/gi18n.h>
#include <math.h>

#include "kasasa-window.h"

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
  GtkWindowHandle     *picture_container;
  GtkButton           *retake_screenshot_button;
  GtkButton           *copy_button;
  AdwToastOverlay     *toast_overlay;
  GtkRevealer         *menu_revealer;
  GtkWindowHandle     *vertical_menu;
  GtkMenuButton       *menu_button;
  GtkToggleButton     *auto_discard_button;

  /* State variables */
  gboolean             hide_menu_requested;
  gboolean             mouse_over_window;

  /* Instance variables */
  GSettings           *settings;
  XdpPortal           *portal;
  GtkEventController  *window_event_controller;
  GtkEventController  *menu_event_controller;
  GFile               *file;
  AdwAnimation        *window_opacity_animation;
  gint                 default_height;
  gint                 default_width;
  gdouble              nat_width;
  gdouble              nat_height;
  GCancellable        *auto_discard_canceller;
};

G_DEFINE_FINAL_TYPE (KasasaWindow, kasasa_window, ADW_TYPE_APPLICATION_WINDOW)

// Compute the window size
static gboolean
compute_size (KasasaWindow *self)
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
  gint monitor_area, hidpi_scale, image_height, image_width, image_area,
  max_width, max_height;
  // gdoubles
  gdouble occupy_area_factor, size_scale, target_scale;

  texture = gdk_texture_new_from_file (self->file, &error);
  if (error != NULL)
    {
      g_warning ("%s", error->message);
      return TRUE;
    }

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay, can't find the best window size");
      return TRUE;
    }

  native = gtk_widget_get_native (GTK_WIDGET (self));
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative, can't find the best window size");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface, can't find the best window size");
      return TRUE;
    }

  monitor = gdk_display_get_monitor_at_surface (display, surface);
  if (monitor == NULL)
    {
      g_warning ("Couldn't get GdkMonitor, can't find the best window size");
      return TRUE;
    }

  gtk_window_get_default_size (GTK_WINDOW (self), &self->default_width, &self->default_height);

  // AREAS
  image_height = gdk_texture_get_height (texture);
  image_width = gdk_texture_get_width (texture);
  image_area = image_height * image_width;

  hidpi_scale = gdk_surface_get_scale_factor (surface);

  gdk_monitor_get_geometry (monitor, &monitor_geometry);
  monitor_area = monitor_geometry.width * monitor_geometry.height;

  occupy_area_factor = g_settings_get_double (self->settings, "occupy-screen");

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

  // If the vertical menu is NOT hiding, then the window width must have more 46 px
  self->nat_width +=
    (g_settings_get_boolean (self->settings, "auto-hide-menu")) ? 0 : 46;

  return FALSE;
}

// Resize the window with an animation
static void
resize_window (KasasaWindow *self)
{
  g_autoptr (AdwAnimation) animation_height = NULL;
  g_autoptr (AdwAnimation) animation_width = NULL;
  AdwAnimationTarget *target_h = NULL;
  AdwAnimationTarget *target_w = NULL;
  static gboolean first_run = TRUE;

  if (compute_size (self) == TRUE)
    return;

  target_h =
    adw_property_animation_target_new (G_OBJECT (self), "default-height");
  target_w =
    adw_property_animation_target_new (G_OBJECT (self), "default-width");

  // Animation for resizing height
  animation_height = adw_timed_animation_new (
    GTK_WIDGET (self),                      // widget
    (gdouble) self->default_height,         // from
    self->nat_height,                       // to
    500,                                    // duration
    target_h                                // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation_height), ADW_EASE_OUT_EXPO);

  // Animation for resizing width
  animation_width = adw_timed_animation_new (
    GTK_WIDGET (self),                      // widget
    (gdouble) self->default_width,          // from
    self->nat_width,                        // to
    500,                                    // duration
    target_w                                // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation_width), ADW_EASE_OUT_EXPO);

  if (first_run)
    {
      adw_animation_skip (animation_width);
      adw_animation_skip (animation_height);
      first_run = FALSE;
    }
  else
    {
      adw_animation_play (animation_width);
      adw_animation_play (animation_height);
    }
}

static void
close_window (AdwAlertDialog *dialog,
              gpointer        user_data)
{
  gtk_window_close (GTK_WINDOW (user_data));
}

static void
auto_discard_window_thread (GTask         *task,
                            gpointer       source_object,
                            gpointer       task_data,
                            GCancellable  *cancellable)
{
  KasasaWindow *self = KASASA_WINDOW (task_data);
  gdouble time_seconds = 60;

  time_seconds = g_settings_get_double (self->settings, "auto-discard-window-time");
  sleep ((unsigned int) (60 * time_seconds));

  g_task_return_pointer (task, NULL, NULL);
}

static void
auto_discard_window_cb (GObject        *source_object,
                        GAsyncResult   *res,
                        gpointer        data)
{
  KasasaWindow *self = KASASA_WINDOW (source_object);
  gboolean cancelled = g_cancellable_is_cancelled (self->auto_discard_canceller);

  g_cancellable_reset (self->auto_discard_canceller);

  if (!cancelled)
    gtk_window_close (GTK_WINDOW (self));
}

static void
auto_discard_window (KasasaWindow *self)
{
  GTask *task = NULL;

  // If auto_discard wasn't queued or was cancelled
  if (self->auto_discard_canceller == NULL)
    self->auto_discard_canceller = g_cancellable_new ();

  task = g_task_new (G_OBJECT (self), self->auto_discard_canceller, auto_discard_window_cb, NULL);
  // Set the task data passed to auto_discard_window_thread ()
  g_task_set_task_data (task, self, NULL);
  g_task_set_return_on_cancel (task, TRUE);
  g_task_run_in_thread (task, auto_discard_window_thread);
  g_object_unref (task);
}

// Set an "missing image" icon when screenshoting fails
// Currently the dialog covers it, but can used in the future
static void
on_screenshot_fails (KasasaWindow *self, const gchar *error_message)
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
    200,                                // icon size
    1,                                  // scale
    GTK_TEXT_DIR_NONE,                  // text direction
    GTK_ICON_LOOKUP_FORCE_SYMBOLIC      // flags
  );

  // Add margins
  gtk_widget_set_margin_top (GTK_WIDGET (self->picture), 30);
  gtk_widget_set_margin_bottom (GTK_WIDGET (self->picture), 30);
  gtk_widget_set_margin_start (GTK_WIDGET (self->picture), 80);
  gtk_widget_set_margin_end (GTK_WIDGET (self->picture), 80);

  gtk_window_set_default_size (GTK_WINDOW (self), -1, -1);
  // Bug: currently, if the window is not resizable, GTK detaches the dialog from the window
  gtk_window_set_resizable (GTK_WINDOW (self), TRUE);

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

  // Make the buttons insensitive
  gtk_widget_set_sensitive (GTK_WIDGET (self->copy_button), FALSE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button), FALSE);

  g_signal_connect (dialog, "response", G_CALLBACK (close_window), self);
  adw_dialog_present (dialog, GTK_WIDGET (self));
}

// Load the screenshot to the GtkPicture widget
static void
load_screenshot (KasasaWindow *self, const gchar *uri)
{
  self->file = g_file_new_for_uri (uri);
  gtk_picture_set_file (self->picture, self->file);
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
  g_autoptr (GNotification) notification = NULL;
  g_autoptr (GIcon) icon = NULL;

  uri =  xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  // If failed to get the URI, set the error message
  if (error != NULL)
    {
      error_message = g_strconcat (_("Reason: "), error->message, NULL);
      goto ERROR_NOTIFICATION;
    }

  if (uri == NULL)
    {
      error_message = g_strconcat (_("Reason: "), _("Couldn't load the screenshot"), NULL);
      goto ERROR_NOTIFICATION;
    }

  load_screenshot (self, uri);

  if (g_settings_get_boolean (self->settings, "auto-discard-window"))
    auto_discard_window (self);

  resize_window (KASASA_WINDOW (user_data));
  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
  return;

ERROR_NOTIFICATION:
  icon = g_themed_icon_new ("screenshooter-symbolic");
  notification = g_notification_new (_("Screenshot failed"));
  g_notification_set_icon (notification, icon);
  g_notification_set_body (notification, error_message);
  g_application_send_notification (g_application_get_default (),
                                   "io.github.kelvinnovais.Kasasa",
                                   notification);

  g_warning ("%s", error->message);

  gtk_window_close (GTK_WINDOW (self));
  return;
}

static void
on_screenshot_retaken (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;

  gtk_widget_set_visible (GTK_WIDGET (self), TRUE);
  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button), TRUE);

  uri =  xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  if (error != NULL)
    {
      AdwToast *toast =  adw_toast_new_format (_("Error: %s"), error->message);
      adw_toast_set_action_target_value (toast, g_variant_new_string (error->message));
      adw_toast_set_button_label (toast, _("Copy"));
      adw_toast_set_action_name (toast, "toast.copy_error");
      adw_toast_overlay_add_toast (self->toast_overlay, toast);
      g_warning ("%s", error->message);
      return;
    }

  if (uri == NULL)
    {
      g_autofree gchar *error_message = _("Couldn't load the screenshot");
      AdwToast *toast = adw_toast_new (error_message);
      adw_toast_set_action_target_value (toast, g_variant_new_string (error_message));
      adw_toast_set_button_label (toast, _("Copy"));
      adw_toast_set_action_name (toast, "toast.copy_error");
      adw_toast_overlay_add_toast (self->toast_overlay, toast);
      g_warning ("%s", error_message);
      return;
    }

  load_screenshot (self, uri);
  resize_window (self);
}

static void
retake_screenshot (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_retaken,
    self
  );
}

static void
change_opacity_cb (double         value,
                   KasasaWindow  *self)
{
  gtk_widget_set_opacity (GTK_WIDGET (self), value);
}

static void
change_opacity_animated (KasasaWindow *self, enum Opacity opacity_direction)
{
  AdwAnimationTarget *target = NULL;
  gdouble opacity = g_settings_get_double (self->settings, "opacity");

  // Set from and to target values, according to the mode (increase or decrease opacity)
  gdouble from  = (opacity_direction == OPACITY_INCREASE) ? opacity : 1.00;
  gdouble to    = (opacity_direction == OPACITY_INCREASE) ? 1.00    : opacity;

  // Return if this option is disabled
  if (!g_settings_get_boolean (self->settings, "change-opacity"))
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

  /*
   * Hidding was queried because at some moment the mouse pointer left the window,
   * however, don't hide the menu if it returned and is still over the window
   */
  if (self->mouse_over_window)
    return;

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), FALSE);
}

static void
reveal_vertical_menu_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), TRUE);
}

static void
hide_vertical_menu (KasasaWindow *self)
{
  // Hide the vertical menu if this option is enabled
  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
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

  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
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

  gtk_widget_set_visible (GTK_WIDGET (self), FALSE);

  g_timeout_add_once (500, retake_screenshot, self);

  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button), FALSE);
}

// Copy the image to the clipboard
static void
on_copy_button_clicked (GtkButton *button,
                        gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autoptr (GError) error = NULL;
  GdkClipboard *clipboard = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  AdwToast *toast = NULL;

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  texture = gdk_texture_new_from_file (self->file, &error);

  if (error != NULL)
    {
      toast =  adw_toast_new_format (_("Error: %s"), error->message);
      adw_toast_set_action_target_value (toast, g_variant_new_string (error->message));
      adw_toast_set_button_label (toast, _("Copy"));
      adw_toast_set_action_name (toast, "toast.copy_error");
      adw_toast_overlay_add_toast (self->toast_overlay, toast);
      g_warning ("%s", error->message);

      // Make the copy button insensitive on failure
      gtk_widget_set_sensitive (GTK_WIDGET (self->copy_button), FALSE);

      return;
    }

  gdk_clipboard_set_texture (clipboard, texture);
  toast = adw_toast_new (_("Copied to the clibboard"));
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
on_auto_discard_button_toggled (GtkToggleButton   *button,
                                gpointer           user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (gtk_toggle_button_get_active (button))
    {
      gtk_widget_add_css_class (GTK_WIDGET (self->auto_discard_button), "warning");
      auto_discard_window (self);
    }
  else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self->auto_discard_button), "warning");
      g_cancellable_cancel (self->auto_discard_canceller);
    }
}

static void
on_settings_updated (GSettings* settings,
                     gchar* key,
                     gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (g_strcmp0 (key, "auto-hide-menu") == 0)
    {
      if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
        hide_vertical_menu (self);
      else
        g_timeout_add_seconds_once (2, reveal_vertical_menu_cb, self);

      // Resize the window to free/occupy the vertical menu space
      resize_window (self);
    }

  else if (g_strcmp0 (key, "auto-discard-window") == 0)
    {
      if (g_settings_get_boolean (self->settings, "auto-discard-window"))
        auto_discard_window (self);
      else
        g_cancellable_cancel (self->auto_discard_canceller);
    }
}

static void
copy_error_cb (GtkWidget  *sender,
               const char *action,
               GVariant   *param)
{
  GdkClipboard *clipboard = NULL;
  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  // Copy the error message to the clipboard
  gdk_clipboard_set_text (clipboard, g_variant_get_string (param, NULL));
}

static void
kasasa_window_dispose (GObject *kasasa_window)
{
  KasasaWindow *self = KASASA_WINDOW (kasasa_window);

  g_clear_object (&self->portal);
  g_clear_object (&self->settings);
  g_clear_object (&self->auto_discard_canceller);
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

  gtk_widget_class_install_action (widget_class, "toast.copy_error", "s", copy_error_cb);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Kasasa/kasasa-window.ui");
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture_container);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, retake_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, copy_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_revealer);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, vertical_menu);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, auto_discard_button);
}

static void
kasasa_window_init (KasasaWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  // Initialize variables
  self->portal = xdp_portal_new ();
  self->file = NULL;
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");
  self->auto_discard_canceller = NULL;

  // Connect signal to track when settings are changed; get the necessary values
  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

  // Connect buttons to the callbacks
  g_signal_connect (self->retake_screenshot_button, "clicked",
                    G_CALLBACK (on_retake_screenshot_button_clicked), self);
  g_signal_connect (self->copy_button, "clicked",
                    G_CALLBACK (on_copy_button_clicked), self);

  // MOTION EVENT CONTORLLERS: Create motion event controllers to monitor when
  // the mouse cursor is over the picture container or the menu
  // (I) Picture container
  self->window_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->window_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_picture_container),
                    self);
  g_signal_connect (self->window_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_picture_container),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_container), self->window_event_controller);

  // (II) Menu
  self->menu_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (self->menu_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_menu),
                    self);
  g_signal_connect (self->menu_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_menu),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->vertical_menu), self->menu_event_controller);

  // Auto discard button
  if (g_settings_get_boolean (self->settings, "auto-discard-window"))
    gtk_toggle_button_set_active (self->auto_discard_button, TRUE);

  g_signal_connect (self->auto_discard_button,
                    "toggled",
                    G_CALLBACK (on_auto_discard_button_toggled),
                    self);

  // feat: ### Auto delete screenshot ###
  /* g_signal_connect (GTK_WINDOW (self), "close-request", G_CALLBACK (on_close_request), self); */

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
  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), FALSE);
}


/* [1] Note:
 * The menu button seems to behave differently from the other widgets, so it can
 * make the mouse "enter/leave" the window when activated; ignore these situations
 */


// feat: ### Auto delete screenshot ###
/* static gboolean */
/* on_close_request (GtkWindow *window, */
/*                   gpointer   user_data) */
/* { */
/*   g_auto (GStrv) strv; */
/*   g_autofree gchar *path = NULL; */
/*   g_autoptr (GFile) file = NULL; */
/*   guint length = 0; */
/*   g_autoptr (GError) error = NULL; */
/*   KasasaWindow *self = KASASA_WINDOW (user_data); */

/*   if (self->file == NULL */
/*       || (path = g_file_get_path (self->file)) == NULL) */
/*     return FALSE; */

  /* // Split the path to the screenshot */
/*   strv = g_strsplit (path, "/", -1); */
/*   length = g_strv_length (strv); */
/*   // Trash the screenshot corresponding to the name */
/*   path = g_strconcat (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES), // Pictures */
/*                       "/Screenshots/",                                    // folder */
/*                       strv[length-1],                                     // image name */
/*                       NULL); */

/*   file = g_file_new_for_path (path); */

/*   g_file_delete (file, NULL, &error); */
/*   if (error != NULL) */
/*     g_warning ("%s", error->message); */

/*   return FALSE; */
/* } */
