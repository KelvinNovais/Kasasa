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

#include <glib/gi18n.h>

#include "kasasa-window.h"
#include "kasasa-picture-container.h"

struct _KasasaWindow
{
  AdwApplicationWindow       parent_instance;

  /* Template widgets */
  KasasaPictureContainer    *picture_container;
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

  /* Instance variables */
  GSettings                 *settings;
  AdwAnimation              *window_opacity_animation;
  GCancellable              *auto_discard_canceller;
  GCancellable              *miniaturization_canceller;
  AdwAnimation              *animation_height;
  AdwAnimation              *animation_width;
};

G_DEFINE_FINAL_TYPE (KasasaWindow, kasasa_window, ADW_TYPE_APPLICATION_WINDOW)

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

// Resize the window with an animation
void
kasasa_window_resize_window (KasasaWindow *window,
                             gdouble       new_height,
                             gdouble       new_width)
{
  AdwAnimationTarget *target_h = NULL;
  AdwAnimationTarget *target_w = NULL;
  gint default_width, default_height;
  static gboolean first_run = TRUE;

  gtk_window_get_default_size (GTK_WINDOW (window),
                               &default_width, &default_height);

  // Disable the carousel navigation while the window is being resized
  kasasa_picture_container_carousel_set_interactive (window->picture_container,
                                                     FALSE);

  // Set targets
  target_h =
    adw_property_animation_target_new (G_OBJECT (window), "default-height");
  target_w =
    adw_property_animation_target_new (G_OBJECT (window), "default-width");

  // Animation for resizing height
  window->animation_height = adw_timed_animation_new (
    GTK_WIDGET (window),                                    // widget
    (gdouble) default_height,                               // from
    new_height,                                             // to
    500,                                                    // duration
    target_h                                                // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (window->animation_height),
                                  ADW_EASE_OUT_EXPO);

  // Animation for resizing width
  window->animation_width = adw_timed_animation_new (
    GTK_WIDGET (window),                                      // widget
    (gdouble) default_width,                                  // from
    new_width,                                                // to
    500,                                                      // duration
    target_w                                                  // target
  );
  adw_timed_animation_set_easing (ADW_TIMED_ANIMATION (window->animation_width),
                                  ADW_EASE_OUT_EXPO);

  if (first_run)
    {
      adw_animation_skip (window->animation_width);
      adw_animation_skip (window->animation_height);
      first_run = FALSE;
    }
  else
    {
      adw_animation_play (window->animation_width);
      adw_animation_play (window->animation_height);
    }

  // Enable carousel again
  kasasa_picture_container_carousel_set_interactive (window->picture_container,
                                                     TRUE);
}

static void
change_opacity_cb (double         value,
                   KasasaWindow  *self)
{
  gtk_widget_set_opacity (GTK_WIDGET (self), value);
}

void
kasasa_window_change_opacity (KasasaWindow *window,
                              Opacity       opacity_direction)
{
  AdwAnimationTarget *target = NULL;
  gdouble opacity = g_settings_get_double (window->settings, "opacity");

  // Set from and to target values, according to the mode (increase or decrease opacity)
  gdouble from  = gtk_widget_get_opacity (GTK_WIDGET (window));
  gdouble to    = (opacity_direction == OPACITY_INCREASE) ? 1.00    : opacity;

  // Return if this option is disabled
  if (!g_settings_get_boolean (window->settings, "change-opacity"))
    return;

  // Return if the window is hiding/hidden when retaking the screenshot
  // it prevents the opacity increase again if the mouse leave the window
  if (window->hiding_window)
    return;

  // Return if the opacity is already 100%
  if (opacity == OPACITY_INCREASE
      && gtk_widget_get_opacity (GTK_WIDGET (window)) == 1.00)
    return;

  // Pause an animation
  // The "if" verifies if the animation was called at least once
  if (ADW_IS_ANIMATION (window->window_opacity_animation))
    adw_animation_pause (window->window_opacity_animation);

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       window,
                                       NULL);

  window->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (window),  // widget
    from, to,             // opacity from to
    270,                  // duration
    target                // target
  );

  adw_animation_play (window->window_opacity_animation);
}

