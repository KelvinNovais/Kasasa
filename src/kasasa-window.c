/* kasasa-window.c
 *
 * Copyright 2024-2025 Kelvin Novais
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

#include <glib/gi18n.h>

#include "kasasa-window.h"
#include "kasasa-content-container.h"

// Defined on GSchema and preferences
#define MIN_OCCUPY_SCREEN 0.1

struct _KasasaWindow
{
  AdwApplicationWindow       parent_instance;

  /* Template widgets */
  KasasaContentContainer    *content_container;
  AdwHeaderBar              *header_bar;
  GtkRevealer               *header_bar_revealer;
  GtkMenuButton             *menu_button;
  GtkToggleButton           *auto_discard_button;
  GtkToggleButton           *auto_trash_button;
  GtkProgressBar            *progress_bar;
  GtkStack                  *stack;

  /* State variables */
  gboolean                   hide_menu_requested;
  gboolean                   mouse_over_window;
  gboolean                   hiding_window;
  gboolean                   block_miniaturization;
  gboolean                   window_is_miniaturized;

  /* Instance variables */
  GSettings                 *settings;
  AdwAnimation              *window_opacity_animation;
  GCancellable              *miniaturization_canceller;
};

typedef struct
{
  HideWindowCallback    function;
  gpointer              data;
} HideWindowCallbackInfo;

G_DEFINE_FINAL_TYPE (KasasaWindow, kasasa_window, ADW_TYPE_APPLICATION_WINDOW)

void
kasasa_window_take_first_screenshot (KasasaWindow *self)
{
  g_return_if_fail (KASASA_IS_WINDOW (self));
  kasasa_content_container_request_first_screenshot (self->content_container);
}

KasasaWindow *
kasasa_window_get_window_reference (GtkWidget *widget)
{
  GtkRoot *root = NULL;
  KasasaWindow *window = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), NULL);

  root = gtk_widget_get_root (GTK_WIDGET (widget));
  window = KASASA_WINDOW (root);

  g_return_val_if_fail (KASASA_IS_WINDOW (window), NULL);

  return window;
}

gboolean
kasasa_window_get_trash_button_active (KasasaWindow *window)
{
  g_return_val_if_fail (KASASA_IS_WINDOW (window), FALSE);
  return gtk_toggle_button_get_active (window->auto_trash_button);
}

static gboolean
has_different_scalings (gdouble *max_scale)
{
  GdkDisplay *display = NULL;
  GListModel *monitors = NULL;
  GObject *monitor = NULL;
  gdouble min_s, max_s, current_scale;
  guint n_items;

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Can't check for different scalings");
      return FALSE;
    }

  monitors = gdk_display_get_monitors (display);
  n_items = g_list_model_get_n_items (monitors);
  g_info ("Number of monitors: %d", n_items);

  if (n_items == 0)
    {
      g_info ("Detected only 1 monitor, there's no different scales");
      return FALSE;
    }

  monitor = g_list_model_get_object (monitors, 0);
  min_s = max_s = gdk_monitor_get_scale (GDK_MONITOR (monitor));
  g_object_unref (monitor);

  for (guint i = 1; i < n_items; i++)
    {
      monitor = g_list_model_get_object (monitors, i);
      current_scale = gdk_monitor_get_scale (GDK_MONITOR (monitor));

      min_s = MIN (current_scale, min_s);
      max_s = MAX (current_scale, max_s);

      g_object_unref (monitor);
    }

  if (min_s != max_s)
    {
      g_info ("Monitors have different scales: %.2f and %.2f [min, max]",
               min_s, max_s);
      *max_scale = max_s;
      return TRUE;
    }
  else
    {
      g_info ("Monitors have same scales");
      return FALSE;
    }
}

static gboolean
scaling (GtkWidget *widget,
         gdouble   *scale)
{
  GdkDisplay *display = NULL;
  GtkNative *native = NULL;
  GdkSurface *surface = NULL;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), TRUE);

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay");
      return TRUE;
    }

  native = gtk_widget_get_native (widget);
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface");
      return TRUE;
    }

  *scale = gdk_surface_get_scale (surface);

  return FALSE;
}

