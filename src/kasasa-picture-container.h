/* kasasa-picture-container.h
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

#pragma once

#include <adwaita.h>
#include <libportal/portal.h>

G_BEGIN_DECLS

#define MAX_SCREENSHOTS 5

#define KASASA_TYPE_PICTURE_CONTAINER (kasasa_picture_container_get_type ())

G_DECLARE_FINAL_TYPE (KasasaPictureContainer, kasasa_picture_container, KASASA, PICTURE_CONTAINER, AdwBreakpointBin)

KasasaPictureContainer *kasasa_picture_container_new (void);

void
kasasa_picture_container_carousel_set_interactive (KasasaPictureContainer *pc,
                                                   gboolean                interactive);
void kasasa_picture_container_request_first_screenshot (KasasaPictureContainer *pc);
void kasasa_picture_container_request_window_resize (KasasaPictureContainer *pc);
void kasasa_picture_container_update_buttons_sensibility (KasasaPictureContainer *pc);
void kasasa_picture_container_reveal_controls (KasasaPictureContainer *pc,
                                               gboolean                reveal_child);
gboolean kasasa_picture_container_get_lock (KasasaPictureContainer *pc);
gboolean kasasa_picture_container_controls_active (KasasaPictureContainer *pc);

void kasasa_picture_container_wipe_content (KasasaPictureContainer *pc);

G_END_DECLS
