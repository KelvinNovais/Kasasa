/* take-first-screenshot.c
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
#include <libportal/portal.h>

#include <take-first-screenshot.h>

static void
on_first_screenshot_taken (GObject      *object,
                           GAsyncResult *res,
                           gpointer      user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);
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

  kasasa_window_append_screenshot (self, uri);

  // Enable auto discard window timer
  if (g_settings_get_boolean (self->settings, "auto-discard-window"))
    kasasa_window_auto_discard_window (self);

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

EXIT_APP:
  g_warning ("%s", error->message);

  gtk_window_close (GTK_WINDOW (self));
  return;
}

void
take_first_screenshot (KasasaWindow *window)
{
  xdp_portal_take_screenshot (
    window->portal,
    NULL,
    XDP_SCREENSHOT_FLAG_INTERACTIVE,
    NULL,
    on_first_screenshot_taken,
    window
  );
}