static gboolean
monitor_size (GtkWidget *widget,
              gdouble   *monitor_width,
              gdouble   *monitor_height)
{
  GdkDisplay *display = NULL;
  GtkNative *native = NULL;
  GdkSurface *surface = NULL;
  GdkMonitor *monitor = NULL;
  GdkRectangle monitor_geometry;
  gdouble hidpi_scale;

  g_return_val_if_fail (GTK_IS_WIDGET (widget), TRUE);

  if (scaling (widget, &hidpi_scale))
    {
      g_warning ("Couldn't get scaling");
      return TRUE;
    }

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay");
      return TRUE;
    }

  native = gtk_widget_get_native (widget);
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface");
      return TRUE;
    }

  monitor = gdk_display_get_monitor_at_surface (display, surface);
  if (monitor == NULL)
    {
      g_warning ("Couldn't get GdkMonitor");
      return TRUE;
    }

  gdk_monitor_get_geometry (monitor, &monitor_geometry);

  *monitor_width = monitor_geometry.width * hidpi_scale;
  *monitor_height = monitor_geometry.height * hidpi_scale;

  return FALSE;
}

// Compute the window size
// Based on:
// https://gitlab.gnome.org/GNOME/Incubator/showtime/-/blob/main/showtime/window.py?ref_type=heads#L836
// https://gitlab.gnome.org/GNOME/loupe/-/blob/4ca5f9e03d18667db5d72325597cebc02887777a/src/widgets/image/rendering.rs#L151
static gboolean
compute_size (KasasaWindow *self,
              gdouble      *nat_width,
              gdouble      *nat_height,
              const gint    content_height,
              const gint    content_width)
{
  g_autoptr (GError) error = NULL;
  // gints
  gint image_width, image_height, image_area, max_width, max_height;
  // gdoubles
  gdouble monitor_width, monitor_height, monitor_area,
          occupy_area_factor, size_scale, target_scale, hidpi_scale, max_scale;

  g_autoptr (GSettings) settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  if (!(content_height > 0 && content_width))
    {
      g_warning ("Content width or height must be > 0");
      return TRUE;
    }

  if (monitor_size (GTK_WIDGET (self), &monitor_width, &monitor_height))
    {
      g_warning ("Couldn't get monitor size");
      return TRUE;
    }

  if (scaling (GTK_WIDGET (self), &hidpi_scale))
    {
      g_warning ("Couldn't get HiDPI scale");
      return TRUE;
    }

  // If the user has different scales for the monitors and the current scale is
  // less than the max scale, divide the image dimentions by the max scale. This
  // is needed because the screenshot size follows the max scale
  if (has_different_scalings (&max_scale))
    {
      image_width = content_width / max_scale;
      image_height = content_height / max_scale;
    }
  else
    {
      image_width = content_width / hidpi_scale;
      image_height = content_height / hidpi_scale;
    }

  // AREAS
  monitor_area = monitor_width * monitor_height;
  image_area = image_height * image_width;

  occupy_area_factor = g_settings_get_double (settings, "occupy-screen");

  // factor for width and height that will achieve the desired area
  // occupation derived from:
  // monitor_area * occupy_area_factor ==
  //   (image_width * size_scale) * (image_height * size_scale)
  size_scale = sqrt (monitor_area / image_area * occupy_area_factor);
  g_debug ("size_scale @ %d: %f", __LINE__, size_scale);
  // ensure that size_scale is not ~ 0 (if image is too big, size_scale can reach 0)
  size_scale = (size_scale < MIN_OCCUPY_SCREEN) ? 0.1 : size_scale;
  g_debug ("size_scale @ %d: %f", __LINE__, size_scale);
  // ensure that we never increase image size
  target_scale = MIN (1, size_scale);
  g_debug ("target_scale @ %d: %f", __LINE__, target_scale);
  *nat_width = image_width * target_scale;
  *nat_height = image_height * target_scale;
  g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
           __LINE__, *nat_width, *nat_height);

  // Scale down if targeted occupation does not fit horizontally
  // Add some margin to not touch corners
  max_width = monitor_width - 20;
  if (*nat_width > max_width)
    {
      *nat_width = max_width;
      *nat_height = image_height * (*nat_width) / image_width;
      g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
               __LINE__, *nat_width, *nat_height);
    }

  // Same for vertical size
  // Additionally substract some space for HeaderBar and Shell bar
  max_height = monitor_height - (50 + 35 + 20) * hidpi_scale;
  if (*nat_height > max_height)
    {
      *nat_height = max_height;
      *nat_width = image_width * (*nat_height) / image_height;
      g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
               __LINE__, *nat_width, *nat_height);
    }

  *nat_width = round (*nat_width);
  *nat_height = round (*nat_height);
  g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
           __LINE__, *nat_width, *nat_height);

  // Ensure that the scaled image isn't smaller than the min window size
  *nat_width = MAX (WINDOW_MIN_WIDTH, *nat_width);
  *nat_height = MAX (WINDOW_MIN_HEIGHT, *nat_height);

  // If the header bar is NOT hiding, then the window height must have more 47 px
  if (!g_settings_get_boolean (settings, "auto-hide-menu"))
    *nat_height += 47;

  g_info ("Physical monitor dimensions: %.2f x %.2f",
          monitor_width, monitor_height);
  g_info ("HiDPI scale: %.2f", hidpi_scale);
  g_info ("Image dimensions: %d x %d", image_width, image_height);
  g_info ("Scaled image dimensions: %.2f x %.2f", *nat_width, *nat_height);

  return FALSE;
}

