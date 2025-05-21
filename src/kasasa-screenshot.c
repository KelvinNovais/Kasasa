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

// Defined on GSchema and preferences
#define MIN_OCCUPY_SCREEN 0.1

#include "kasasa-screenshot.h"
#include "kasasa-window.h"

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

GFile *
kasasa_screenshot_get_file (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), NULL);
  return self->file;
}

// Compute the window size
// Based on:
// https://gitlab.gnome.org/GNOME/Incubator/showtime/-/blob/main/showtime/window.py?ref_type=heads#L836
// https://gitlab.gnome.org/GNOME/loupe/-/blob/4ca5f9e03d18667db5d72325597cebc02887777a/src/widgets/image/rendering.rs#L151
static gboolean
compute_size (KasasaScreenshot *self)
{
  g_autoptr (GError) error = NULL;
  GtkNative *native = NULL;
  GdkDisplay *display = NULL;
  GdkSurface *surface = NULL;
  GdkMonitor *monitor = NULL;
  GdkRectangle monitor_geometry;
  // gints
  gint monitor_area, hidpi_scale, image_area, max_width, max_height;
  // gdoubles
  gdouble occupy_area_factor, size_scale, target_scale;

  g_autoptr (GSettings) settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  KasasaWindow *window = kasasa_window_get_window_reference (GTK_WIDGET (self));

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay, can't find the best window size");
      return TRUE;
    }

  native = gtk_widget_get_native (GTK_WIDGET (window));
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative, can't find the best window size");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface, can't find the best window size");
      return TRUE;
    }

  monitor = gdk_display_get_monitor_at_surface (display, surface);
  if (monitor == NULL)
    {
      g_warning ("Couldn't get GdkMonitor, can't find the best window size");
      return TRUE;
    }

  // AREAS
  image_area = self->image_height * self->image_width;

  hidpi_scale = gdk_surface_get_scale_factor (surface);

  gdk_monitor_get_geometry (monitor, &monitor_geometry);
  monitor_area = monitor_geometry.width * monitor_geometry.height;

  occupy_area_factor = g_settings_get_double (settings, "occupy-screen");

  // factor for width and height that will achieve the desired area
  // occupation derived from:
  // monitor_area * occupy_area_factor ==
  //   (image_width * size_scale) * (image_height * size_scale)
  size_scale = sqrt (monitor_area / image_area * occupy_area_factor);
  // ensure that size_scale is not ~ 0 (if image is too big, size_scale can reach 0)
  size_scale = (size_scale < MIN_OCCUPY_SCREEN) ? 0.3 : size_scale;
  // ensure that we never increase image size
  target_scale = MIN (1, size_scale);
  self->nat_width = self->image_width * target_scale;
  self->nat_height = self->image_height * target_scale;

  // Scale down if targeted occupation does not fit horizontally
  // Add some margin to not touch corners
  max_width = monitor_geometry.width - 20;
  if (self->nat_width > max_width)
    {
      guint previous_nat_width = self->nat_width;
      guint new_nat_width = max_width;
      guint new_nat_height = self->image_height * previous_nat_width / self->image_width;

      self->nat_width = new_nat_width;
      self->nat_height = new_nat_height;
    }

  // Same for vertical size
  // Additionally substract some space for HeaderBar and Shell bar
  max_height = monitor_geometry.height - (50 + 35 + 20) * hidpi_scale;
  if (self->nat_height > max_height)
    {
      guint previous_nat_height = self->nat_height;
      guint new_nat_height = max_height;
      guint new_nat_width = self->image_width * previous_nat_height / self->image_height;

      self->nat_width = new_nat_width;
      self->nat_height = new_nat_height;
    }

  self->nat_width = MAX (WINDOW_MIN_WIDTH, self->nat_width);
  self->nat_height = MAX (WINDOW_MIN_HEIGHT, self->nat_height);

  // If the header bar is NOT hiding, then the window height must have more 47 px
  if (!g_settings_get_boolean (settings, "auto-hide-menu"))
    self->nat_height += 47;

  return FALSE;
}

void
kasasa_secreenshot_get_dimensions (KasasaScreenshot *self,
                                   gint             *height,
                                   gint             *width)
{
  *height = -1;
  *width = -1;

  g_return_if_fail (KASASA_IS_SCREENSHOT (self));

  compute_size (self);

  *height = self->nat_height;
  *width = self->nat_width;
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
  KasasaWindow *window = NULL;
  g_autofree gchar *base_name = NULL;

  g_return_if_fail (KASASA_IS_SCREENSHOT (self));

  window = kasasa_window_get_window_reference (GTK_WIDGET (self));

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
  KasasaWindow *window = NULL;
  g_autoptr (GError) error = NULL;
  g_autoptr (GdkTexture) texture = NULL;
  gint height, width;

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

  // Compute new dimensions and resize the window
  kasasa_secreenshot_get_dimensions (self, &height, &width);
  window = kasasa_window_get_window_reference (GTK_WIDGET (self));
  kasasa_window_resize_window (window, height, width);
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

