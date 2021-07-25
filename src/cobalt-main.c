/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "cobalt-alert.h"
#include "cobalt-config.h"
#include "cobalt-host.h"
#include "cobalt-launcher.h"

#include <gtk/gtk.h>
#include <string.h>
#include <unistd.h>

#define COBALT_EXPOSE_PIDS_ALERT_ERROR_TITLE "Fatal Error"
#define COBALT_EXPOSE_PIDS_ALERT_WARNING_TITLE "Warning"

#define COBALT_STAMP_FIRST_RUN "run"
// Note that the name is "mimic" for legacy reasons, to work with the existing
// stamp files all the Chrome-based Flatpaks use.
#define COBALT_STAMP_EXPOSE_PIDS "mimic"

#define COBALT_RESOURCE_EXPOSE_PIDS_ERROR "/cobalt/expose-pids-error.xml"
#define COBALT_RESOURCE_EXPOSE_PIDS_WARNING "/cobalt/expose-pids-warning.xml"
#define COBALT_RESOURCE_EXPOSE_PIDS_GUIDE "/cobalt/expose-pids-guide.xml"

static char *DEFAULT_ENABLED_FEATURES[] = {NULL};

static char *DEFAULT_DISABLED_FEATURES[] = {
    "WebAssemblyTrapHandler",
    NULL,
};

static char *infer_application_name(CobaltHost *host, GError **error) {
  const char *app_id = cobalt_host_get_app_id(host, error);
  if (app_id == NULL) {
    g_prefix_error(error, "Failed to find app ID");
    return NULL;
  }

  const char *last_dot = strrchr(app_id, '.');
  if (last_dot == NULL || *(last_dot + 1) == '\0') {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid application ID: %s",
                app_id);
  }

  g_autofree char *name = g_strdup(last_dot + 1);
  for (char *s = name; *s != '\0'; s++) {
    *s = g_ascii_tolower(*s);
  }

  return g_steal_pointer(&name);
}

static char *infer_entry_point(const char *name, GError **error) {
  g_autofree char *path = g_build_filename("/app", name, name, NULL);
  if (access(path, F_OK | X_OK) != -1) {
    return g_steal_pointer(&path);
  }

  g_autofree char *extra_path = g_build_filename("/app", "extra", name, NULL);
  if (access(extra_path, F_OK | X_OK) != -1) {
    return g_steal_pointer(&extra_path);
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
              "Could not locate default entry point (looked for %s and %s)", path,
              extra_path);
  return NULL;
}

static char *infer_wrapper_script(CobaltHost *host, GError **error) {
  const char *exec = cobalt_host_get_app_exec(host, error);
  if (exec == NULL) {
    g_prefix_error(error, "Failed to get Exec= value: ");
    return NULL;
  }

  g_debug("Exec= line is: %s", exec);

  g_auto(GStrv) argv = NULL;
  if (!g_shell_parse_argv(exec, NULL, &argv, error)) {
    g_prefix_error(error, "Parsing Exec= value: ");
    return NULL;
  }

  g_warn_if_fail(argv[0] != NULL);
  return g_strdup(argv[0]);
}

static char *infer_sandbox_filename(const char *name, const char *entry_point,
                                    GError **error) {
  g_autofree char *entry_point_dir = g_path_get_dirname(entry_point);

  const char *chrome_sandbox_filename = "chrome-sandbox";
  g_autofree char *chrome_sandbox =
      g_build_filename(entry_point_dir, chrome_sandbox_filename, NULL);
  if (access(chrome_sandbox, F_OK | X_OK) != -1) {
    return g_strdup(chrome_sandbox_filename);
  }

  g_autofree char *named_sandbox_filename = g_strdup_printf("%s-sandbox", name);
  g_autofree char *named_sandbox =
      g_build_filename(entry_point_dir, named_sandbox_filename, NULL);
  if (access(named_sandbox, F_OK | X_OK) != -1) {
    return g_steal_pointer(&named_sandbox_filename);
  }

  g_autofree char *entry_point_filename = g_path_get_basename(entry_point);
  g_autofree char *entry_point_sandbox_filename =
      g_strdup_printf("%s-sandbox", entry_point_filename);
  g_autofree char *entry_point_sandbox =
      g_build_filename(entry_point_dir, entry_point_sandbox_filename, NULL);
  if (access(entry_point_sandbox, F_OK | X_OK) != -1) {
    return g_steal_pointer(&entry_point_sandbox_filename);
  }

  g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
              "Could not locate sandbox file (looked for '%s', '%s', and '%s')",
              chrome_sandbox, named_sandbox, entry_point_sandbox);
  return NULL;
}