// Resize the window with an animation
void
kasasa_window_resize_window (KasasaWindow *self,
                             gdouble       new_height,
                             gdouble       new_width)
{
  AdwAnimationTarget *target_h = NULL;
  AdwAnimationTarget *target_w = NULL;
  g_autoptr (AdwAnimation) animation_height = NULL;
  g_autoptr (AdwAnimation) animation_width = NULL;
  gint default_width, default_height;
  static gboolean first_run = TRUE;

  gtk_window_get_default_size (GTK_WINDOW (self),
                               &default_width, &default_height);

  // Disable the carousel navigation while the window is being resized
  kasasa_content_container_carousel_set_interactive (self->content_container,
                                                     FALSE);

  // Set targets
  target_h =
    adw_property_animation_target_new (G_OBJECT (self), "default-height");
  target_w =
    adw_property_animation_target_new (G_OBJECT (self), "default-width");

  // Animation for resizing height
  animation_height = adw_timed_animation_new (
    GTK_WIDGET (self),                                      // widget
    (gdouble) default_height,                               // from
    new_height,                                             // to
    WINDOW_RESIZING_DURATION,                               // duration
    target_h                                                // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation_height),
                                  ADW_EASE_OUT_EXPO);

  // Animation for resizing width
  animation_width = adw_timed_animation_new (
    GTK_WIDGET (self),                                        // widget
    (gdouble) default_width,                                  // from
    new_width,                                                // to
    WINDOW_RESIZING_DURATION,                                 // duration
    target_w                                                  // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (animation_width),
                                  ADW_EASE_OUT_EXPO);

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

  // Enable carousel again
  kasasa_content_container_carousel_set_interactive (self->content_container,
                                                     TRUE);
}

void
kasasa_window_resize_window_scaling (KasasaWindow *self,
                                     gdouble       new_height,
                                     gdouble       new_width)
{
  gdouble nat_width = -1.0;
  gdouble nat_height = -1.0;

  g_return_if_fail (KASASA_IS_WINDOW (self));

  compute_size (self,
                &nat_width, &nat_height,
                new_height, new_width);

  kasasa_window_resize_window (self, nat_height, nat_width);
}

static void
change_opacity_cb (double         value,
                   KasasaWindow  *self)
{
  gtk_widget_set_opacity (GTK_WIDGET (self), value);
}

void
kasasa_window_change_opacity (KasasaWindow *self,
                              Opacity       opacity_direction)
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

  g_clear_object (&self->window_opacity_animation);

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       self,
                                       NULL);

  self->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (self),  // widget
    from, to,             // opacity from to
    270,                  // duration
    target                // target
  );

  adw_animation_play (self->window_opacity_animation);
}

