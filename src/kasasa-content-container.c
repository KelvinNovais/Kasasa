/* kasasa-content-container.c
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

#include <libportal-gtk4/portal-gtk4.h>
#include <glib/gi18n.h>
#include <math.h>

#include "kasasa-content-container.h"

#include "kasasa-window.h"
#include "kasasa-screenshot.h"
#include "kasasa-screencast.h"

struct _KasasaContentContainer
{
  AdwBreakpointBin         parent_instance;

  /* Template widgets */
  AdwToastOverlay         *toast_overlay;
  AdwCarousel             *carousel;
  GtkButton               *retake_screenshot_button;
  GtkButton               *add_screenshot_button;
  GtkButton               *add_screenshot_button2;
  GtkButton               *add_screencast_button;
  GtkButton               *remove_content_button;
  GtkButton               *copy_screenshot_button;
  GtkMenuButton           *more_actions_button;
  GtkRevealer             *revealer_end_buttons;
  GtkRevealer             *revealer_start_buttons;
  GtkOverlay              *toolbar_overlay;
  GtkPopover              *more_actions_popover;

  /* Instance variables */
  XdpPortal               *portal;
  XdpParent               *parent;
  GSettings               *settings;
};

G_DEFINE_FINAL_TYPE (KasasaContentContainer, kasasa_content_container, ADW_TYPE_BREAKPOINT_BIN)

static GtkWidget * get_current_content (KasasaContentContainer *self);

gboolean
kasasa_content_container_controls_active (KasasaContentContainer *self)
{
  g_return_val_if_fail (KASASA_IS_CONTENT_CONTAINER (self), FALSE);

  return gtk_menu_button_get_active (self->more_actions_button);
}

void
kasasa_content_container_reveal_controls (KasasaContentContainer *self,
                                          gboolean                reveal_child)
{
  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));

  gtk_revealer_set_reveal_child (self->revealer_start_buttons, reveal_child);
  gtk_revealer_set_reveal_child (self->revealer_end_buttons, reveal_child);
}

void
kasasa_content_container_request_window_resize (KasasaContentContainer *self)
{
  KasasaWindow *window = NULL;
  GtkWidget *content = NULL;
  gint new_height, new_width;

  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));

  window = kasasa_window_get_window_reference (GTK_WIDGET (self));
  content = get_current_content (self);

  kasasa_content_get_dimensions (KASASA_CONTENT (content),
                                 &new_height,
                                 &new_width);

  kasasa_window_resize_window_scaling (window,
                                       (gdouble) new_height,
                                       (gdouble) new_width);
}

void
kasasa_content_container_wipe_content (KasasaContentContainer *self)
{
  guint n_pages = 0;

  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));

  n_pages = adw_carousel_get_n_pages (self->carousel);

  adw_carousel_set_interactive (self->carousel, FALSE);

  // Request finishing content from the last to the first page of the carousel.
  // Pictures are only deleted if the trash_button is toggled
  for (gint i = n_pages-1; i >= 0; i--)
    {
      GtkWidget *content = adw_carousel_get_nth_page (self->carousel, i);

      // Finish the content (KasasaSCreenshot needs a reference to the parent
      // window)...
      kasasa_content_finish (KASASA_CONTENT (content));
      // ...then remove it from the carousel
      adw_carousel_remove (self->carousel, content);
    }
}

void
kasasa_content_container_carousel_set_interactive (KasasaContentContainer *self,
                                                   gboolean interactive)
{
  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));
  adw_carousel_set_interactive (self->carousel, interactive);
}

void
kasasa_content_container_update_buttons_sensibility (KasasaContentContainer *self)
{
  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));

  if (adw_carousel_get_n_pages (self->carousel) < MAX_SCREENSHOTS)
    gtk_widget_set_sensitive (GTK_WIDGET (self->add_screenshot_button),
                              TRUE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (self->add_screenshot_button),
                              FALSE);


  if (adw_carousel_get_n_pages (self->carousel) > 1)
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_content_button),
                              TRUE);
  else
    gtk_widget_set_sensitive (GTK_WIDGET (self->remove_content_button),
                              FALSE);
}

