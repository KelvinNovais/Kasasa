/* kasasa-content-container.h
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

#define KASASA_TYPE_CONTENT_CONTAINER (kasasa_content_container_get_type ())

G_DECLARE_FINAL_TYPE (KasasaContentContainer, kasasa_content_container, KASASA, CONTENT_CONTAINER, AdwBreakpointBin)

KasasaContentContainer *kasasa_content_container_new (void);

void
kasasa_content_container_carousel_set_interactive (KasasaContentContainer *cc,
                                                   gboolean                interactive);
void kasasa_content_container_request_first_screenshot (KasasaContentContainer *cc);
void kasasa_content_container_request_window_resize (KasasaContentContainer *cc);
void kasasa_content_container_update_buttons_sensibility (KasasaContentContainer *cc);
void kasasa_content_container_reveal_controls (KasasaContentContainer *cc,
                                               gboolean                reveal_child);
gboolean kasasa_content_container_controls_active (KasasaContentContainer *cc);

void kasasa_content_container_wipe_content (KasasaContentContainer *cc);

G_END_DECLS