static void
on_window_hidden (AdwAnimation *animation,
                  gpointer      user_data)
{
  HideWindowCallbackInfo *cb = user_data;

  cb->function (cb->data);
  g_free (cb);
}

/*
 * Hide/reveal the window by changing its opacity
 * Hide if 'hide' is TRUE
 * Reveal if 'hide' is FALSE
 *
 * This trick is required because by using gtk_widget_set_visible (window, FALSE),
 * cause the window to be unpinned.
 *
 * Optionally, this functon can receive a 'callback', that is called when hiding
 * window is finished. This argument can be NULL, as well 'callback_data'.
 */
void
kasasa_window_hide_window (KasasaWindow           *self,
                           gboolean                hide,
                           HideWindowCallback      callback,
                           gpointer                callback_data)
{
  AdwAnimationTarget *target = NULL;
  gdouble from, to;

  g_return_if_fail (KASASA_IS_WINDOW (self));

  // Pause an animation
  // The "if" verifies if the animation was called at least once
  if (ADW_IS_ANIMATION (self->window_opacity_animation))
    {
      adw_animation_pause (self->window_opacity_animation);
      g_object_unref (self->window_opacity_animation);
    }

  // Set 'from' and 'to' target values
  from  = gtk_widget_get_opacity (GTK_WIDGET (self));
  to    = (hide) ? 0.00    : 1.00;

  // Set if the window is hiding or being revealed
  self->hiding_window = hide;

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       self,
                                       NULL);

  self->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (self),                      // widget
    from, to,                               // opacity from to
    (hide) ? WINDOW_HIDING_DURATION : 200,  // duration
    target                                  // target
  );

  if (callback != NULL)
    {
      HideWindowCallbackInfo *cb_info = g_new0 (HideWindowCallbackInfo, 1);
      cb_info->function = callback;
      cb_info->data = callback_data;
      g_signal_connect (self->window_opacity_animation, "done",
                        G_CALLBACK (on_window_hidden), cb_info);
    }

  adw_animation_play (self->window_opacity_animation);
}

static gboolean
auto_discard_window_cb (gpointer user_data)
{
  gdouble new_fraction = 0;
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (!gtk_toggle_button_get_active (self->auto_discard_button))
    {
      gtk_progress_bar_set_fraction (self->progress_bar, 0.0);
      return G_SOURCE_REMOVE;
    }

  new_fraction = gtk_progress_bar_get_fraction (self->progress_bar) - 0.005;

  if (new_fraction <= 0)
    {
      gtk_window_close (GTK_WINDOW (self));
      return G_SOURCE_REMOVE;
    }
  else
    {
      gtk_progress_bar_set_fraction (self->progress_bar, new_fraction);
    }

  return G_SOURCE_CONTINUE;
}

void
kasasa_window_auto_discard_window (KasasaWindow *self)
{
  gdouble time_seconds;
  guint time_miliseconds;

  g_return_if_fail (KASASA_IS_WINDOW (self));

  time_seconds = 60 * g_settings_get_double (self->settings,
                                             "auto-discard-window-time");
  // We are goning to decrease the ProgressBar 0.005 (or 0.5%) each function call
  time_miliseconds = (guint) ((time_seconds / 200) * 1000);
  gtk_progress_bar_set_fraction (self->progress_bar, 1.0);
  g_timeout_add (time_miliseconds, auto_discard_window_cb, self);
}

static gboolean
on_modal_close_request (GtkWindow *window,
                        gpointer   user_data)
{
  kasasa_window_miniaturize_window (KASASA_WINDOW (user_data), TRUE);
  return FALSE;
}

/**
 * This function recognizes if there's a Preferences/About dialog (modals);
 * Since the window is not resizeble, a dialog can presented as a transient
 * window, and this function seems to be the only way to get if there's a modal
 * or not
 */
