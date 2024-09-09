/* soslaio-screenshoter.c
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

/* #include "libportal-gtk4/portal-gtk4.h" // TODO */
#include "libportal/portal.h"

#include <stdio.h>

#include "soslaio-screenshoter.h"

struct _SoslaioScreenshoter
{
  GObject parent_instance;

  /* Other members, including private data. */
  XdpPortal *portal;
  /* XdpParent *parent; // TODO */
  char *URI;
};

G_DEFINE_FINAL_TYPE (SoslaioScreenshoter, soslaio_screenshoter, G_TYPE_OBJECT)

/* TODO implement finalize function (to free memory) */

static void
soslaio_screenshoter_class_init (SoslaioScreenshoterClass *klass)
{

}

static void
soslaio_screenshoter_init (SoslaioScreenshoter *self)
{
  GError *error = NULL;

  self->portal = xdp_portal_initable_new (&error);

  if (error != NULL)
    {
      fprintf (stderr, "Unable to create an XDP portal: %s\n", error->message);
      g_error_free (error);
    }
}

SoslaioScreenshoter *
soslaio_screenshoter_new (void /* GtkWindow *window */) {
  SoslaioScreenshoter *instance = g_object_new (SOSLAIO_TYPE_SCREENSHOTER, NULL);

  /* instance->parent = xdp_parent_new_gtk (window); // TODO */

  return instance;
}

static void
on_screenshot_taken (GObject* source_object,
                     GAsyncResult* result,
                     gpointer data)
{
  GError *error = NULL;
  SoslaioScreenshoter *self = (SoslaioScreenshoter *) data;

  g_print ("Callback called\n");

  self->URI = xdp_portal_take_screenshot_finish (self->portal,
                                                 result,
                                                 &error);

  if (error != NULL)
    {
      fprintf (stderr, "Unable to get screenshot: %s\n", error->message);
      g_error_free (error);
    }
  else
    {
      printf ("URI: %s\n", self->URI);
    }
}

void soslaio_screenshoter_take_screenshot (SoslaioScreenshoter *self)
{
  // TODO handle if portal == NULL
  // TODO handle no permissions

  /* if (self->portal == NULL) */
  /*   fprintf (stderr, "Portal == NULL\n"); */

  /* xdp_portal_take_screenshot (self->portal,                     // portal */
  /*                             NULL,                             // parent TODO */
  /*                             XDP_SCREENSHOT_FLAG_NONE,         // flags */
  /*                             NULL,                             // cancellable */
  /*                             on_screenshot_taken,              // callback */
  /*                             self);                            // data */


    GDBusConnection *connection;
    g_autofree gchar *filename = NULL;
    g_autofree gchar *path = NULL;
    g_autofree gchar *uri = NULL;
    g_autoptr(GError) error = NULL;

    filename = g_strdup_printf ("org.adishatz.Screenshot-%d.png", g_random_int ());
    path = g_build_filename (g_get_user_cache_dir (), filename, NULL);

    connection = g_application_get_dbus_connection (g_application_get_default ());
    g_dbus_connection_call_sync (
        connection,
        "org.gnome.Shell.Screenshot",
        "/org/gnome/Shell/Screenshot",
        "org.gnome.Shell.Screenshot",
        "Screenshot",
        g_variant_new (
            "(bbs)",
            FALSE,
            TRUE,
            path),
        NULL,
        G_DBUS_CALL_FLAGS_NONE,
        -1,
        NULL,
        &error);

    if (error != NULL) {
          g_warning ("%s", error->message);
          return;
    }

    uri = g_strdup_printf ("file://%s", path);
    printf ("URI: %s\n", uri);
}