static void
get_parent (KasasaContentContainer *self)
{
  GtkWindow *window = NULL;

  window = GTK_WINDOW (kasasa_window_get_window_reference (GTK_WIDGET (self)));

  self->parent = xdp_parent_new_gtk (window);
}

// Load the screenshot to the GtkPicture widget
static void
append_screenshot (KasasaContentContainer *self,
                   const gchar            *uri)
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

static void
handle_taken_screenshot (GObject      *object,
                         GAsyncResult *res,
                         gpointer      user_data,
                         gboolean      retaking_screenshot)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = NULL;
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;

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
      KasasaScreenshot *screenshot =
        KASASA_SCREENSHOT (get_current_content (self));

      kasasa_screenshot_load_screenshot (screenshot, uri);
    }
  else
    {
      // Add new screenshot
      append_screenshot (self, uri);
    }

  // Set the focus to the retake_screenshot_button
  gtk_window_set_focus (GTK_WINDOW (window), GTK_WIDGET (self->retake_screenshot_button));
}

static void
on_screencast_new_dimension (KasasaScreencast *screencast,
                             gint              new_width,
                             gint              new_height,
                             gpointer          user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  if (get_current_content (self) == GTK_WIDGET (screencast))
    kasasa_window_resize_window_scaling (window,
                                         (gdouble) new_height,
                                         (gdouble) new_width);
}

static void
on_screencast_eos (KasasaScreencast *screencast,
                   gpointer          user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);

  kasasa_content_finish (KASASA_CONTENT (screencast));

  if (adw_carousel_get_n_pages (self->carousel) > 2)
    adw_carousel_remove (self->carousel, GTK_WIDGET (screencast));

  kasasa_content_container_update_buttons_sensibility (self);
}




/*************************** TAKE FIRST SCREENSHOT ***************************/
// take_first_screenshot -> on_first_screenshot_taken
static void
on_first_screenshot_taken (GObject      *object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));
  g_autoptr (GSettings) settings = g_settings_new ("io.github.kelvinnovais.Kasasa");
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;
  g_autofree gchar *error_message = NULL;
  g_autoptr (GNotification) notification = NULL;
  g_autoptr (GIcon) icon = NULL;

  uri = xdp_portal_take_screenshot_finish (
    self->portal,
    res,
    &error
  );

  // The Portal API doesn't return if the user cancelled the screenshot. Simply
  // exit the app in this case.
  if (error != NULL)
    goto EXIT_APP;

  if (uri == NULL)
    {
      // translators: reason which the screenshot failed
      error_message = g_strconcat (_("Reason: "), _("Couldn't load the screenshot"), NULL);
      goto ERROR_NOTIFICATION;
    }

  append_screenshot (self, uri);

  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);

  // Enable auto discard window timer
  if (g_settings_get_boolean (settings, "auto-discard-window"))
    kasasa_window_auto_discard_window (window);

  kasasa_window_miniaturize_window (window, TRUE);
  return;

ERROR_NOTIFICATION:
  icon = g_themed_icon_new ("dialog-warning-symbolic");
  notification = g_notification_new (_("Screenshot failed"));
  g_notification_set_icon (notification, icon);
  g_notification_set_body (notification, error_message);
  g_application_send_notification (g_application_get_default (),
                                   "io.github.kelvinnovais.Kasasa",
                                   notification);

EXIT_APP:
  g_warning ("%s", error->message);

  gtk_window_close (GTK_WINDOW (window));
  return;
}

static void
take_first_screenshot (KasasaContentContainer *self)
{
  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));

  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_first_screenshot_taken,
    self
  );
}
/******************************************************************************/




/******************************* ADD SCREENSHOT *******************************/
// take_screenshot -> take_screenshot_cb -> on_screenshot_taken
static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  handle_taken_screenshot (object, res, user_data, FALSE);

  kasasa_content_container_update_buttons_sensibility (self);

  kasasa_window_block_miniaturization (window, FALSE);
}