static gboolean fill_defaults(CobaltConfig *config, CobaltHost *host, GError **error) {
  if (!config->application.name) {
    config->application.name = infer_application_name(host, error);
    if (!config->application.name) {
      g_prefix_error(error, "Failed to infer name: ");
      return FALSE;
    }

    g_debug("Inferred application name '%s'", config->application.name);
  }

  if (!config->application.entry_point) {
    config->application.entry_point = infer_entry_point(config->application.name, error);
    if (!config->application.entry_point) {
      g_prefix_error(error, "Failed to infer entry point: ");
      return FALSE;
    }

    g_debug("Inferred entry point '%s'", config->application.entry_point);
  }

  if (!config->application.wrapper_script) {
    config->application.wrapper_script = infer_wrapper_script(host, error);
    if (!config->application.wrapper_script) {
      g_prefix_error(error, "Failed to infer wrapper script: ");
      return FALSE;
    }
  }

  // Must be tested up here, since this value influences the default for
  // ExposePids.
  if (!config->zypak.enabled_was_set_by_user) {
    if (!cobalt_host_get_zypak_available(host, &config->zypak.enabled, error)) {
      g_prefix_error(error, "Failed to get Zypak status: ");
      return FALSE;
    }
  }

  if (!config->application.expose_pids) {
    if (config->zypak.enabled) {
      config->application.expose_pids = COBALT_CONFIG_EXPOSE_PIDS_RECOMMENDED;
      g_debug("Inferred ExposePids as 'recommended' because Zypak is being used");
    } else {
      config->application.expose_pids = COBALT_CONFIG_EXPOSE_PIDS_REQUIRED;
      g_debug("Inferred ExposePids as 'required' because Zypak is not being used");
    }
  }

  if (!config->flextop.enabled_was_set_by_user) {
    if (!cobalt_host_get_flextop_available(host, &config->flextop.enabled, error)) {
      g_prefix_error(error, "Failed to get Flextop status: ");
      return FALSE;
    }
  }

  if (config->zypak.enabled && !config->zypak.sandbox_filename) {
    config->zypak.sandbox_filename = infer_sandbox_filename(
        config->application.name, config->application.entry_point, error);
    if (!config->zypak.sandbox_filename) {
      g_prefix_error(error, "Failed to infer sandbox filename: ");
      return FALSE;
    }

    g_debug("Inferred sandbox filename '%s'", config->zypak.sandbox_filename);
  }

  return TRUE;
}

static GFile *get_stamp_file(CobaltConfig *config, const char *id) {
  const char *data_dir = g_get_user_data_dir();
  g_autofree char *stamp_filename =
      g_strdup_printf("flatpak-%s-%s-stamp", config->application.name, id);
  return g_file_new_build_filename(data_dir, stamp_filename, NULL);
}

static void touch_stamp_file(GFile *stamp_file) {
  g_autoptr(GError) local_error = NULL;
  if (!g_file_set_contents(g_file_peek_path(stamp_file), "", 0, &local_error)) {
    g_warning("Failed to touch stamp file '%s': %s", g_file_peek_path(stamp_file),
              local_error->message);
  }
}

static void show_expose_pids_alert(CobaltConfig *config) {
  CobaltAlert *alert = NULL;
  GtkWidget *no_remind = NULL;
  g_autoptr(GFile) stamp_file = NULL;

  switch (config->application.expose_pids) {
  case COBALT_CONFIG_EXPOSE_PIDS_OPTIONAL:
    g_warn_if_reached();
    return;
  case COBALT_CONFIG_EXPOSE_PIDS_RECOMMENDED:
    stamp_file = get_stamp_file(config, COBALT_STAMP_EXPOSE_PIDS);
    if (g_file_query_exists(stamp_file, NULL)) {
      return;
    }

    alert = cobalt_alert_new_from_resources(COBALT_EXPOSE_PIDS_ALERT_WARNING_TITLE,
                                            COBALT_RESOURCE_EXPOSE_PIDS_WARNING,
                                            COBALT_RESOURCE_EXPOSE_PIDS_GUIDE, NULL);

    no_remind = gtk_check_button_new_with_label("Don't show this again");
    gtk_widget_set_halign(no_remind, GTK_ALIGN_END);
    gtk_widget_set_margin_top(no_remind, 8);
    gtk_widget_set_margin_bottom(no_remind, 8);

    GtkWidget *alert_content = gtk_dialog_get_content_area(GTK_DIALOG(alert));
    gtk_container_add(GTK_CONTAINER(alert_content), no_remind);
    gtk_widget_show_all(alert_content);
    break;
  case COBALT_CONFIG_EXPOSE_PIDS_REQUIRED:
    alert = cobalt_alert_new_from_resources(COBALT_EXPOSE_PIDS_ALERT_ERROR_TITLE,
                                            COBALT_RESOURCE_EXPOSE_PIDS_ERROR,
                                            COBALT_RESOURCE_EXPOSE_PIDS_GUIDE, NULL);
    break;
  }

  gtk_dialog_run(GTK_DIALOG(alert));

  if (config->application.expose_pids == COBALT_CONFIG_EXPOSE_PIDS_RECOMMENDED) {
    g_warn_if_fail(no_remind != NULL);
    g_warn_if_fail(stamp_file != NULL);
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(no_remind))) {
      touch_stamp_file(stamp_file);
    }
  }
}

