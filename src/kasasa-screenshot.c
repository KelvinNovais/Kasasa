/* kasasa-screenshot.c
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

static void kasasa_screenshot_content_interface_init (KasasaContentInterface *iface);

G_DEFINE_TYPE_WITH_CODE (KasasaScreenshot, kasasa_screenshot, ADW_TYPE_BIN,
                         G_IMPLEMENT_INTERFACE (KASASA_TYPE_CONTENT,
                                                kasasa_screenshot_content_interface_init))

GFile *
kasasa_screenshot_get_file (KasasaScreenshot *self)
{
  g_return_val_if_fail (KASASA_IS_SCREENSHOT (self), NULL);
  return self->file;
}

static gboolean
has_different_scalings (gdouble *max_scale)
{
  GdkDisplay *display = NULL;
  GListModel *monitors = NULL;
  GObject *monitor = NULL;
  gdouble min_s, max_s, current_scale;
  guint n_items;

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Can't check for different scalings");
      return FALSE;
    }

  monitors = gdk_display_get_monitors (display);
  n_items = g_list_model_get_n_items (monitors);
  g_info ("Number of monitors: %d", n_items);

  if (n_items == 0)
    {
      g_info ("Detected only 1 monitor, there's no different scales");
      return FALSE;
    }

  monitor = g_list_model_get_object (monitors, 0);
  min_s = max_s = gdk_monitor_get_scale (GDK_MONITOR (monitor));
  g_object_unref (monitor);

  for (guint i = 1; i < n_items; i++)
    {
      monitor = g_list_model_get_object (monitors, i);
      current_scale = gdk_monitor_get_scale (GDK_MONITOR (monitor));

      min_s = MIN (current_scale, min_s);
      max_s = MAX (current_scale, max_s);

      g_object_unref (monitor);
    }

  if (min_s != max_s)
    {
      g_info ("Monitors have different scales: %.2f and %.2f [min, max]",
               min_s, max_s);
      *max_scale = max_s;
      return TRUE;
    }
  else
    {
      g_info ("Monitors have same scales");
      return FALSE;
    }
}

static gboolean
scaling (KasasaScreenshot *self,
         gdouble          *scale)
{
  GdkDisplay *display = NULL;
  GtkNative *native = NULL;
  GdkSurface *surface = NULL;

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay");
      return TRUE;
    }

  native = gtk_widget_get_native (GTK_WIDGET (self));
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface");
      return TRUE;
    }

  *scale = gdk_surface_get_scale (surface);

  return FALSE;
}

static gboolean
monitor_size (KasasaScreenshot *self,
              gdouble          *monitor_width,
              gdouble          *monitor_height)
{
  GdkDisplay *display = NULL;
  GtkNative *native = NULL;
  GdkSurface *surface = NULL;
  GdkMonitor *monitor = NULL;
  GdkRectangle monitor_geometry;
  gdouble hidpi_scale;

  if (scaling (self, &hidpi_scale))
    {
      g_warning ("Couldn't get scaling");
      return TRUE;
    }

  display = gdk_display_get_default ();
  if (display == NULL)
    {
      g_warning ("Couldn't get GdkDisplay");
      return TRUE;
    }

  native = gtk_widget_get_native (GTK_WIDGET (self));
  if (native == NULL)
    {
      g_warning ("Couldn't get GtkNative");
      return TRUE;
    }

  surface = gtk_native_get_surface (native);
  if (surface == NULL)
    {
      g_warning ("Couldn't get GdkSurface");
      return TRUE;
    }

  monitor = gdk_display_get_monitor_at_surface (display, surface);
  if (monitor == NULL)
    {
      g_warning ("Couldn't get GdkMonitor");
      return TRUE;
    }

  gdk_monitor_get_geometry (monitor, &monitor_geometry);

  *monitor_width = monitor_geometry.width * hidpi_scale;
  *monitor_height = monitor_geometry.height * hidpi_scale;

  return FALSE;
}

// Compute the window size
// Based on:
// https://gitlab.gnome.org/GNOME/Incubator/showtime/-/blob/main/showtime/window.py?ref_type=heads#L836
// https://gitlab.gnome.org/GNOME/loupe/-/blob/4ca5f9e03d18667db5d72325597cebc02887777a/src/widgets/image/rendering.rs#L151
static gboolean
compute_size (KasasaScreenshot *self)
{
  g_autoptr (GError) error = NULL;
  // gints
  gint image_width, image_height, image_area, max_width, max_height;
  // gdoubles
  gdouble monitor_width, monitor_height, monitor_area,
          occupy_area_factor, size_scale, target_scale, hidpi_scale, max_scale;

  g_autoptr (GSettings) settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  if (!(self->image_height > 0 && self->image_width))
    {
      g_warning ("Image width or height must be > 0");
      return TRUE;
    }

  if (monitor_size (self, &monitor_width, &monitor_height))
    {
      g_warning ("Couldn't get monitor size");
      return TRUE;
    }

  if (scaling (self, &hidpi_scale))
    {
      g_warning ("Couldn't get HiDPI scale");
      return TRUE;
    }

  // If the user has different scales for the monitors and the current scale is
  // less than the max scale, divide the image dimentions by the max scale. This
  // is needed because the screenshot size follows the max scale
  if (has_different_scalings (&max_scale))
    {
      image_width = self->image_width / max_scale;
      image_height = self->image_height / max_scale;
    }
  else
    {
      image_width = self->image_width / hidpi_scale;
      image_height = self->image_height / hidpi_scale;
    }

  // AREAS
  monitor_area = monitor_width * monitor_height;
  image_area = image_height * image_width;

  occupy_area_factor = g_settings_get_double (settings, "occupy-screen");

  // factor for width and height that will achieve the desired area
  // occupation derived from:
  // monitor_area * occupy_area_factor ==
  //   (image_width * size_scale) * (image_height * size_scale)
  size_scale = sqrt (monitor_area / image_area * occupy_area_factor);
  g_debug ("size_scale @ %d: %f", __LINE__, size_scale);
  // ensure that size_scale is not ~ 0 (if image is too big, size_scale can reach 0)
  size_scale = (size_scale < MIN_OCCUPY_SCREEN) ? 0.1 : size_scale;
  g_debug ("size_scale @ %d: %f", __LINE__, size_scale);
  // ensure that we never increase image size
  target_scale = MIN (1, size_scale);
  g_debug ("target_scale @ %d: %f", __LINE__, target_scale);
  self->nat_width = image_width * target_scale;
  self->nat_height = image_height * target_scale;
  g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
           __LINE__, self->nat_width, self->nat_height);

  // Scale down if targeted occupation does not fit horizontally
  // Add some margin to not touch corners
  max_width = monitor_width - 20;
  if (self->nat_width > max_width)
    {
      self->nat_width = max_width;
      self->nat_height = image_height * self->nat_width / image_width;
      g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
               __LINE__, self->nat_width, self->nat_height);
    }

  // Same for vertical size
  // Additionally substract some space for HeaderBar and Shell bar
  max_height = monitor_height - (50 + 35 + 20) * hidpi_scale;
  if (self->nat_height > max_height)
    {
      self->nat_height = max_height;
      self->nat_width = image_width * self->nat_height / image_height;
      g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
               __LINE__, self->nat_width, self->nat_height);
    }

  self->nat_width = round (self->nat_width);
  self->nat_height = round (self->nat_height);
  g_debug ("[nat_width, nat_height] @ %d: [%f, %f]",
           __LINE__, self->nat_width, self->nat_height);

  // Ensure that the scaled image isn't smaller than the min window size
  self->nat_width = MAX (WINDOW_MIN_WIDTH, self->nat_width);
  self->nat_height = MAX (WINDOW_MIN_HEIGHT, self->nat_height);

  // If the header bar is NOT hiding, then the window height must have more 47 px
  if (!g_settings_get_boolean (settings, "auto-hide-menu"))
    self->nat_height += 47;

  g_info ("Physical monitor dimensions: %.2f x %.2f",
          monitor_width, monitor_height);
  g_info ("HiDPI scale: %.2f", hidpi_scale);
  g_info ("Image dimensions: %d x %d", image_width, image_height);
  g_info ("Scaled image dimensions: %.2f x %.2f", self->nat_width, self->nat_height);

  return FALSE;
}

static void
kasasa_screenshot_get_dimensions (KasasaContent *content,
                                  gint          *height,
                                  gint          *width)
{
  KasasaScreenshot *self = NULL;

  g_return_if_fail (KASASA_IS_SCREENSHOT (content));

  self = KASASA_SCREENSHOT (content);
  *height = -1;
  *width = -1;

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
          g_debug ("Trashed %s", parent_path);
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

static void
kasasa_screenshot_finish (KasasaContent *content)
{
  KasasaWindow *window = NULL;
  g_autofree gchar *base_name = NULL;
  KasasaScreenshot *self = NULL;

  g_return_if_fail (KASASA_IS_SCREENSHOT (content));

  self = KASASA_SCREENSHOT (content);

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
    kasasa_screenshot_finish (KASASA_CONTENT (self));

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
  kasasa_content_get_dimensions (KASASA_CONTENT (self), &height, &width);
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
kasasa_screenshot_content_interface_init (KasasaContentInterface *iface)
{
  iface->get_dimensions = kasasa_screenshot_get_dimensions;
  iface->finish = kasasa_screenshot_finish;
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

