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

#define HIDE_WINDOW_TIME 110
#define WAITING_HIDE_WINDOW_TIME (2 * HIDE_WINDOW_TIME)

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
  AdwHeaderBar        *menu;
  GtkMenuButton       *menu_button;
  GtkToggleButton     *auto_discard_button;
  GtkToggleButton     *auto_trash_button;

  /* State variables */
  gboolean             hide_menu_requested;
  gboolean             mouse_over_window;
  gboolean             hiding_window;

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

// Returns FALSE if not trashed
static gboolean
search_and_trash_image (const gchar *directory_name,
                        const gchar *file_name)
{
  g_autoptr (GFile) directory = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GFileInfo *info = NULL;

  // Get the pictures directory
  directory = g_file_new_for_path (directory_name);

  enumerator = g_file_enumerate_children (directory,                            // directory
                                          G_FILE_ATTRIBUTE_STANDARD_NAME,       // attributes
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,  // flags
                                          NULL,                                 // cancellable
                                          &error);                              // error

  if (error != NULL)
    {
      g_warning ("Error while deleting screenshot - couldn't enumerate directory: %s",
                 error->message);
      return FALSE;
    }

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      const gchar *name = g_file_info_get_name (info);  // no need to free
      GFile *file = g_file_get_child (directory, name);

      if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
        {
          // Recursively search in subdirectories
          g_autofree gchar *path = g_file_get_path (file);
          g_message ("Searching on %s ...", path);

          // If returned TRUE, finalize the loop returning TRUE
          if (search_and_trash_image (path, file_name) == TRUE)
            return TRUE;
        }
      else if (g_strcmp0 (name, file_name) == 0)
        {
          // Trash the image
          g_autofree gchar *parent_path = g_file_get_path (file);
          g_autoptr (GFile) trash_file = g_file_new_for_path (parent_path);

          g_file_trash (trash_file, NULL, &error);
          if (error != NULL)
            g_warning ("Error while deleting screenshot: %s", error->message);

          // Finilize
          g_message ("Trasehd %s", parent_path);
          g_clear_object (&info);
          g_object_unref (file);
          return TRUE;
        }

      g_object_unref (info);
      g_object_unref (file);
    }

  g_clear_object (&info);
  return FALSE;
}

static gboolean
trash_image (GtkWindow *window,
             gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  g_autofree gchar *base_name = NULL;

  // Return if auto trashing screenshot is not enabled
  if (gtk_toggle_button_get_active (self->auto_trash_button) == FALSE)
    return FALSE;

  g_message ("Auto trashing screenshot...");

  // Get the image base name
  if (self->file == NULL
      || (base_name = g_file_get_basename (self->file)) == NULL)
    {
      g_warning ("Error while deleting screenshot: no reference to image");
      return FALSE;
    }

  search_and_trash_image (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES),
                          base_name);

  return FALSE;
}

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

  // If the header bar is NOT hiding, then the window height must have more 46 px
  self->nat_height +=
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
  gdouble from  = gtk_widget_get_opacity (GTK_WIDGET (self));
  gdouble to    = (opacity_direction == OPACITY_INCREASE) ? 1.00    : opacity;

  // Return if this option is disabled
  if (!g_settings_get_boolean (self->settings, "change-opacity"))
    return;

  // Return if the window is hiding/hidden when retaking the screenshot
  // it prevents the opacity increase again if the mouse leave the window
  if (self->hiding_window)
    return;

  // Return if the opacity is already 100%
  if (opacity == OPACITY_INCREASE
      && gtk_widget_get_opacity (GTK_WIDGET (self)) == 1.00)
    return;

  // Pause an animation
  // The "if" verifies if the animation was called at least once
  if (ADW_IS_ANIMATION (self->window_opacity_animation))
    adw_animation_pause (self->window_opacity_animation);

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       self,
                                       NULL);

  self->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (self),    // widget
    from, to,             // opacity from to
    270,                  // duration
    target                // target
  );

  adw_animation_play (self->window_opacity_animation);
}

/*
 * Hide/reveal the window by changing its opacity
 * Hide if "hide" is TRUE
 * Reveal if "hide" is FALSE
 *
 * This trick is required because by using gtk_widget_set_visible (window, FALSE),
 * cause the window to be unpinned.
 */
static void
hide_window (KasasaWindow *self, gboolean hide)
{
  AdwAnimationTarget *target = NULL;

  // Set from and to target values
  gdouble from  = gtk_widget_get_opacity (GTK_WIDGET (self));
  gdouble to    = (hide) ? 0.00    : 1.00;

  // Set if the window is hiding or being revealed
  self->hiding_window = (hide) ? TRUE : FALSE;

  // Pause an animation
  // The "if" verifies if the animation was called at least once
  if (ADW_IS_ANIMATION (self->window_opacity_animation))
    adw_animation_pause (self->window_opacity_animation);

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       self,
                                       NULL);

  self->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (self),                  // widget
    from, to,                           // opacity from to
    (hide) ? HIDE_WINDOW_TIME : 200,    // duration
    target                              // target
  );

  adw_animation_play (self->window_opacity_animation);
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

