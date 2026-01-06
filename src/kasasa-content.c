/* kasasa-content.c
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

#include "kasasa-content.h"

G_DEFINE_INTERFACE (KasasaContent, kasasa_content, G_TYPE_OBJECT)

void
kasasa_content_get_dimensions (KasasaContent *self,
                               gint          *height,
                               gint          *width)
{
  KasasaContentInterface *iface = NULL;

  g_return_if_fail (KASASA_IS_CONTENT (self));

  iface = KASASA_CONTENT_GET_IFACE (self);
  g_return_if_fail (iface->get_dimensions != NULL);

  iface->get_dimensions (self, height, width);
}

void
kasasa_content_finish (KasasaContent *self)
{
  KasasaContentInterface *iface = NULL;

  g_return_if_fail (KASASA_IS_CONTENT (self));

  iface = KASASA_CONTENT_GET_IFACE (self);
  g_return_if_fail (iface->finish != NULL);

  iface->finish (self);
}

static void
default_finish (KasasaContent *self)
{
  return;
}

static void
kasasa_content_default_init (KasasaContentInterface *iface)
{
  iface->finish = default_finish;
}