static void
take_screenshot_cb (gpointer user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);

  xdp_portal_take_screenshot (
    self->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    self
  );
}

static void
take_screenshot (GtkButton *button,
                gpointer   user_data)
{
  KasasaContentContainer *self = NULL;
  KasasaWindow *window = NULL;

  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (user_data));

  self = KASASA_CONTENT_CONTAINER (user_data);

  gtk_popover_popdown (self->more_actions_popover);

  window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  gtk_widget_set_sensitive (GTK_WIDGET (self->add_screenshot_button), FALSE);

  kasasa_window_block_miniaturization (window, TRUE);

  kasasa_window_hide_window (window, TRUE,
                             take_screenshot_cb, self);
}
/******************************************************************************/




/***************************** RETAKE SCREENSHOT ******************************/
// retake_screenshot -> retake_screenshot_cb -> on_screenshot_retaken
static void
on_screenshot_retaken (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_block_miniaturization (window, FALSE);

  handle_taken_screenshot (object, res, user_data, TRUE);

  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button),
                            TRUE);

  // Enable carousel again
  adw_carousel_set_interactive (self->carousel, TRUE);
}

static void
retake_screenshot_cb (gpointer user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);

  // Avoid changing the carousel page
  adw_carousel_set_interactive (self->carousel, FALSE);

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
retake_screenshot (GtkButton *button, gpointer user_data)
{
  KasasaContentContainer *self = NULL;
  KasasaWindow *window = NULL;

  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (user_data));

  self = KASASA_CONTENT_CONTAINER (user_data);
  window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button), FALSE);

  kasasa_window_block_miniaturization (window, TRUE);

  kasasa_window_hide_window (window, TRUE,
                             retake_screenshot_cb, self);
}
/******************************************************************************/




/********************************* SCREENCAST *********************************/
// create_screencast_session -> create_screencast_session_cb -> on_screencast_session_started
static void
on_screencast_session_started (GObject      *source_object,
                               GAsyncResult *res,
                               gpointer      data)
{
  gint fd;
  guint node_id;

  g_autoptr (GError) error = NULL;
  g_autoptr (GVariant) streams = NULL;
  g_autoptr (GVariant) stream = NULL;
  KasasaScreencast *screencast = NULL;
  XdpSession *session = NULL;
  gboolean success;

  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_block_miniaturization (window, FALSE);

  session = XDP_SESSION (source_object);
  success = xdp_session_start_finish (session,
                                      res,
                                      &error);

  if (error != NULL || !success)
    {
      g_warning ("Couldn't start the session successfully: %s", error->message);
      return;
    }

  fd = xdp_session_open_pipewire_remote (session);

  streams = xdp_session_get_streams (session);
  stream = g_variant_get_child_value (streams, 0);
  g_variant_get (stream,
                 "(ua{sv})", &node_id, NULL);

  g_debug ("Streams: %s", g_variant_print (streams, TRUE));

  screencast = kasasa_screencast_new ();

  g_signal_connect (screencast, "new-dimension",
                    G_CALLBACK (on_screencast_new_dimension), self);
  g_signal_connect (screencast, "eos",
                    G_CALLBACK (on_screencast_eos), self);

  kasasa_screencast_show (screencast, session, fd, node_id);
  adw_carousel_append (self->carousel, GTK_WIDGET (screencast));
  adw_carousel_scroll_to (self->carousel, GTK_WIDGET (screencast), TRUE);
  kasasa_content_container_update_buttons_sensibility (self);
}

static void
create_screencast_session_cb (GObject      *source_object,
                              GAsyncResult *res,
                              gpointer      data)
{
  g_autoptr (GError) error = NULL;
  XdpSession *session = NULL;
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (data);

  session =
    xdp_portal_create_screencast_session_finish (XDP_PORTAL (source_object),
                                                 res,
                                                 &error);

  if (error != NULL)
    {
      g_warning ("Failed to create screencast session: %s", error->message);
      return;
    }

  if (!self->parent)
    get_parent (self);

  xdp_session_start (session,
                     self->parent,
                     NULL,
                     on_screencast_session_started,
                     self);
}

