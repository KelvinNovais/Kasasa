/* kasasa-preferences.c
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

#include "kasasa-preferences.h"

struct _KasasaPreferences
{
  AdwPreferencesDialog   parent_instance;

  /* Template widgets */
  GtkWidget             *opacity_expander_row;
  GtkWidget             *opacity_adjustment;

  GtkWidget             *miniaturize_switch;

  GtkWidget             *auto_hide_menu_switch;

  GtkWidget             *occupy_screen_adjustment;

  GtkWidget             *auto_discard_window_switch;
  GtkWidget             *auto_discard_window_adjustment;

  GtkWidget             *auto_trash_image_switch;

  /* Instance variables */
  GSettings             *settings;
};

G_DEFINE_FINAL_TYPE (KasasaPreferences, kasasa_preferences, ADW_TYPE_PREFERENCES_DIALOG)

static void
on_opacity_expander_row_changed (GObject    *object,
                                 GParamSpec *pspec,
                                 gpointer    user_data)
{
  KasasaPreferences *self = KASASA_PREFERENCES (user_data);
  gboolean decreasing_opacity =
    adw_expander_row_get_expanded (ADW_EXPANDER_ROW (self->opacity_expander_row));

  // Never allow both hiding modes together
  gtk_widget_set_sensitive (self->miniaturize_switch, !decreasing_opacity);

  g_settings_set_boolean (self->settings, "change-opacity", decreasing_opacity);
}

static void
on_miniaturize_switch_changed (GObject    *object,
                               GParamSpec *pspec,
                               gpointer    user_data)
{
  KasasaPreferences *self = KASASA_PREFERENCES (user_data);
  gboolean miniaturize =
    adw_switch_row_get_active (ADW_SWITCH_ROW (self->miniaturize_switch));

  // Never allow both hiding modes together
  gtk_widget_set_sensitive (self->opacity_expander_row, !miniaturize);

  g_settings_set_boolean (self->settings, "miniaturize-window", miniaturize);
}

static void
kasasa_preferences_dispose (GObject *kasasa_preferences)
{
  KasasaPreferences *self = KASASA_PREFERENCES (kasasa_preferences);

  g_clear_object (&self->settings);

  gtk_widget_dispose_template (GTK_WIDGET (kasasa_preferences), KASASA_TYPE_PREFERENCES);

  G_OBJECT_CLASS (kasasa_preferences_parent_class)->dispose (kasasa_preferences);
}

static void
kasasa_preferences_class_init (KasasaPreferencesClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = kasasa_preferences_dispose;

  gtk_widget_class_set_template_from_resource (widget_class, "/io/github/kelvinnovais/Kasasa/kasasa-preferences.ui");

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, opacity_expander_row);
  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, opacity_adjustment);

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, miniaturize_switch);

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, auto_hide_menu_switch);

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, occupy_screen_adjustment);

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, auto_discard_window_switch);
  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, auto_discard_window_adjustment);

  gtk_widget_class_bind_template_child (widget_class, KasasaPreferences, auto_trash_image_switch);
}


static void
kasasa_preferences_init (KasasaPreferences *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->settings = g_settings_new ("io.github.kelvinnovais.Kasasa");

  // BIND SETTINGS
  // Opacity
  g_settings_bind (self->settings, "opacity",
                   self->opacity_adjustment, "value",
                   G_SETTINGS_BIND_DEFAULT);
  // Auto hide
  g_settings_bind (self->settings, "auto-hide-menu",
                   self->auto_hide_menu_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);

  // Ocuppy screen
  g_settings_bind (self->settings, "occupy-screen",
                   self->occupy_screen_adjustment, "value",
                   G_SETTINGS_BIND_DEFAULT);

  // Auto discard window
  g_settings_bind (self->settings, "auto-discard-window",
                   self->auto_discard_window_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);
  g_settings_bind (self->settings, "auto-discard-window-time",
                   self->auto_discard_window_adjustment, "value",
                   G_SETTINGS_BIND_DEFAULT);

  // Auto trash image
  g_settings_bind (self->settings, "auto-trash-image",
                   self->auto_trash_image_switch, "active",
                   G_SETTINGS_BIND_DEFAULT);

  // MANUAL "BIDING"
  if (g_settings_get_boolean (self->settings, "change-opacity"))
    {
      gtk_widget_set_sensitive (self->miniaturize_switch, FALSE);
      adw_expander_row_set_enable_expansion (ADW_EXPANDER_ROW (self->opacity_expander_row),
                                             TRUE);
    }
  else if (g_settings_get_boolean (self->settings, "miniaturize-window"))
    {
      gtk_widget_set_sensitive (self->opacity_expander_row, FALSE);
      adw_switch_row_set_active (ADW_SWITCH_ROW (self->miniaturize_switch), TRUE);
    }

  // Signals
  g_signal_connect (self->opacity_expander_row, "notify::expanded",
                    G_CALLBACK (on_opacity_expander_row_changed),
                    self);

  g_signal_connect (self->miniaturize_switch, "notify::active",
                    G_CALLBACK (on_miniaturize_switch_changed),
                    self);
}

KasasaPreferences *
kasasa_preferences_new (void)
{
  return  g_object_new (KASASA_TYPE_PREFERENCES, NULL);
}