static gboolean
has_modal (KasasaWindow *self)
{
  GListModel *windows = gtk_window_get_toplevels ();
  guint n_items = g_list_model_get_n_items (windows);

  for (guint i = 0; i < n_items; i++)
    {
      gpointer item = g_list_model_get_item (windows, i);

      if (GTK_IS_WINDOW (item)
          && !GTK_IS_SHORTCUTS_WINDOW (item)
          && gtk_window_get_modal (GTK_WINDOW (item))
          )
        {
          g_info ("Window has a modal");
          g_signal_connect (item, "close-request",
                            G_CALLBACK (on_modal_close_request), self);
          g_object_unref (item);
          return TRUE;
        }
      g_object_unref (item);
    }

  return FALSE;
}

static gboolean
window_miniaturization_finish (GObject       *source_object,
                               GAsyncResult  *result,
                               GError       **error)
{
  g_return_val_if_fail (g_task_is_valid (result, source_object), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
window_miniaturization_cb (GObject      *source_object,
                           GAsyncResult *res,
                           gpointer      data)
{
  KasasaWindow *self = KASASA_WINDOW (source_object);
  gboolean miniaturize = window_miniaturization_finish (source_object, res, NULL);

  if (miniaturize
      && !has_modal (self))
    {
      self->window_is_miniaturized = TRUE;
      gtk_stack_set_visible_child_name (self->stack, "miniature_page");
      gtk_widget_add_css_class (GTK_WIDGET (self), "circular-window");
      kasasa_window_resize_window (self, 75, 75);
    }
}

static void
window_miniaturization_thread (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
  sleep (WINDOW_MINIATURIZATION_DELAY);

  // If got here, the task was not cancelled, set TRUE to miniaturize
  // If cancelled, FALSE is silently returned
  g_task_return_boolean (task, TRUE);
}

/*
 * If miniaturize == TRUE, this function will miniaturize the window after some time
 *
 * If miniaturize == FALSE, previous requests will be cancelled, and the window
 * will immediately return to its default visual
 */
void
kasasa_window_miniaturize_window (KasasaWindow *self,
                                  gboolean      miniaturize)
{
  GTask *task = NULL;

  g_return_if_fail (KASASA_IS_WINDOW (self));

  // Cancel a (possible) previous request
  g_cancellable_cancel (self->miniaturization_canceller);

  if (miniaturize)
    {
      if (self->window_is_miniaturized
          || !g_settings_get_boolean (self->settings, "miniaturize-window")
          || self->block_miniaturization
          /* TODO check for block miniaturization button */)
        return;

      g_object_unref (self->miniaturization_canceller);
      self->miniaturization_canceller = g_cancellable_new ();
      task = g_task_new (G_OBJECT (self),
                         self->miniaturization_canceller,
                         window_miniaturization_cb, NULL);
      g_task_set_return_on_cancel (task, TRUE);
      g_task_run_in_thread (task, window_miniaturization_thread);
      g_object_unref (task);
    }
  else
    {
      if (!self->window_is_miniaturized)
        return;

      self->window_is_miniaturized = FALSE;
      kasasa_content_container_request_window_resize (self->content_container);
      gtk_widget_remove_css_class (GTK_WIDGET (self), "circular-window");
      gtk_stack_set_visible_child_name (self->stack, "main_page");
    }
}

void
kasasa_window_block_miniaturization (KasasaWindow *self,
                                     gboolean       block)
{
  g_return_if_fail (KASASA_IS_WINDOW (self));

  if (block)
    {
      // Sets a block
      self->block_miniaturization = TRUE;
      // Restore window
      kasasa_window_miniaturize_window (self, FALSE);
    }
  else
    {
      // Remove the block
      self->block_miniaturization = FALSE;
      // Miniaturize window
      kasasa_window_miniaturize_window (self, TRUE);
    }
}

static void
hide_header_bar_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  self->hide_menu_requested = FALSE;

  /*
   * Hidding was queried because at some moment the mouse pointer left the window,
   * however, don't hide the HeaderBar if it returned and is still over the window
   */
  if (self->mouse_over_window)
    return;

  gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), FALSE);
}

static void
hide_toolbar_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  /*
   * Hidding was queried because at some moment the mouse pointer left the window,
   * however, don't hide the Toolbar if it returned and is still over the window
   */
  if (self->mouse_over_window)
    return;

  kasasa_content_container_reveal_controls (self->content_container, FALSE);
}

static void
reveal_header_bar_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
  gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), TRUE);
}

