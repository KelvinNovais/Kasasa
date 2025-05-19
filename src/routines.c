/* routines.c
 *
 * Copyright 2025 Kelvin
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

#include "routines.h"

#include "kasasa-picture-container-private.h"
#include "kasasa-window.h"




/* TAKE FIRST SCREENSHOT */
static void
on_first_screenshot_taken (GObject      *object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  KasasaPictureContainer *pc = KASASA_PICTURE_CONTAINER (user_data);
  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (pc));
  g_autoptr (GSettings) settings = g_settings_new ("io.github.kelvinnovais.Kasasa");
  g_autoptr (GError) error = NULL;
  g_autofree gchar *uri = NULL;
  g_autofree gchar *error_message = NULL;
  g_autoptr (GNotification) notification = NULL;
  g_autoptr (GIcon) icon = NULL;

  uri = xdp_portal_take_screenshot_finish (
    pc->portal,
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

  kasasa_picture_container_append_screenshot (pc, uri);

  // Enable auto discard window timer
  if (g_settings_get_boolean (settings, "auto-discard-window"))
    kasasa_window_auto_discard_window (window);

  gtk_widget_set_visible (GTK_WIDGET (window), TRUE);
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

void
routines_take_first_screenshot (KasasaPictureContainer *pc)
{
  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (pc));

  xdp_portal_take_screenshot (
    pc->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_first_screenshot_taken,
    pc
  );
}




/* ADD SCREENSHOT */
static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  KasasaPictureContainer *pc = KASASA_PICTURE_CONTAINER (user_data);

  kasasa_picture_container_handle_taken_screenshot (object, res, user_data, FALSE);

  kasasa_picture_container_update_buttons_sensibility (pc);
}

static void
take_screenshot (gpointer user_data)
{
  KasasaPictureContainer *pc = KASASA_PICTURE_CONTAINER (user_data);

  xdp_portal_take_screenshot (
    pc->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_taken,
    pc
  );
}

void
routines_add_screenshot (GtkButton *button,
                         gpointer   user_data)
{
  KasasaPictureContainer *pc = NULL;
  KasasaWindow *window = NULL;

  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (user_data));

  pc = KASASA_PICTURE_CONTAINER (user_data);
  window = kasasa_window_get_window_reference (GTK_WIDGET (pc));

  gtk_widget_set_sensitive (GTK_WIDGET (pc->add_screenshot_button), FALSE);

  kasasa_window_hide_window (window, TRUE);

  g_timeout_add_once (WAITING_HIDE_WINDOW_TIME, take_screenshot, pc);
}




/* RETAKE SCREENSHOT */
static void
on_screenshot_retaken (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  KasasaPictureContainer *pc = KASASA_PICTURE_CONTAINER (user_data);

  kasasa_picture_container_handle_taken_screenshot (object, res, user_data, TRUE);

  gtk_widget_set_sensitive (GTK_WIDGET (pc->retake_screenshot_button),
                            TRUE);

  // Enable carousel again
  adw_carousel_set_interactive (pc->carousel, TRUE);
}

static void
retake_screenshot_cb (gpointer user_data)
{
  KasasaPictureContainer *pc = KASASA_PICTURE_CONTAINER (user_data);

  // Avoid changing the carousel page
  adw_carousel_set_interactive (pc->carousel, FALSE);

  xdp_portal_take_screenshot (
    pc->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_screenshot_retaken,
    pc
  );
}

void
routines_retake_screenshot (GtkButton *button, gpointer user_data)
{
  KasasaPictureContainer *pc = NULL;
  KasasaWindow *window = NULL;

  g_return_if_fail (KASASA_IS_PICTURE_CONTAINER (user_data));

  pc = KASASA_PICTURE_CONTAINER (user_data);
  window = kasasa_window_get_window_reference (GTK_WIDGET (pc));

  gtk_widget_set_sensitive (GTK_WIDGET (pc->retake_screenshot_button), FALSE);

  kasasa_window_hide_window (window, TRUE);

  g_timeout_add_once (WAITING_HIDE_WINDOW_TIME, retake_screenshot_cb, pc);
}
