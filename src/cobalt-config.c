/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "cobalt-config.h"

#define CONFIG_OVERRIDE_ENV "COBALT_CONFIG_OVERRIDE"
#define CONFIG_FILE_PATH "/app/etc/cobalt.ini"

#define CONFIG_APPLICATION "Application"
#define CONFIG_APPLICATION_NAME "Name"
#define CONFIG_APPLICATION_ENTRY_POINT "EntryPoint"
#define CONFIG_APPLICATION_WRAPPER_SCRIPT "WrapperScript"
#define CONFIG_APPLICATION_EXPOSE_PIDS "ExposePids"
#define CONFIG_APPLICATION_CONFIG_DIR "ConfigDir"
#define CONFIG_APPLICATION_FIRST_RUN_URLS "FirstRunUrls"
#define CONFIG_APPLICATION_MIGRATE_FLAGS_FILE "MigrateFlagsFile"

#define CONFIG_ZYPAK "Zypak"
#define CONFIG_ZYPAK_ENABLED "Enabled"
#define CONFIG_ZYPAK_SANDBOX_FILENAME "SandboxFilename"
#define CONFIG_ZYPAK_EXPOSE_WIDEVINE "ExposeWidevine"
#define CONFIG_ZYPAK_WIDEVINE_PATH "WidevinePath"

#define CONFIG_FLEXTOP "Flextop"
#define CONFIG_FLEXTOP_ENABLED "Enabled"

#define CONFIG_DEFAULT_FEATURES "DefaultFeatures"
#define CONFIG_DEFAULT_FEATURES_ENABLED "Enabled"
#define CONFIG_DEFAULT_FEATURES_DISABLED "Disabled"

#define CONFIG_ZYPAK_WIDEVINE_PATH_DEFAULT "WidevineCdm"
#define CONFIG_ZYPAK_MIMIC_STRATEGY_ACTION_DEFAULT COBALT_CONFIG_MIMIC_STRATEGY_WARN

static gboolean read_boolean(GKeyFile *key_file, const char *group, const char *key,
                             gboolean *out, gboolean *was_set, GError **error) {
  g_autoptr(GError) local_error = NULL;
  gboolean value = g_key_file_get_boolean(key_file, group, key, &local_error);
  if (local_error) {
    if (g_error_matches(local_error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND) ||
        g_error_matches(local_error, G_KEY_FILE_ERROR,
                        G_KEY_FILE_ERROR_GROUP_NOT_FOUND)) {
      g_clear_error(&local_error);
      if (was_set) {
        *was_set = FALSE;
      }
      return TRUE;
    } else {
      g_propagate_error(error, g_steal_pointer(&local_error));
      return FALSE;
    }
  }

  *out = value;
  if (was_set) {
    *was_set = TRUE;
  }
  return TRUE;
}

static CobaltConfigExposePids parse_expose_pids(const char *string, GError **error) {
  if (g_str_equal(string, "required")) {
    return COBALT_CONFIG_EXPOSE_PIDS_REQUIRED;
  } else if (g_str_equal(string, "recommended")) {
    return COBALT_CONFIG_EXPOSE_PIDS_RECOMMENDED;
  } else if (g_str_equal(string, "optional")) {
    return COBALT_CONFIG_EXPOSE_PIDS_OPTIONAL;
  } else {
    g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_INVALID_VALUE,
                "Value '%s' for '" CONFIG_APPLICATION_EXPOSE_PIDS "' is not valid",
                string);
    return 0;
  }
}

