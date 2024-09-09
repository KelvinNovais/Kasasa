/* soslaio-screenshoter.h
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

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h> // TODO

G_BEGIN_DECLS

#define SOSLAIO_TYPE_SCREENSHOTER (soslaio_screenshoter_get_type())

G_DECLARE_FINAL_TYPE (SoslaioScreenshoter, soslaio_screenshoter, SOSLAIO, SCREENSHOTER, GObject)

/*
 * Method definitions
 */
SoslaioScreenshoter *soslaio_screenshoter_new (void /* GtkWindow *window */);
void soslaio_screenshoter_take_screenshot (SoslaioScreenshoter *self);

G_END_DECLS
