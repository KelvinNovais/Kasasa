/* kasasa-screenshot.c
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

#include "kasasa-screenshot.h"
#include "kasasa-window-private.h"

struct _KasasaScreenshot
{
  AdwBin                  parent_instance;

  /* Instance variables */
  GFile                  *file;
  GtkPicture             *picture;
  gdouble                 nat_width;
  gdouble                 nat_height;
  gint                    image_height;
  gint                    image_width;
};

G_DEFINE_FINAL_TYPE (KasasaScreenshot, kasasa_screenshot, ADW_TYPE_BIN)

gint
kasasa_screenshot_get_image_height (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), -1);
  return self->image_height;
}

gint
kasasa_screenshot_get_image_width (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), -1);
  return self->image_width;
}

GFile *
kasasa_screenshot_get_file (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), NULL);
  return self->file;
}

gdouble
kasasa_screenshot_get_nat_width (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), -1);
  return self->nat_width;
}

gdouble
kasasa_screenshot_get_nat_height (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), -1);
  return self->nat_height;
}

void
kasasa_screenshot_set_nat_width (KasasaScreenshot *self,
                                 gdouble           nat_width)
{
  g_return_if_fail (KASASA_IS_SCREENSHOT (self));
  self->nat_width = nat_width;
}

void
kasasa_screenshot_set_nat_height (KasasaScreenshot *self,
                                  gdouble           nat_height)
{
  g_return_if_fail (KASASA_IS_SCREENSHOT (self));
  self->nat_height = nat_height;
}

// Returns FALSE if not trashed
static gboolean
search_and_trash_image (const gchar *directory_name,
                        const gchar *file_name)
{
  g_autoptr (GFile) directory = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GFileEnumerator) enumerator = NULL;
  GFileInfo *info = NULL;

  // Get the pictures directory
  directory = g_file_new_for_path (directory_name);

  enumerator = g_file_enumerate_children (directory,                            // directory
                                          G_FILE_ATTRIBUTE_STANDARD_NAME,       // attributes
                                          G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,  // flags
                                          NULL,                                 // cancellable
                                          &error);                              // error

  if (error != NULL)
    {
      g_warning ("Error while deleting screenshot - couldn't enumerate directory: %s",
                 error->message);
      return FALSE;
    }

  while ((info = g_file_enumerator_next_file (enumerator, NULL, NULL)) != NULL)
    {
      const gchar *name = g_file_info_get_name (info);  // no need to free
      GFile *file = g_file_get_child (directory, name);

      if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
        {
          // Recursively search in subdirectories
          g_autofree gchar *path = g_file_get_path (file);
          g_debug ("Searching on %s ...", path);

          // If returned TRUE, finalize the loop returning TRUE
          if (search_and_trash_image (path, file_name) == TRUE)
            return TRUE;
        }
      else if (g_strcmp0 (name, file_name) == 0)
        {
          // Trash the image
          g_autofree gchar *parent_path = g_file_get_path (file);
          g_autoptr (GFile) trash_file = g_file_new_for_path (parent_path);

          g_file_trash (trash_file, NULL, &error);
          if (error != NULL)
            g_warning ("Error while deleting screenshot: %s", error->message);

          // Finilize
          g_debug ("Trasehd %s", parent_path);
          g_clear_object (&info);
          g_object_unref (file);
          return TRUE;
        }

      g_object_unref (info);
      g_object_unref (file);
    }

  g_clear_object (&info);
  return FALSE;
}

void
kasasa_screenshot_trash_image (KasasaScreenshot *self)
{
  GtkRoot *root = NULL;
  KasasaWindow *window = NULL;
  g_autofree gchar *base_name = NULL;

  g_return_if_fail (KASASA_IS_SCREENSHOT (self));

  root = gtk_widget_get_root (GTK_WIDGET (self));
  window = KASASA_WINDOW (root);

  // Return if auto trashing screenshot is not enabled
  if (kasasa_window_get_trash_button_active (window) == FALSE)
    return;

  g_debug ("Auto trashing screenshot...");

  // Get the image base name
  if (self->file == NULL
      || (base_name = g_file_get_basename (self->file)) == NULL)
    {
      g_warning ("Error while deleting screenshot: no reference to image");
      return;
    }

  if (search_and_trash_image (g_get_user_special_dir (G_USER_DIRECTORY_PICTURES),
                              base_name))
    gtk_picture_set_file (self->picture, NULL);

  return;
}

// Load the screenshot to the GtkPicture widget
void
kasasa_screenshot_load_screenshot (KasasaScreenshot *self,
                                   const gchar      *uri)
{
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkTexture) texture = NULL;

  g_return_if_fail (KASASA_IS_SCREENSHOT (self) || uri == NULL);

  if (self->file != NULL)
    kasasa_screenshot_trash_image (self);

  self->file = g_file_new_for_uri (uri);

  // Save image information
  texture = gdk_texture_new_from_file (self->file, &error);
  self->image_height = gdk_texture_get_height (texture);
  self->image_width = gdk_texture_get_width (texture);

  // Explicity unset the previous image: for some reason the old image doesn't get
  // replaced if the new image have the same size
  gtk_picture_set_file (self->picture, NULL);
  gtk_picture_set_file (self->picture, self->file);
}

static void
kasasa_screenshot_dispose (GObject *object)
{
  KasasaScreenshot *self = KASASA_SCREENSHOT (object);

  if (self->file != NULL)
    g_object_unref (self->file);

  G_OBJECT_CLASS (kasasa_screenshot_parent_class)->dispose (object);
}

static void
kasasa_screenshot_class_init (KasasaScreenshotClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = kasasa_screenshot_dispose;
}

static void
kasasa_screenshot_init (KasasaScreenshot *self)
{
  self->picture = GTK_PICTURE (gtk_picture_new ());
  adw_bin_set_child (ADW_BIN (self), GTK_WIDGET (self->picture));
  gtk_widget_set_valign (GTK_WIDGET (self), GTK_ALIGN_END);
}

KasasaScreenshot *
kasasa_screenshot_new (void)
{
  return KASASA_SCREENSHOT (g_object_new (KASASA_TYPE_SCREENSHOT, NULL));
}

