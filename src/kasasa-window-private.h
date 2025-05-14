/* kasasa-window-private.h
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

#include <libportal/portal.h>

#include "kasasa-window.h"

G_BEGIN_DECLS

#define HIDE_WINDOW_TIME 110
#define WAITING_HIDE_WINDOW_TIME (2 * HIDE_WINDOW_TIME)

#define MAX_SCREENSHOTS 5

struct _KasasaWindow
{
  AdwApplicationWindow  parent_instance;

  /* Template widgets */
  GtkPicture          *picture;
  GtkWindowHandle     *picture_container;
  GtkButton           *retake_screenshot_button;
  GtkButton           *add_screenshot_button;
  GtkButton           *remove_screenshot_button;
  GtkButton           *copy_button;
  AdwToastOverlay     *toast_overlay;
  AdwHeaderBar        *header_bar;
  GtkRevealer         *header_bar_revealer;
  GtkRevealer         *toolbar_revealer;
  GtkMenuButton       *menu_button;
  GtkToggleButton     *auto_discard_button;
  GtkToggleButton     *auto_trash_button;
  AdwCarousel         *carousel;
  GtkProgressBar      *progress_bar;

  /* State variables */
  gboolean             hide_menu_requested;
  gboolean             mouse_over_window;
  gboolean             hiding_window;

  /* Instance variables */
  GSettings           *settings;
  XdpPortal           *portal;
  GtkEventController  *win_motion_event_controller;
  GtkEventController  *win_scroll_event_controller;
  GtkEventController  *header_bar_motion_event_controller;
  AdwAnimation        *window_opacity_animation;
  gint                 default_height;
  gint                 default_width;
  GCancellable        *auto_discard_canceller;
};

gboolean kasasa_window_get_trash_button_active (KasasaWindow *window);
void kasasa_window_append_screenshot (KasasaWindow *self, const gchar  *uri);
void kasasa_window_auto_discard_window (KasasaWindow *self);
void kasasa_window_hide_window (KasasaWindow *self, gboolean hide);
void kasasa_window_handle_taken_screenshot (GObject      *object,
                                            GAsyncResult *res,
                                            gpointer      user_data,
                                            gboolean      retaking_screenshot);
void kasasa_window_update_buttons_sensibility (KasasaWindow *self);

G_END_DECLS

/*
 * Descanxe em paz, tia Eldenir
 * nós te amamos muito.
 *
 * 10/05/25
 */