static void
create_screencast_session (GtkButton *button,
                           gpointer   user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  gtk_popover_popdown (self->more_actions_popover);
  kasasa_window_block_miniaturization (window, TRUE);

  xdp_portal_create_screencast_session (self->portal,
                                        XDP_OUTPUT_WINDOW,
                                        XDP_SCREENCAST_FLAG_NONE,
                                        XDP_CURSOR_MODE_HIDDEN,
                                        XDP_PERSIST_MODE_TRANSIENT,
                                        NULL,
                                        NULL,
                                        create_screencast_session_cb,
                                        self);
}
/******************************************************************************/




void
kasasa_content_container_request_first_screenshot (KasasaContentContainer *self)
{
  g_return_if_fail (KASASA_IS_CONTENT_CONTAINER (self));
  take_first_screenshot (self);
}

static GtkWidget *
get_current_content (KasasaContentContainer *self)
{
  guint position = (guint) adw_carousel_get_position (self->carousel);

  g_return_val_if_fail ((position < MAX_SCREENSHOTS), NULL);

  g_debug ("Carousel current position: %d", position);

  return adw_carousel_get_nth_page (self->carousel, position);
}

static void
on_mouse_enter_controls (GtkEventControllerMotion *event_controller_motion,
                        gdouble                   x,
                        gdouble                   y,
                        gpointer                  user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_change_opacity (window, OPACITY_INCREASE);
}

static void
on_mouse_leave_controls (GtkEventControllerMotion *event_controller_motion,
                        gdouble                   x,
                        gdouble                   y,
                        gpointer                  user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  kasasa_window_change_opacity (window, OPACITY_DECREASE);
}

static void
on_page_changed (AdwCarousel *carousel,
                 guint        index,
                 gpointer     user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));
  GtkWidget *content = NULL;
  gint new_height, new_width;

  g_debug ("Page changed");
  // If the carousel is empty, return
  if ((gint) index == -1)
    return;

  // Ensure that the window is visible
  kasasa_window_change_opacity (window, OPACITY_INCREASE);

  g_debug ("Resizing window for content at index %d due to page change", index);
  content = get_current_content (self);

  if (KASASA_IS_SCREENCAST (content))
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->copy_screenshot_button),
                                FALSE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button),
                                FALSE);
    }
  else
    {
      gtk_widget_set_sensitive (GTK_WIDGET (self->copy_screenshot_button),
                                TRUE);
      gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button),
                                TRUE);
    }

  kasasa_content_get_dimensions (KASASA_CONTENT (content),
                                 &new_height,
                                 &new_width);

  kasasa_window_resize_window_scaling (window,
                                       (gdouble) new_height,
                                       (gdouble) new_width);
}

static void
on_remove_content_clicked (GtkButton *button,
                           gpointer   user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  gdouble current_position;
  GtkWidget *current_content = get_current_content (self);
  GtkWidget *neighbor_content = NULL;

  adw_carousel_set_interactive (self->carousel, FALSE);

  // Use the finish implementation for each class
  kasasa_content_finish (KASASA_CONTENT (current_content));

  /*
   * After calling 'adw_carousel_remove ()' a 'page-changed' signal is not emitted,
   * so the window don't get resized; also, calling 'resize_window ()', it access
   * a wrong carousel index due to a race condition.
   *
   * Workaround: get the neighbor content and forcibly scroll to it; to delete
   * a content, the window must hold at least 2 contents
   */
  current_position = adw_carousel_get_position (self->carousel);
  if (current_position == 0)
    {
      // If the deleted content is at index 0, get the next one...
      neighbor_content = adw_carousel_get_nth_page (self->carousel,
                                                    current_position+1);
    }
  else
    {
      // ...otherwise, always get the previous one.
      neighbor_content = adw_carousel_get_nth_page (self->carousel,
                                                    current_position-1);
    }

  adw_carousel_remove (self->carousel, current_content);

  adw_carousel_scroll_to (self->carousel, neighbor_content, TRUE);

  kasasa_content_container_update_buttons_sensibility (self);

  adw_carousel_set_interactive (self->carousel, TRUE);
}

