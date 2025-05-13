/* add-screenshot.c
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

#include <libportal/portal.h>

#include "add-screenshot.h"

static void
on_screenshot_taken (GObject      *object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  kasasa_window_handle_taken_screenshot (object, res, user_data, FALSE);

  kasasa_window_update_buttons_sensibility (self);
}

static void
take_screenshot (gpointer user_data)
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

void
add_screenshot (GtkButton *button,
                gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  gtk_widget_set_sensitive (GTK_WIDGET (self->add_screenshot_button), FALSE);

  kasasa_window_hide_window (self, TRUE);

  g_timeout_add_once (WAITING_HIDE_WINDOW_TIME, take_screenshot, self);
}