static void
hide_header_bar (KasasaWindow *self)
{
  // Hide the vertical menu if this option is enabled
  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    // As soon as this action has a delay:
    // if already requested, do nothing; else, request hiding
    if (self->hide_menu_requested == FALSE)
      {
        self->hide_menu_requested = TRUE;
        g_timeout_add_seconds_once (2, hide_header_bar_cb, self);
      }
}

static void
on_mouse_enter_content_container (GtkEventControllerMotion *event_controller_motion,
                                  gdouble                   x,
                                  gdouble                   y,
                                  gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = TRUE;

  kasasa_window_change_opacity (self, OPACITY_DECREASE);

  // Do not reveal HeaderBar/Toolbar if miniaturization is active; this will done
  // after clicking the window
  if (g_settings_get_boolean (self->settings, "miniaturize-window"))
    return;

  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), TRUE);

  kasasa_content_container_reveal_controls (self->content_container, TRUE);
}

static void
on_mouse_leave_content_container (GtkEventControllerMotion *event_controller_motion,
                                  gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;
  if (kasasa_content_container_controls_active (self->content_container)) return;

  self->mouse_over_window = FALSE;
  kasasa_window_change_opacity (self, OPACITY_INCREASE);
  hide_header_bar (self);
  g_timeout_add_seconds_once (2, hide_toolbar_cb, self);
}

static void
on_mouse_enter_header_bar (GtkEventControllerMotion *event_controller_motion,
                           gdouble                   x,
                           gdouble                   y,
                           gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = TRUE;
  kasasa_window_change_opacity (self, OPACITY_INCREASE);
}

// Increase window opacity when the pointer leaves it
static void
on_mouse_leave_header_bar (GtkEventControllerMotion *event_controller_motion,
                           gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

  self->mouse_over_window = FALSE;
  hide_header_bar (self);
}

static void
on_mouse_enter_window (GtkEventControllerMotion *event_controller_motion,
                       gpointer                  user_data)
{
  kasasa_window_miniaturize_window (KASASA_WINDOW (user_data), FALSE);
}

static void
on_mouse_leave_window (GtkEventControllerMotion *event_controller_motion,
                       gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (kasasa_content_container_get_lock (self->content_container))
    return;

  // See Note [1]
  if (kasasa_content_container_controls_active (self->content_container)) return;

  kasasa_window_miniaturize_window (self, TRUE);
}

static void
on_window_click_released (GtkGestureClick *gesture_click,
                          gint             n_press,
                          gdouble          x,
                          gdouble          y,
                          gpointer         user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (!g_settings_get_boolean (self->settings, "miniaturize-window"))
    return;

  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), TRUE);

  kasasa_content_container_reveal_controls (self->content_container, TRUE);
}

static gboolean
on_scroll (GtkEventControllerScroll *self,
           gdouble                   dx,
           gdouble                   dy,
           gpointer                  user_data)
{
  kasasa_window_change_opacity (KASASA_WINDOW (user_data),
                                OPACITY_INCREASE);

  return TRUE;
}

static void
on_auto_discard_button_toggled (GtkToggleButton   *button,
                                gpointer           user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (gtk_toggle_button_get_active (button))
    kasasa_window_auto_discard_window (self);
}

