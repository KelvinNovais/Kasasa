/* retake-screenshot.c
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

#include "retake-screenshot.h"

static void
on_screenshot_retaken (GObject      *object,
                       GAsyncResult *res,
                       gpointer      user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  kasasa_window_handle_taken_screenshot (object, res, user_data, TRUE);

  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button),
                            TRUE);

  // Enable carousel again
  adw_carousel_set_interactive (self->carousel, TRUE);
}

static void
retake_screenshot_cb (gpointer user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

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

void
retake_screenshot (GtkButton *button,
                   gpointer   user_data)
{
  KasasaWindow *self = KASASA_WINDOW (user_data);

  gtk_widget_set_sensitive (GTK_WIDGET (self->retake_screenshot_button), FALSE);

  kasasa_window_hide_window (self, TRUE);

  g_timeout_add_once (WAITING_HIDE_WINDOW_TIME, retake_screenshot_cb, self);
}
