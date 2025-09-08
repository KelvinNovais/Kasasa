/* kasasa-picture-container.c
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

#include <glib/gi18n.h>
#include <math.h>

#include "kasasa-picture-container.h"
#include "kasasa-picture-container-private.h"

#include "kasasa-window.h"
#include "kasasa-screenshot.h"
#include "kasasa-screencast.h"
#include "routines.h"

G_DEFINE_FINAL_TYPE (KasasaPictureContainer, kasasa_picture_container, ADW_TYPE_BREAKPOINT_BIN)

static KasasaScreenshot * get_current_screenshot (KasasaPictureContainer *self);

gboolean
kasasa_picture_container_get_lock (KasasaPictureContainer *self)
{
  g_return_val_if_fail (KASASA_IS_PICTURE_CONTAINER (self), FALSE);

  return gtk_toggle_button_get_active (self->lock_button);
}

void
kasasa_picture_container_reveal_controls (KasasaPictureContainer *self,
                                          gboolean                reveal_child)
{
  gboolean reveal_lock_button;
  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));

  reveal_lock_button =
    g_settings_get_boolean (self->settings,
                            "miniaturize-window") ? reveal_child : FALSE;

  gtk_revealer_set_reveal_child (self->revealer_start_buttons, reveal_child);
  gtk_revealer_set_reveal_child (self->revealer_end_buttons, reveal_child);
  gtk_revealer_set_reveal_child (self->revealer_lock_button, reveal_lock_button);
}

void
kasasa_picture_container_request_first_screenshot (KasasaPictureContainer *self)
{
  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));
  routines_take_first_screenshot (self);
}

void
kasasa_picture_container_request_window_resize (KasasaPictureContainer *self)
{
  KasasaWindow *window = NULL;
  gint new_height, new_width;

  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));

  window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_secreenshot_get_dimensions (get_current_screenshot (self),
                                     &new_height,
                                     &new_width);

  kasasa_window_resize_window (window, new_height, new_width);
}

void
kasasa_picture_container_wipe_screenshots (KasasaPictureContainer *self)
{
  guint n_pages = 0;

  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));

  n_pages = adw_carousel_get_n_pages (self->carousel);

  adw_carousel_set_interactive (self->carousel, FALSE);

  // Request deleting screenshots from the last to the first page of the carousel;
  // the image is only deleted by KasasaScreenshot if the trash_button is toggled
  for (gint i = n_pages-1; i >= 0; i--)
    {
      KasasaScreenshot *screenshot =
        KASASA_SCREENSHOT (adw_carousel_get_nth_page (self->carousel, i));

      // First trash the image (it needs a reference to the parent window)...
      kasasa_screenshot_trash_image (screenshot);
      // ...then remove it from the carousel
      adw_carousel_remove (self->carousel, GTK_WIDGET (screenshot));
    }
}

void
kasasa_picture_container_carousel_set_interactive (KasasaPictureContainer *self,
                                                   gboolean interactive)
{
  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));
  adw_carousel_set_interactive (self->carousel, interactive);
}

void
kasasa_picture_container_update_buttons_sensibility (KasasaPictureContainer *pc)
{
  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (pc));

  if (adw_carousel_get_n_pages (pc->carousel) < MAX_SCREENSHOTS)
    gtk_widget_set_sensitive (GTK_WIDGET (pc->add_screenshot_button),
                              TRUE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (pc->add_screenshot_button),
                              FALSE);

  if (adw_carousel_get_n_pages (pc->carousel) > 1)
    gtk_widget_set_sensitive (GTK_WIDGET (pc->remove_screenshot_button),
                              TRUE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (pc->remove_screenshot_button),
                              FALSE);
}

static KasasaScreenshot *
get_current_screenshot (KasasaPictureContainer *self)
{
  guint position = (guint) adw_carousel_get_position (self->carousel);

  g_return_val_if_fail ((position < MAX_SCREENSHOTS), NULL);

  g_debug ("Carousel current position: %d", position);

  return KASASA_SCREENSHOT (adw_carousel_get_nth_page (self->carousel,
                                                       position));
}

// Load the screenshot to the GtkPicture widget
void
kasasa_picture_container_append_screenshot (KasasaPictureContainer *self,
                                            const gchar  *uri)
{
  KasasaScreenshot *new_screenshot = NULL;
  guint n_pages = adw_carousel_get_n_pages (self->carousel);
  g_debug ("Carousel number of pages: %d", n_pages);

  if (n_pages >= MAX_SCREENSHOTS)
    {
      g_warning ("Max number of screnshots reached");
      return;
    }

  new_screenshot = kasasa_screenshot_new ();
  adw_carousel_append (self->carousel, GTK_WIDGET (new_screenshot));
  kasasa_screenshot_load_screenshot (new_screenshot, uri);
  adw_carousel_scroll_to (self->carousel, GTK_WIDGET (new_screenshot), TRUE);
}

void
kasasa_picture_container_handle_taken_screenshot (GObject      *object,
                                                  GAsyncResult *res,
                                                  gpointer      user_data,
                                                  gboolean      retaking_screenshot)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);
  KasasaWindow *window = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;

  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (self));

  window = kasasa_window_get_window_reference (GTK_WIDGET (self));
  kasasa_window_hide_window (window, FALSE,
                             NULL, NULL);

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

  if (retaking_screenshot)
    {
      // Replace with new screenshot
      kasasa_screenshot_load_screenshot (get_current_screenshot (self), uri);
    }
  else
    {
      // Add new screenshot
      kasasa_picture_container_append_screenshot (self, uri);
    }

  // Set the focus to the retake_screenshot_button
  gtk_window_set_focus (GTK_WINDOW (window), GTK_WIDGET (self->retake_screenshot_button));
}

static void
on_mouse_enter_controls (GtkEventControllerMotion *event_controller_motion,
                        gdouble                   x,
                        gdouble                   y,
                        gpointer                  user_data)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_change_opacity (window, OPACITY_INCREASE);
}

static void
on_mouse_leave_controls (GtkEventControllerMotion *event_controller_motion,
                        gdouble                   x,
                        gdouble                   y,
                        gpointer                  user_data)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_change_opacity (window, OPACITY_DECREASE);
}

static void
on_page_changed (AdwCarousel *carousel,
                 guint        index,
                 gpointer     user_data)
{
  // TODO remove comments
  /* KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data); */
  /* KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self)); */
  /* gint new_height, new_width; */

  /* g_debug ("Page changed"); */
  /* // If the carousel is empty, return */
  /* if ((int) index == -1) */
  /*   return; */

  /* // Ensure that the window is visible */
  /* kasasa_window_change_opacity (window, OPACITY_INCREASE); */

  /* g_debug ("Resizing window for image at index %d due to page change", index); */
  /* kasasa_secreenshot_get_dimensions (get_current_screenshot (self), */
  /*                                    &new_height, */
  /*                                    &new_width); */

  /* kasasa_window_resize_window (window, new_height, new_width); */
}