/*
 * Hide/reveal the window by changing its opacity
 * Hide if "hide" is TRUE
 * Reveal if "hide" is FALSE
 *
 * This trick is required because by using gtk_widget_set_visible (window, FALSE),
 * cause the window to be unpinned.
 */
void
kasasa_window_hide_window (KasasaWindow *window,
                           gboolean      hide)
{
  AdwAnimationTarget *target = NULL;
  gdouble from, to;

  g_return_if_fail (KASASA_IS_WINDOW (window));

  // Set from and to target values
  from  = gtk_widget_get_opacity (GTK_WIDGET (window));
  to    = (hide) ? 0.00    : 1.00;

  // Set if the window is hiding or being revealed
  window->hiding_window = (hide) ? TRUE : FALSE;

  // Pause an animation
  // The "if" verifies if the animation was called at least once
  if (ADW_IS_ANIMATION (window->window_opacity_animation))
    adw_animation_pause (window->window_opacity_animation);

  target =
    adw_callback_animation_target_new ((AdwAnimationTargetFunc) change_opacity_cb,
                                       window,
                                       NULL);

  window->window_opacity_animation = adw_timed_animation_new (
    GTK_WIDGET (window),                // widget
    from, to,                           // opacity from to
    (hide) ? HIDE_WINDOW_TIME : 200,    // duration
    target                              // target
  );

  adw_animation_play (window->window_opacity_animation);
}

static void
auto_discard_window_thread (GTask         *task,
                            gpointer       source_object,
                            gpointer       task_data,
                            GCancellable  *cancellable)
{
  KasasaWindow *self = KASASA_WINDOW (task_data);
  gdouble time_seconds = 60;
  gboolean use_usleep = FALSE;

  time_seconds *= g_settings_get_double (self->settings, "auto-discard-window-time");

  // Use usleep if the wating time 'time_seconds' is <= 120 seconds...
  use_usleep = (time_seconds <= 120);
  // and multiply by 5 the total time
  time_seconds *= (use_usleep) ? 5 : 1;

  for (gint time = time_seconds; time >= 0; time--)
    {
      if (g_cancellable_is_cancelled (cancellable))
        break;

      gtk_progress_bar_set_fraction (self->progress_bar, (time / time_seconds));
      (use_usleep) ? usleep (200000) : sleep (1);
    }

  g_task_return_pointer (task, NULL, NULL);
}

static void
auto_discard_window_cb (GObject        *source_object,
                        GAsyncResult   *res,
                        gpointer        data)
{
  KasasaWindow *self = KASASA_WINDOW (source_object);
  gboolean cancelled = g_cancellable_is_cancelled (self->auto_discard_canceller);

  if (!cancelled)
    {
      gtk_window_close (GTK_WINDOW (self));
      return;
    }

  g_cancellable_reset (self->auto_discard_canceller);
  gtk_progress_bar_set_fraction (self->progress_bar, 0);
}

void
kasasa_window_auto_discard_window (KasasaWindow *self)
{
  GTask *task = NULL;

  g_return_if_fail (KASASA_IS_WINDOW (self));

  // If auto_discard wasn't queued or was cancelled
  if (self->auto_discard_canceller == NULL)
    self->auto_discard_canceller = g_cancellable_new ();

  task = g_task_new (G_OBJECT (self), self->auto_discard_canceller, auto_discard_window_cb, NULL);
  // Set the task data passed to auto_discard_window_thread ()
  g_task_set_task_data (task, self, NULL);
  g_task_set_return_on_cancel (task, FALSE);
  g_task_run_in_thread (task, auto_discard_window_thread);
  g_object_unref (task);
}