// Copy the image to the clipboard
static void
on_copy_screenshot_button_clicked (GtkButton *button,
                                   gpointer   user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  g_autoptr (GError) error = NULL;
  GdkClipboard *clipboard = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  AdwToast *toast = NULL;
  GtkWidget *content = NULL;

  content = get_current_content (self);

  g_return_if_fail (KASASA_IS_SCREENSHOT (content));

  clipboard = gdk_display_get_clipboard (gdk_display_get_default ());

  texture =
    gdk_texture_new_from_file (kasasa_screenshot_get_file (KASASA_SCREENSHOT (content)),
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
on_menu_button_active (GObject    *object,
                       GParamSpec *pspec,
                       gpointer    user_data)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  if (gtk_menu_button_get_active (self->more_actions_button))
    kasasa_window_block_miniaturization (window, TRUE);
  else
    kasasa_window_block_miniaturization (window, FALSE);
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
kasasa_content_container_dispose (GObject *object)
{
  KasasaContentContainer *self = KASASA_CONTENT_CONTAINER (object);

  g_clear_object (&self->portal);
  g_clear_object (&self->settings);
  if (self->parent)
    xdp_parent_free (self->parent);

  gtk_widget_dispose_template (GTK_WIDGET (object), KASASA_TYPE_CONTENT_CONTAINER);

  G_OBJECT_CLASS (kasasa_content_container_parent_class)->dispose (object);
}

static void
kasasa_content_container_class_init (KasasaContentContainerClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = kasasa_content_container_dispose;

  gtk_widget_class_set_template_from_resource (widget_class,
                                               "/io/github/kelvinnovais/Kasasa/kasasa-content-container.ui");

  gtk_widget_class_install_action (widget_class, "toast.copy_error", "s", copy_error_cb);

  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, toast_overlay);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, carousel);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, retake_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, add_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, add_screenshot_button2);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, add_screencast_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, remove_content_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, copy_screenshot_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, more_actions_button);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, revealer_start_buttons);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, revealer_end_buttons);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, toolbar_overlay);
  gtk_widget_class_bind_template_child (widget_class, KasasaContentContainer, more_actions_popover);
}

static void
kasasa_content_container_init (KasasaContentContainer *self)
{
  GtkEventController *toolbar_motion_event_controller = NULL;

  gtk_widget_init_template (GTK_WIDGET (self));

  self->portal = xdp_portal_new ();
  self->parent = NULL;
  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  // Signals
  g_signal_connect (self->carousel,
                    "page-changed",
                    G_CALLBACK (on_page_changed),
                    self);
  g_signal_connect (self->retake_screenshot_button,
                    "clicked",
                    G_CALLBACK (retake_screenshot),
                    self);
  g_signal_connect (self->add_screenshot_button,
                    "clicked",
                    G_CALLBACK (take_screenshot),
                    self);
  g_signal_connect (self->add_screenshot_button2,
                    "clicked",
                    G_CALLBACK (take_screenshot),
                    self);
  g_signal_connect (self->add_screencast_button,
                    "clicked",
                    G_CALLBACK (create_screencast_session),
                    self);
  g_signal_connect (self->remove_content_button,
                    "clicked",
                    G_CALLBACK (on_remove_content_clicked),
                    self);
  g_signal_connect (self->copy_screenshot_button,
                    "clicked",
                    G_CALLBACK (on_copy_screenshot_button_clicked),
                    self);
  g_signal_connect (self->more_actions_button,
                    "notify::active",
                    G_CALLBACK (on_menu_button_active),
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
  gtk_widget_add_controller (GTK_WIDGET (self->toolbar_overlay),
                             toolbar_motion_event_controller);
}

KasasaContentContainer *
kasasa_content_container_new (void)
{
  return KASASA_CONTENT_CONTAINER (g_object_new (KASASA_TYPE_CONTENT_CONTAINER,
                                                 NULL));
}
