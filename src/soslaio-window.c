/* soslaio-window.c
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

#include "config.h"

#include "soslaio-window.h"

struct _SoslaioWindow
{
	AdwApplicationWindow  parent_instance;

	/* Template widgets */
	AdwHeaderBar        *header_bar;
	GtkLabel            *label;
};

G_DEFINE_FINAL_TYPE (SoslaioWindow, soslaio_window, ADW_TYPE_APPLICATION_WINDOW)

static void
soslaio_window_class_init (SoslaioWindowClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Soslaio/soslaio-window.ui");
	gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, header_bar);
	gtk_widget_class_bind_template_child (widget_class, SoslaioWindow, label);
}

static void
soslaio_window_init (SoslaioWindow *self)
{
	gtk_widget_init_template (GTK_WIDGET (self));
}
