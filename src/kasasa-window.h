/* kasasa-window.h
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

#include <adwaita.h>

G_BEGIN_DECLS

typedef enum
{
  OPACITY_INCREASE,
  OPACITY_DECREASE
} Opacity;

#define HIDE_WINDOW_TIME 110
#define WAITING_HIDE_WINDOW_TIME (2 * HIDE_WINDOW_TIME)

// Due to miniaturization, the real min dimensions are set here (width-request
// and height-request)
#define WINDOW_MIN_HEIGHT 110
#define WINDOW_MIN_WIDTH  180

#define KASASA_TYPE_WINDOW (kasasa_window_get_type ())

G_DECLARE_FINAL_TYPE (KasasaWindow, kasasa_window, KASASA, WINDOW, AdwApplicationWindow)

KasasaWindow * kasasa_window_get_window_reference (GtkWidget *widget);
gboolean kasasa_window_get_trash_button_active (KasasaWindow *window);
void kasasa_window_hide_window (KasasaWindow *window, gboolean hide);
void kasasa_window_change_opacity (KasasaWindow *window,
                                   Opacity       opacity_direction);
void kasasa_window_resize_window (KasasaWindow *window,
                                  gdouble       new_height,
                                  gdouble       new_width);
void kasasa_window_auto_discard_window (KasasaWindow *window);
void kasasa_window_miniaturize_window (KasasaWindow *window, gboolean miniaturize);
void kasasa_window_block_miniaturization (KasasaWindow *window, gboolean block);

G_END_DECLS

/*
 * Descanxe em paz, tia Eldenir
 * nós te amamos muito.
 *
 * 10/05/25
 */