static void flextop_init() {
  g_autoptr(GError) error = NULL;

  g_autoptr(GSubprocess) process =
      g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error, "flextop-init", NULL);
  if (process == NULL || !g_subprocess_wait_check(process, NULL, &error)) {
    g_warning("Failed to run flextop-init: %s", error->message);
  }
}

static CobaltLauncher *setup_launcher(CobaltConfig *config, CobaltHost *host) {
  g_autoptr(GError) error = NULL;

  g_autoptr(CobaltLauncher) launcher = cobalt_launcher_new(
      host, config->application.entry_point, config->application.wrapper_script);
  if (config->zypak.enabled) {
    cobalt_launcher_zypak_enable(launcher);
    cobalt_launcher_zypak_set_sandbox_filename(launcher, config->zypak.sandbox_filename);
    if (config->zypak.expose_widevine) {
      g_warn_if_fail(config->application.config_dir);
      g_warn_if_fail(config->zypak.widevine_path);
      g_autofree char *widevine_path =
          g_build_filename(g_get_user_config_dir(), config->application.config_dir,
                           config->zypak.widevine_path, NULL);
      cobalt_launcher_zypak_expose_widevine_path(launcher, widevine_path);
    }
  }

  cobalt_launcher_set_features(launcher, DEFAULT_ENABLED_FEATURES,
                               COBALT_LAUNCHER_FEATURE_ENABLED);
  cobalt_launcher_set_features(launcher, DEFAULT_DISABLED_FEATURES,
                               COBALT_LAUNCHER_FEATURE_DISABLED);

  cobalt_launcher_set_features(launcher, config->default_features.enabled,
                               COBALT_LAUNCHER_FEATURE_ENABLED);
  cobalt_launcher_set_features(launcher, config->default_features.disabled,
                               COBALT_LAUNCHER_FEATURE_DISABLED);

  g_autofree char *flags_filename =
      g_strdup_printf("%s-flags.conf", config->application.name);
  g_autoptr(GFile) flags_file =
      g_file_new_build_filename(g_get_user_config_dir(), flags_filename, NULL);

  if (config->application.migrate_flags_file != NULL &&
      !g_file_query_exists(flags_file, NULL)) {
    g_autoptr(GFile) migrate_file = g_file_new_build_filename(
        g_get_user_config_dir(), config->application.migrate_flags_file, NULL);
    if (g_file_query_exists(migrate_file, NULL)) {
      g_autofree char *contents =
          g_strdup_printf("# Your flags have been migrated to '%s'.", flags_filename);

      if (!g_file_move(migrate_file, flags_file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL,
                       &error) ||
          !g_file_set_contents(g_file_peek_path(migrate_file), contents, -1, &error)) {
        g_warning("Failed to migrate '%s' to '%s' file: %s",
                  g_file_peek_path(migrate_file), g_file_peek_path(flags_file),
                  error->message);
        g_clear_error(&error);
      }
    }
  }

  if (!cobalt_launcher_read_flags_file(launcher, flags_file, &error)) {
    g_warning("Failed to read flags file '%s': %s", g_file_peek_path(flags_file),
              error->message);
    g_clear_error(&error);
  }

  return g_steal_pointer(&launcher);
}

int main(int argc, char **argv) {
  gtk_init(0, NULL);

  g_autoptr(GError) error = NULL;

  g_autoptr(CobaltConfig) config = cobalt_config_load(&error);
  if (config == NULL) {
    g_printerr("Failed to load config file: %s\n", error->message);
    return 1;
  }

  g_autoptr(CobaltHost) host = cobalt_host_new();

  if (!fill_defaults(config, host, &error)) {
    g_printerr("Failed to fill defaults: %s\n", error->message);
    return 1;
  }

  if (config->application.expose_pids != COBALT_CONFIG_EXPOSE_PIDS_OPTIONAL) {
    gboolean expose_pids_available = FALSE;
    if (!cobalt_host_get_expose_pids_available(host, &expose_pids_available, &error)) {
      g_printerr("Failed to get expose-pids state: %s\n", error->message);
      return 1;
    }

    if (!expose_pids_available) {
      show_expose_pids_alert(config);
      if (config->application.expose_pids == COBALT_CONFIG_EXPOSE_PIDS_REQUIRED) {
        return 1;
      }
    }
  }

  if (config->flextop.enabled) {
    flextop_init();
  }

  g_autoptr(CobaltLauncher) launcher = setup_launcher(config, host);

  if (config->application.first_run_urls && *config->application.first_run_urls) {
    g_autoptr(GFile) stamp_file = get_stamp_file(config, COBALT_STAMP_FIRST_RUN);
    if (!g_file_query_exists(stamp_file, NULL)) {
      cobalt_launcher_add_argv(launcher, config->application.first_run_urls);
      touch_stamp_file(stamp_file);
    }
  }

  cobalt_launcher_add_argv(launcher, argv + 1);

  cobalt_launcher_exec(launcher, &error);
  g_critical("Failed to exec: %s", error->message);
  return 1;
}