static void
on_remove_screenshot_button_clicked (GtkButton *button,
                                     gpointer   user_data)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);
  gdouble curent_position;
  KasasaScreenshot *curent_screenshot = get_current_screenshot (self);
  KasasaScreenshot *neighbor_screenshot = NULL;

  adw_carousel_set_interactive (self->carousel, FALSE);

  // First trash the image with KasasaScreenshot instance (it needs a reference
  // to the parent window), before removing the widget from the carousel
  kasasa_screenshot_trash_image (curent_screenshot);

  /*
   * After calling 'adw_carousel_remove ()' a 'page-changed' signal is not emitted,
   * so the window don't get resized; also, calling 'resize_window ()', it access
   * a wrong carousel index due to a race condition.
   *
   * Workaround: get the neighbor screenshot and forcibly scroll to it - to delete
   * a screenshot, the window must hold at least 2 screenshots
   */
  curent_position = adw_carousel_get_position (self->carousel);
  if (curent_position == 0)
    {
      // If the deleted screenshot is at index 0, get the next one...
      neighbor_screenshot =
        KASASA_SCREENSHOT (adw_carousel_get_nth_page (self->carousel,
                                                      curent_position+1));
    }
  else
    {
      // ...otherwise, always get the previous one.
      neighbor_screenshot =
        KASASA_SCREENSHOT (adw_carousel_get_nth_page (self->carousel,
                                                      curent_position-1));
    }

  adw_carousel_remove (self->carousel, GTK_WIDGET (curent_screenshot));

  adw_carousel_scroll_to (self->carousel, GTK_WIDGET (neighbor_screenshot), TRUE);

  kasasa_picture_container_update_buttons_sensibility (self);

  adw_carousel_set_interactive (self->carousel, TRUE);
}