// Load the screenshot to the GtkPicture widget
static void
load_screenshot (KasasaWindow *self, const gchar *uri)
{
  self->file = g_file_new_for_uri (uri);

  // Explicity unset the previous image: for some reason the old image doesn't get
  // replaced if the new image have the same size
  gtk_picture_set_file (self->picture, NULL);
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
      // translators: reason which the screenshot failed
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
  icon = g_themed_icon_new ("dialog-warning-symbolic");
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

  hide_window (self, FALSE);
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

  // Auto trash previous image, if this option is enabled
  trash_image (NULL, self);

  // Load new image and resize the window
  load_screenshot (self, uri);
  resize_window (self);

  // Set the focus to the retake_screenshot_button
  gtk_window_set_focus (GTK_WINDOW (self), GTK_WIDGET (self->retake_screenshot_button));
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
hide_menu_cb (gpointer user_data)
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
reveal_menu_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->menu_revealer), TRUE);
}

static void
hide_menu (KasasaWindow *self)
{
  // Hide the vertical menu if this option is enabled
  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    // As soon as this action has a delay:
    // if already requested, do nothing; else, request hiding
    if (self->hide_menu_requested == FALSE)
      {
        self->hide_menu_requested = TRUE;
        g_timeout_add_seconds_once (2, hide_menu_cb, self);
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
  hide_menu (self);
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
  hide_menu (self);
}

// Retake screenshot
static void
on_retake_screenshot_button_clicked (GtkButton *button,
                                     gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  hide_window (self, TRUE);

  g_timeout_add_once (WAITING_HIDE_WINDOW_TIME, retake_screenshot, self);

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
      toast = adw_toast_new_format (_("Error: %s"), error->message);
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
  toast = adw_toast_new (_("Copied to the clipboard"));
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
on_auto_discard_button_toggled (GtkToggleButton   *button,
                                gpointer           user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (gtk_toggle_button_get_active (button))
    auto_discard_window (self);
  else
    g_cancellable_cancel (self->auto_discard_canceller);
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
        hide_menu (self);
      else
        g_timeout_add_seconds_once (2, reveal_menu_cb, self);

      // Resize the window to free/occupy the vertical menu space
      resize_window (self);
    }

  else if (g_strcmp0 (key, "auto-discard-window") == 0)
    {
      // Just change the button state; the button callback signal will trigger the auto discarding
      if (g_settings_get_boolean (self->settings, "auto-discard-window"))
        gtk_toggle_button_set_active (self->auto_discard_button, TRUE);
      else
        gtk_toggle_button_set_active (self->auto_discard_button, FALSE);
    }

  else if (g_strcmp0 (key, "auto-trash-image") == 0)
    {
      if (g_settings_get_boolean (self->settings, "auto-trash-image"))
        gtk_toggle_button_set_active (self->auto_trash_button, TRUE);
      else
        gtk_toggle_button_set_active (self->auto_trash_button, FALSE);
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

  g_clear_object (&self->settings);
  g_clear_object (&self->portal);
  if (self->file != NULL)
    g_object_unref (self->file);

  G_OBJECT_CLASS (kasasa_window_parent_class)->dispose (kasasa_window);
}

static void
kasasa_window_finalize (GObject *kasasa_window)
{
    G_OBJECT_CLASS (kasasa_window_parent_class)->finalize (kasasa_window);
}

static void
kasasa_window_class_init (KasasaWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = kasasa_window_dispose;
  object_class->finalize = kasasa_window_finalize;

  gtk_widget_class_install_action (widget_class, "toast.copy_error", "s", copy_error_cb);

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Kasasa/kasasa-window.ui");
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture_container);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, retake_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, copy_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_revealer);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, auto_discard_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, auto_trash_button);
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
  self->hiding_window = FALSE;

  // Connect signal to track when settings are changed; get the necessary values
  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

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
  gtk_widget_add_controller (GTK_WIDGET (self->menu), self->menu_event_controller);

  // BUTTONS
  // Connect buttons to the callbacks
  g_signal_connect (self->retake_screenshot_button, "clicked",
                    G_CALLBACK (on_retake_screenshot_button_clicked), self);
  g_signal_connect (self->copy_button, "clicked",
                    G_CALLBACK (on_copy_button_clicked), self);

  // Auto discard button
  if (g_settings_get_boolean (self->settings, "auto-discard-window"))
    gtk_toggle_button_set_active (self->auto_discard_button, TRUE);


  g_signal_connect (self->auto_discard_button,
                    "toggled",
                    G_CALLBACK (on_auto_discard_button_toggled),
                    self);

  // Auto trash button
  if (g_settings_get_boolean (self->settings, "auto-trash-image"))
    gtk_toggle_button_set_active (self->auto_trash_button, TRUE);

  // Set the focus to the retake_screenshot_button
  gtk_window_set_focus (GTK_WINDOW (self), GTK_WIDGET (self->retake_screenshot_button));

  g_signal_connect (GTK_WINDOW (self), "close-request", G_CALLBACK (trash_image), self);

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
