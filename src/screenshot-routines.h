/* screenshot-routines.h
 *
 * Copyright 2025 Kelvin Novais
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

#include "kasasa-picture-container.h"

G_BEGIN_DECLS

void screenshot_routines_take_first (KasasaPictureContainer *pc);
void screenshot_routines_add (GtkButton *button, gpointer pc);
void screenshot_routines_retake (GtkButton *button, gpointer pc);

G_END_DECLS