// Copy the image to the clipboard
static void
on_copy_screenshot_button_clicked (GtkButton *button,
                                   gpointer   user_data)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);
  g_autoptr (GError) error = NULL;
  GdkClipboard *clipboard = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  AdwToast *toast = NULL;

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  texture = gdk_texture_new_from_file (kasasa_screenshot_get_file (get_current_screenshot (self)),
                                       &error);

  if (error != NULL)
    {
      toast = adw_toast_new_format (_("Error: %s"), error->message);
      adw_toast_set_action_target_value (toast, g_variant_new_string (error->message));
      adw_toast_set_button_label (toast, _("Copy"));
      adw_toast_set_action_name (toast, "toast.copy_error");
      adw_toast_overlay_add_toast (self->toast_overlay, toast);
      g_warning ("%s", error->message);

      // Make the copy button insensitive on failure
      gtk_widget_set_sensitive (GTK_WIDGET (self->copy_screenshot_button), FALSE);

      return;
    }

  gdk_clipboard_set_texture (clipboard, texture);
  toast = adw_toast_new (_("Copied to the clipboard"));
  adw_toast_overlay_add_toast (self->toast_overlay, toast);
}

static void
on_lock_button_toggled (GtkToggleButton *button,
                        gpointer         user_data)
{
  if (gtk_toggle_button_get_active (button))
    {
      gtk_button_set_icon_name (GTK_BUTTON (button), "padlock2-symbolic");
      gtk_widget_remove_css_class (GTK_WIDGET (button), "osd");
      gtk_widget_add_css_class (GTK_WIDGET (button), "warning");
    }
  else
    {
      gtk_button_set_icon_name (GTK_BUTTON (button), "padlock2-open-symbolic");
      gtk_widget_remove_css_class (GTK_WIDGET (button), "warning");
      gtk_widget_add_css_class (GTK_WIDGET (button), "osd");
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

// TODO this function is only for tests
static void
add_screencast (GtkButton *button,
                gpointer user_data)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (user_data);

  KasasaScreencast *screencast = kasasa_screencast_new ();
  g_message ("Clicked");
  kasasa_screencast_set_window (screencast);
  adw_carousel_append (self->carousel, GTK_WIDGET (screencast));
}

static void
kasasa_picture_container_dispose (GObject *object)
{
  KasasaPictureContainer *self = KASASA_PICTURE_CONTAINER (object);

  g_clear_object (&self->portal);
  g_clear_object (&self->settings);

  gtk_widget_dispose_template (GTK_WIDGET (object), KASASA_TYPE_PICTURE_CONTAINER);

  G_OBJECT_CLASS (kasasa_picture_container_parent_class)->dispose (object);
}

static void
kasasa_picture_container_class_init (KasasaPictureContainerClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = kasasa_picture_container_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/github/kelvinnovais/Kasasa/kasasa-picture-container.ui");

  gtk_widget_class_install_action (widget_class, "toast.copy_error", "s", copy_error_cb);

  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, carousel);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, retake_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, add_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, remove_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, copy_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, lock_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, revealer_start_buttons);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, revealer_end_buttons);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, revealer_lock_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaPictureContainer, toolbar_overlay);
}

static void
kasasa_picture_container_init (KasasaPictureContainer *self)
{
  GtkEventController *toolbar_motion_event_controller = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->portal = xdp_portal_new ();
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  // Signals
  g_signal_connect (self->carousel,
                    "page-changed",
                    G_CALLBACK (on_page_changed),
                    self);
  g_signal_connect (self->retake_screenshot_button,
                    "clicked",
                    G_CALLBACK (routines_retake_screenshot),
                    self);
  g_signal_connect (self->add_screenshot_button,
                    "clicked",
                    G_CALLBACK (add_screencast),
                    self);
  g_signal_connect (self->remove_screenshot_button,
                    "clicked",
                    G_CALLBACK (on_remove_screenshot_button_clicked),
                    self);
  g_signal_connect (self->copy_screenshot_button,
                    "clicked",
                    G_CALLBACK (on_copy_screenshot_button_clicked),
                    self);
  g_signal_connect (self->lock_button,
                    "toggled",
                    G_CALLBACK (on_lock_button_toggled),
                    self);

  // Event controllers
  toolbar_motion_event_controller = gtk_event_controller_motion_new ();
  g_signal_connect (toolbar_motion_event_controller,
                    "enter",
                    G_CALLBACK (on_mouse_enter_controls),
                    self);
  g_signal_connect (toolbar_motion_event_controller,
                    "leave",
                    G_CALLBACK (on_mouse_leave_controls),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self->toolbar_overlay), toolbar_motion_event_controller);
}

KasasaPictureContainer *
kasasa_picture_container_new (void)
{
  return KASASA_PICTURE_CONTAINER (g_object_new (KASASA_TYPE_PICTURE_CONTAINER,
                                                 NULL));
}
