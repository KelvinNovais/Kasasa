/* kasasa-picture-container-private.h
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

#include "kasasa-picture-container.h"

G_BEGIN_DECLS

struct _KasasaPictureContainer
{
  AdwBreakpointBin         parent_instance;

  /* Template widgets */
  AdwToastOverlay         *toast_overlay;
  AdwCarousel             *carousel;
  GtkButton               *retake_screenshot_button;
  GtkButton               *add_screenshot_button;
  GtkButton               *remove_screenshot_button;
  GtkButton               *copy_screenshot_button;
  GtkRevealer             *revealer_end_buttons;
  GtkRevealer             *revealer_start_buttons;
  GtkOverlay              *toolbar_overlay;

  /* Instance variables */
  XdpPortal               *portal;
};

void kasasa_picture_container_append_screenshot (KasasaPictureContainer *pc,
                                                 const gchar            *uri);
void kasasa_picture_container_handle_taken_screenshot (GObject      *object,
                                                       GAsyncResult *res,
                                                       gpointer      pc,
                                                       gboolean      retaking_screenshot);

G_END_DECLS