static void
window_miniaturization_thread (GTask        *task,
                               gpointer      source_object,
                               gpointer      task_data,
                               GCancellable *cancellable)
{
  KasasaWindow *self = KASASA_WINDOW (source_object);
  gboolean cancelled;

  sleep (3);

  cancelled = g_cancellable_is_cancelled (cancellable);

  if (!cancelled
      && !self->block_miniaturization)
    {
      gtk_widget_add_css_class (GTK_WIDGET (self), "circular-window");
      kasasa_window_resize_window (self, 75, 75);
      gtk_stack_set_visible_child_name (self->stack, "miniature_page");
    }

  g_task_return_pointer (task, NULL, NULL);
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
  self->miniaturization_canceller = NULL;

  if (miniaturize)
    {
      if (!g_settings_get_boolean (self->settings, "miniaturize-window"))
        return;

      self->miniaturization_canceller = g_cancellable_new ();
      task = g_task_new (G_OBJECT (self), self->miniaturization_canceller, NULL, NULL);
      g_task_set_return_on_cancel (task, FALSE);
      g_task_run_in_thread (task, window_miniaturization_thread);
      g_object_unref (task);
    }
  else
    {
      gtk_widget_remove_css_class (GTK_WIDGET (self), "circular-window");
      kasasa_picture_container_request_window_resize (self->picture_container);
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

  kasasa_picture_container_reveal_toolbar (self->picture_container, FALSE);
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

  kasasa_window_change_opacity (self, OPACITY_DECREASE);

  // Do not reveal HeaderBar/Toolbar if miniaturization is active; this will done
  // after clicking the window
  if (g_settings_get_boolean (self->settings, "miniaturize-window"))
    return;

  if (g_settings_get_boolean (self->settings, "auto-hide-menu"))
    gtk_revealer_set_reveal_child (GTK_REVEALER (self->header_bar_revealer), TRUE);

  kasasa_picture_container_reveal_toolbar (self->picture_container, TRUE);
}

static void
on_mouse_leave_picture_container (GtkEventControllerMotion *event_controller_motion,
                                  gpointer                  user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  // See Note [1]
  if (gtk_menu_button_get_active (self->menu_button)) return;

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
  kasasa_window_miniaturize_window (KASASA_WINDOW (user_data), TRUE);
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

  kasasa_picture_container_reveal_toolbar (self->picture_container, TRUE);
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
  else
    g_cancellable_cancel (self->auto_discard_canceller);
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
      kasasa_picture_container_request_window_resize (self->picture_container);
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
      if (!g_settings_get_boolean (self->settings, "miniaturize-window"))
        kasasa_window_miniaturize_window (self, FALSE);
    }
}

static gboolean
on_close_request (GtkWindow *window,
                  gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  kasasa_picture_container_wipe_screenshots (self->picture_container);

  return FALSE;
}

static void
kasasa_window_dispose (GObject *kasasa_window)
{
  KasasaWindow *self = KASASA_WINDOW (kasasa_window);

  g_clear_object (&self->settings);

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
  gtk_widget_class_bind_template_child (widget_class, KasasaWindow, picture_container);
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

  g_type_ensure (KASASA_TYPE_PICTURE_CONTAINER);

  gtk_widget_init_template (GTK_WIDGET (self));

  // Initialize self variables
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");
  self->auto_discard_canceller = NULL;
  self->miniaturization_canceller = NULL;
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
  // the mouse cursor is over the picture container or the menu
  // (I) Picture container
  pc_motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (pc_motion_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_picture_container),
                    self);
  g_signal_connect (pc_motion_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_picture_container),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->picture_container), pc_motion_event_controller);

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
  // "page-changed" signal from the caroussel @PictureContainer
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

  // Request the first screenshot
  kasasa_picture_container_request_first_screenshot (self->picture_container);
}


/* [1] Note:
 * The menu button seems to behave differently from the other widgets, so it can
 * make the mouse "enter/leave" the window when activated; ignore these situations
 */