CobaltConfig *cobalt_config_load(GError **error) {
  g_autoptr(CobaltConfig) config = g_new0(CobaltConfig, 1);
  g_autoptr(GKeyFile) key_file = g_key_file_new();
  g_autoptr(GError) local_error = NULL;

  const char *path = g_getenv(CONFIG_OVERRIDE_ENV);
  if (path == NULL) {
    path = CONFIG_FILE_PATH;
  }

  g_debug("Loading config file '%s'", path);

  if (!g_key_file_load_from_file(key_file, path, G_KEY_FILE_NONE, &local_error)) {
    if (g_error_matches(local_error, G_FILE_ERROR, G_FILE_ERROR_NOENT)) {
      g_debug("Config file '%s' is missing, treating as empty", path);
      g_clear_error(&local_error);
    } else {
      g_propagate_error(error, g_steal_pointer(&local_error));
      local_error = NULL;
      return NULL;
    }
  }

  config->application.name =
      g_key_file_get_string(key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_NAME, NULL);
  config->application.entry_point = g_key_file_get_string(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_ENTRY_POINT, NULL);
  config->application.wrapper_script = g_key_file_get_string(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_WRAPPER_SCRIPT, NULL);
  config->application.config_dir = g_key_file_get_string(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_CONFIG_DIR, NULL);
  config->application.first_run_urls = g_key_file_get_string_list(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_FIRST_RUN_URLS, NULL, NULL);
  config->application.migrate_flags_file = g_key_file_get_string(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_MIGRATE_FLAGS_FILE, NULL);

  const char *expose_pids_string = g_key_file_get_string(
      key_file, CONFIG_APPLICATION, CONFIG_APPLICATION_EXPOSE_PIDS, NULL);
  if (expose_pids_string != NULL) {
    config->application.expose_pids = parse_expose_pids(expose_pids_string, &local_error);
    if (local_error) {
      g_propagate_error(error, g_steal_pointer(&local_error));
      return NULL;
    }
  }

  if (!read_boolean(key_file, CONFIG_ZYPAK, CONFIG_ZYPAK_ENABLED, &config->zypak.enabled,
                    &config->zypak.enabled_was_set_by_user, error)) {
    return FALSE;
  }

  config->zypak.sandbox_filename =
      g_key_file_get_string(key_file, CONFIG_ZYPAK, CONFIG_ZYPAK_SANDBOX_FILENAME, NULL);

  config->zypak.expose_widevine = FALSE;
  if (!read_boolean(key_file, CONFIG_ZYPAK, CONFIG_ZYPAK_EXPOSE_WIDEVINE,
                    &config->zypak.expose_widevine, NULL, error)) {
    return FALSE;
  }

  if (config->zypak.expose_widevine && !config->application.config_dir) {
    g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND,
                CONFIG_APPLICATION_CONFIG_DIR
                " must be set if " CONFIG_ZYPAK_EXPOSE_WIDEVINE " is enabled");
    return NULL;
  }

  config->zypak.widevine_path =
      g_key_file_get_string(key_file, CONFIG_ZYPAK, CONFIG_ZYPAK_WIDEVINE_PATH, NULL);
  if (config->zypak.widevine_path == NULL) {
    config->zypak.widevine_path = g_strdup(CONFIG_ZYPAK_WIDEVINE_PATH_DEFAULT);
  }

  if (!read_boolean(key_file, CONFIG_FLEXTOP, CONFIG_FLEXTOP_ENABLED,
                    &config->flextop.enabled, &config->flextop.enabled_was_set_by_user,
                    error)) {
    return FALSE;
  }

  config->default_features.enabled = g_key_file_get_string_list(
      key_file, CONFIG_DEFAULT_FEATURES, CONFIG_DEFAULT_FEATURES_ENABLED, NULL, NULL);
  config->default_features.disabled = g_key_file_get_string_list(
      key_file, CONFIG_DEFAULT_FEATURES, CONFIG_DEFAULT_FEATURES_DISABLED, NULL, NULL);

  return g_steal_pointer(&config);
}

void cobalt_config_free(CobaltConfig *config) {
  g_clear_pointer(&config->application.name, g_free);
  g_clear_pointer(&config->application.entry_point, g_free);
  g_clear_pointer(&config->application.wrapper_script, g_free);
  g_clear_pointer(&config->application.config_dir, g_free);
  g_clear_pointer(&config->application.first_run_urls, g_strfreev);
  g_clear_pointer(&config->application.migrate_flags_file, g_free);
  g_clear_pointer(&config->zypak.sandbox_filename, g_free);
  g_clear_pointer(&config->zypak.widevine_path, g_free);
  g_clear_pointer(&config->default_features.enabled, g_strfreev);
  g_clear_pointer(&config->default_features.disabled, g_strfreev);

  g_free(config);
}