static void
on_settings_updated (GSettings *settings,
                     gchar     *key,
                     gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  if (g_strcmp0 (key, "auto-hide-menu") == 0)
    {
      gboolean auto_hide = g_settings_get_boolean (self->settings, "auto-hide-menu");

      if (auto_hide)
        gtk_widget_add_css_class (GTK_WIDGET (self->header_bar), "headerbar-no-dimming");
      else
        gtk_widget_remove_css_class (GTK_WIDGET (self->header_bar), "headerbar-no-dimming");

      if (auto_hide)
        hide_header_bar (self);
      else
        g_timeout_add_seconds_once (2, reveal_header_bar_cb, self);

      // Resize the window to free/occupy the vertical menu space
      kasasa_content_container_request_window_resize (self->content_container);
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

  else if (g_strcmp0 (key, "miniaturize-window") == 0)
    {
      if (g_settings_get_boolean (self->settings, "miniaturize-window"))
        kasasa_window_miniaturize_window (self, TRUE);
      else
        kasasa_window_miniaturize_window (self, FALSE);
    }
}

static gboolean
on_close_request (GtkWindow *window,
                  gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  kasasa_content_container_wipe_content (self->content_container);

  return FALSE;
}

static void
kasasa_window_dispose (GObject *kasasa_window)
{
  KasasaWindow *self = KASASA_WINDOW (kasasa_window);

  g_clear_object (&self->settings);
  g_clear_object (&self->window_opacity_animation);
  g_clear_object (&self->miniaturization_canceller);

  gtk_widget_dispose_template (GTK_WIDGET (kasasa_window), KASASA_TYPE_WINDOW);

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

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Kasasa/kasasa-window.ui");
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, content_container);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, header_bar_revealer);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, header_bar);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, menu_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, auto_discard_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, auto_trash_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, progress_bar);
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, stack);
}

static void
kasasa_window_init (KasasaWindow *self)
{
  GtkEventController *pc_motion_event_controller = NULL;
  GtkEventController *hb_motion_event_controller = NULL;
  GtkEventController *win_motion_event_controller = NULL;
  GtkEventController *win_scroll_event_controller = NULL;
  GtkGesture *win_gesture_click = NULL;

  g_type_ensure (KASASA_TYPE_CONTENT_CONTAINER);

  gtk_widget_init_template (GTK_WIDGET (self));

  // Initialize self variables
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");
  self->miniaturization_canceller = g_cancellable_new ();
  self->hiding_window = FALSE;

  g_signal_connect (self->settings, "changed", G_CALLBACK (on_settings_updated), self);

  // PERFFORM ACTIONS ON WIDGETS (this should be done before connecting signals
  // to avoid triggering them)
  // Auto discard button
  if (g_settings_get_boolean (self->settings, "auto-discard-window"))
    gtk_toggle_button_set_active (self->auto_discard_button, TRUE);

  // Auto trash button
  if (g_settings_get_boolean (self->settings, "auto-trash-image"))
    gtk_toggle_button_set_active (self->auto_trash_button, TRUE);

  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_widget_add_css_class (GTK_WIDGET (self->header_bar), "headerbar-no-dimming");

  // Hide the HeaderBar if this option is enabled
  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), FALSE);

  // MOTION EVENT CONTROLLERS: Create motion event controllers to monitor when
  // the mouse cursor is over the content container or the menu
  // (I) Picture container
  pc_motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (pc_motion_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_content_container),
                    self);
  g_signal_connect (pc_motion_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_content_container),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->content_container), pc_motion_event_controller);

  // (II) HeaderBar
  hb_motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (hb_motion_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_header_bar),
                    self);
  g_signal_connect (hb_motion_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_header_bar),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->header_bar),
                             hb_motion_event_controller);

  // (III) Window
  win_motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (win_motion_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_window),
                    self);
  g_signal_connect (win_motion_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_window),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self),
                             win_motion_event_controller);

  win_gesture_click = gtk_gesture_click_new ();
  g_signal_connect (win_gesture_click,
                    "released",
                    G_CALLBACK (on_window_click_released),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self),
                             GTK_EVENT_CONTROLLER (win_gesture_click));


  // Increase opacity when the user scrolls the screenshot; this is combined with
  // "page-changed" signal from the caroussel @ContentContainer
  win_scroll_event_controller =
    gtk_event_controller_scroll_new (GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES);
  g_signal_connect (win_scroll_event_controller,
                    "scroll",
                    G_CALLBACK (on_scroll),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), win_scroll_event_controller);

  // SIGNALS
  // Connect buttons to the callbacks
  g_signal_connect (self->auto_discard_button,
                    "toggled",
                    G_CALLBACK (on_auto_discard_button_toggled),
                    self);

  // Listen to events
  g_signal_connect (GTK_WINDOW (self),
                    "close-request",
                    G_CALLBACK (on_close_request),
                    self);
}


/* [1] Note:
 * The menu button seems to behave differently from the other widgets, so it can
 * make the mouse "enter/leave" the window when activated; ignore these situations
 */
