/* kasasa-application.c
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

#include <glib/gi18n.h>

#include "kasasa-preferences.h"
#include "kasasa-application.h"
#include "kasasa-window.h"

struct _KasasaApplication
{
  AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE (KasasaApplication, kasasa_application, ADW_TYPE_APPLICATION)

KasasaApplication *
kasasa_application_new (const char        *application_id,
                        GApplicationFlags  flags)
{
  g_return_val_if_fail (application_id != NULL, NULL);

  return g_object_new (KASASA_TYPE_APPLICATION,
                       "application-id", application_id,
                       "flags", flags,
                       NULL);
}

static void
kasasa_application_activate (GApplication *app)
{
  GtkWindow *window;

  g_assert (KASASA_IS_APPLICATION (app));

  window = gtk_application_get_active_window (GTK_APPLICATION (app));

  if (window == NULL)
    window = g_object_new (KASASA_TYPE_WINDOW,
                           "application", app,
                           NULL);

  gtk_window_present (GTK_WINDOW (window));
  // The window will be set to 'visible = TRUE' after the screenshot is taken
  gtk_widget_set_visible (GTK_WIDGET (window), FALSE);
}

static void
kasasa_application_preferences_action (GSimpleAction *action,
                                       GVariant      *parameter,
                                       gpointer       app)
{
  KasasaPreferences *preferences;
  GtkWindow *window;

  window = gtk_application_get_active_window (GTK_APPLICATION (app));
  preferences = kasasa_preferences_new ();
  adw_dialog_present (ADW_DIALOG (preferences), GTK_WIDGET (window));
}

static void
kasasa_application_class_init (KasasaApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS (klass);

  app_class->activate = kasasa_application_activate;
}

static void
kasasa_application_about_action (GSimpleAction *action,
                                 GVariant      *parameter,
                                 gpointer       user_data)
{
  static const char *developers[] = {"Kelvin Ribeiro Novais", NULL};
  KasasaApplication *self = user_data;
  GtkWindow *window = NULL;

  g_assert (KASASA_IS_APPLICATION (self));

  window = gtk_application_get_active_window (GTK_APPLICATION (self));

  adw_show_about_dialog (GTK_WIDGET (window),
                         "application-name", _("Kasasa"),
                         "application-icon", "io.github.kelvinnovais.Kasasa",
                         "developer-name", "Kelvin Ribeiro Novais",
                         "version", "1.1.3",
                         "comments", _("Snip and pin useful information"
                                       "\n\nIf you liked the app ❤️, consider giving it a star ⭐:"),
                         "issue-url", "https://github.com/KelvinNovais/Kasasa/issues",
                         "website", "https://github.com/KelvinNovais/Kasasa",
                         "developers", developers,
                         "copyright", "© 2024-2025 Kelvin Ribeiro Novais",
                         "license-type", GTK_LICENSE_GPL_3_0,
                         // Translators: Replace "translator-credits" with your names, one name per line
                         "translator_credits", _("translator-credits"),
                         NULL);
}

static void
kasasa_application_quit_action (GSimpleAction *action,
                                GVariant      *parameter,
                                gpointer       user_data)
{
  KasasaApplication *self = user_data;

  g_assert (KASASA_IS_APPLICATION (self));

  g_application_quit (G_APPLICATION (self));
}

static const GActionEntry app_actions[] = {
  { "quit", kasasa_application_quit_action },
  { "about", kasasa_application_about_action },
  { "preferences", kasasa_application_preferences_action }
};

static void
kasasa_application_init (KasasaApplication *self)
{
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   app_actions,
                                   G_N_ELEMENTS (app_actions),
                                   self);
  gtk_application_set_accels_for_action (GTK_APPLICATION (self),
                                         "app.quit",
                                         (const char *[]) { "<primary>q", NULL });
}

