/* kasasa-content-container-private.h
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

#include "kasasa-content-container.h"

G_BEGIN_DECLS

struct _KasasaContentContainer
{
  AdwBreakpointBin         parent_instance;

  /* Template widgets */
  AdwToastOverlay         *toast_overlay;
  AdwCarousel             *carousel;
  GtkButton               *retake_screenshot_button;
  GtkButton               *add_screenshot_button;
  GtkButton               *add_screenshot_button2;
  GtkButton               *add_screencast_button;
  GtkButton               *remove_content_button;
  GtkButton               *copy_screenshot_button;
  GtkToggleButton         *lock_button;
  GtkMenuButton           *more_actions_button;
  GtkRevealer             *revealer_end_buttons;
  GtkRevealer             *revealer_start_buttons;
  GtkRevealer             *revealer_lock_button;
  GtkOverlay              *toolbar_overlay;
  GtkPopover              *more_actions_popover;

  /* Instance variables */
  XdpPortal               *portal;
  GSettings               *settings;
};

void kasasa_content_container_append_screenshot (KasasaContentContainer *pc,
                                                 const gchar            *uri);
void kasasa_content_container_handle_taken_screenshot (GObject      *object,
                                                       GAsyncResult *res,
                                                       gpointer      pc,
                                                       gboolean      retaking_screenshot);

G_END_DECLS
