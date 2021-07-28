/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "cobalt-launcher.h"

#include <errno.h>

#define FLAG_PREFIX "--"

#define ENABLE_FEATURES_FLAG_PREFIX "--enable-features="
#define DISABLE_FEATURES_FLAG_PREFIX "--disable-features="

#define ENABLE_FEATURES_FLAGFILE_PREFIX "features+="
#define DISABLE_FEATURES_FLAGFILE_PREFIX "features-="

struct CobaltLauncher {
  CobaltHost *host;

  char *entry_point;
  GPtrArray *args;

  char *wrapper_script;

  char *enable_features;
  char *disable_features;

  GHashTable *feature_statuses;

  gboolean use_zypak;
  char *sandbox_filename;
  char *expose_widevine_path;
};

CobaltLauncher *cobalt_launcher_new(CobaltHost *host, const char *entry_point,
                                    const char *wrapper_script) {
  CobaltLauncher *launcher = g_new0(CobaltLauncher, 1);
  launcher->host = host;
  launcher->entry_point = g_strdup(entry_point);
  launcher->args = g_ptr_array_new_with_free_func(g_free);
  launcher->wrapper_script = g_strdup(wrapper_script);
  launcher->feature_statuses =
      g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
  return launcher;
}

void cobalt_launcher_zypak_enable(CobaltLauncher *launcher) {
  launcher->use_zypak = TRUE;
}

void cobalt_launcher_zypak_set_sandbox_filename(CobaltLauncher *launcher,
                                                const char *sandbox_filename) {
  g_return_if_fail(launcher->use_zypak);

  g_clear_pointer(&launcher->sandbox_filename, g_free);
  launcher->sandbox_filename = g_strdup(sandbox_filename);
}

void cobalt_launcher_zypak_expose_widevine_path(CobaltLauncher *launcher,
                                                const char *widevine_path) {
  g_return_if_fail(launcher->use_zypak);

  g_clear_pointer(&launcher->expose_widevine_path, g_free);
  launcher->expose_widevine_path = g_strdup(widevine_path);
}

void cobalt_launcher_set_feature(CobaltLauncher *launcher, const char *feature,
                                 CobaltLauncherFeatureStatus status) {
  g_hash_table_replace(launcher->feature_statuses, g_strdup(feature),
                       GINT_TO_POINTER(status));
}

void cobalt_launcher_set_features(CobaltLauncher *launcher, char **features,
                                  CobaltLauncherFeatureStatus status) {
  for (; features && *features != NULL; features++) {
    cobalt_launcher_set_feature(launcher, *features, status);
  }
}

gboolean cobalt_launcher_read_flags_file(CobaltLauncher *launcher, GFile *file,
                                         GError **error) {
  g_autoptr(GError) local_error = NULL;

  g_autoptr(GFileInputStream) file_input_stream = g_file_read(file, NULL, &local_error);
  if (file_input_stream == NULL) {
    if (g_error_matches(local_error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND)) {
      g_debug("Flags file '%s' not found", g_file_peek_path(file));
      return TRUE;
    }

    g_propagate_error(error, g_steal_pointer(&local_error));
  }

  g_autoptr(GDataInputStream) data_input_stream =
      g_data_input_stream_new(G_INPUT_STREAM(file_input_stream));
  for (;;) {
    g_autofree char *line =
        g_data_input_stream_read_line_utf8(data_input_stream, NULL, NULL, &local_error);
    if (line == NULL) {
      if (local_error != NULL) {
        g_propagate_error(error, g_steal_pointer(&local_error));
        return FALSE;
      } else {
        // EOF.
        break;
      }
    }

    g_strstrip(line);

    if (*line == '\0' || g_str_has_prefix(line, "#")) {
      continue;
    }

    {
      char* token;
      char* rest = line;
      while ((token = strtok_r(rest, " ", &rest))) {
        if (g_str_has_prefix(token, ENABLE_FEATURES_FLAGFILE_PREFIX) ||
            g_str_has_prefix(token, DISABLE_FEATURES_FLAGFILE_PREFIX)) {
          const char *feature = strchr(token, '=') + 1;
          if (*feature == '\0') {
            g_warning("Argument in '%s' has an empty feature: %s", g_file_peek_path(file), token);
          }

          cobalt_launcher_set_feature(launcher, feature,
                                      g_str_has_prefix(token, ENABLE_FEATURES_FLAGFILE_PREFIX)
                                          ? COBALT_LAUNCHER_FEATURE_ENABLED
                                          : COBALT_LAUNCHER_FEATURE_DISABLED);
        } else if (!(g_str_has_prefix(token, FLAG_PREFIX) &&
                    token[strlen(FLAG_PREFIX)] != '\0')) {
          g_warning("Argument in '%s' is not a flag (must start with '--'): %s",
                    g_file_peek_path(file), token);
        } else {
          cobalt_launcher_add_arg(launcher, token);
        }
      }
    }
  }

  return TRUE;
}

void cobalt_launcher_add_arg(CobaltLauncher *launcher, const char *arg) {
  g_return_if_fail(arg != NULL);

  if (g_str_has_prefix(arg, ENABLE_FEATURES_FLAG_PREFIX)) {
    g_clear_pointer(&launcher->enable_features, g_free);
    launcher->enable_features = g_strdup(arg + strlen(ENABLE_FEATURES_FLAG_PREFIX));
  } else if (g_str_has_prefix(arg, DISABLE_FEATURES_FLAG_PREFIX)) {
    g_clear_pointer(&launcher->disable_features, g_free);
    launcher->disable_features = g_strdup(arg + strlen(DISABLE_FEATURES_FLAG_PREFIX));
  } else {
    g_ptr_array_add(launcher->args, g_strdup(arg));
  }
}

void cobalt_launcher_add_argv(CobaltLauncher *launcher, char **argv) {
  for (; argv && *argv; argv++) {
    cobalt_launcher_add_arg(launcher, *argv);
  }
}

static void set_features_from_flag_value(CobaltLauncher *launcher, const char *value,
                                         CobaltLauncherFeatureStatus status) {
  g_auto(GStrv) features = g_strsplit(value, ",", -1);
  for (char **feature = features; feature && *feature != NULL; feature++) {
    if (**feature == '\0') {
      continue;
    }

    cobalt_launcher_set_feature(launcher, *feature, status);
  }
}

static char *format_features_as_flag(CobaltLauncher *launcher,
                                     CobaltLauncherFeatureStatus status) {
  g_autoptr(GPtrArray) features = g_ptr_array_new();

  GHashTableIter iter;
  g_hash_table_iter_init(&iter, launcher->feature_statuses);

  gpointer key, value;
  while (g_hash_table_iter_next(&iter, &key, &value)) {
    const char *feature = key;
    CobaltLauncherFeatureStatus feature_status = GPOINTER_TO_INT(value);

    if (status == feature_status) {
      g_ptr_array_add(features, (gpointer)feature);
    }
  }

  if (features->len == 0) {
    return NULL;
  }

  g_ptr_array_add(features, NULL);
  g_autofree char *arg_value = g_strjoinv(",", (char **)features->pdata);

  switch (status) {
  case COBALT_LAUNCHER_FEATURE_DISABLED:
    return g_strdup_printf("--disable-features=%s", arg_value);
  case COBALT_LAUNCHER_FEATURE_ENABLED:
    return g_strdup_printf("--enable-features=%s", arg_value);
  default:
    g_warn_if_reached();
    return NULL;
  }
}

static GPtrArray *launcher_build_argv(CobaltLauncher *launcher) {
  g_autoptr(GPtrArray) argv = g_ptr_array_new_with_free_func(g_free);
  if (launcher->use_zypak) {
    g_ptr_array_add(argv, g_strdup("zypak-wrapper.sh"));
  }
  g_ptr_array_add(argv, g_steal_pointer(&launcher->entry_point));

  g_autofree char *enabled_features_flag =
      format_features_as_flag(launcher, COBALT_LAUNCHER_FEATURE_ENABLED);
  if (enabled_features_flag != NULL) {
    g_ptr_array_add(argv, g_steal_pointer(&enabled_features_flag));
  }

  g_autofree char *disabled_features_flag =
      format_features_as_flag(launcher, COBALT_LAUNCHER_FEATURE_DISABLED);
  if (disabled_features_flag != NULL) {
    g_ptr_array_add(argv, g_steal_pointer(&disabled_features_flag));
  }

  for (gsize i = 0; i < launcher->args->len; i++) {
    g_ptr_array_add(argv, g_steal_pointer(&g_ptr_array_index(launcher->args, i)));
  }

  g_ptr_array_add(argv, NULL);
  return g_steal_pointer(&argv);
}

static void launcher_setenv(const char *variable, const char *value) {
  g_debug("setenv: %s=%s", variable, value);
  g_setenv(variable, value, TRUE);
}

static gboolean launcher_update_environment(CobaltLauncher *launcher, GError **error) {
  const char *app_id = cobalt_host_get_app_id(launcher->host, error);
  if (app_id == NULL) {
    g_prefix_error(error, "Failed to get app ID: ");
    return FALSE;
  }

  g_autofree char *new_tmpdir =
      g_build_filename(g_get_user_runtime_dir(), "app", app_id, NULL);
  launcher_setenv("TMPDIR", new_tmpdir);

  launcher_setenv("CHROME_WRAPPER", launcher->wrapper_script);

  if (launcher->sandbox_filename != NULL) {
    launcher_setenv("ZYPAK_SANDBOX_FILENAME", launcher->sandbox_filename);
  }
  if (launcher->expose_widevine_path != NULL) {
    launcher_setenv("ZYPAK_EXPOSE_WIDEVINE_PATH", launcher->expose_widevine_path);
  }

  return TRUE;
}

void cobalt_launcher_exec(CobaltLauncher *launcher, GError **error) {
  if (launcher->enable_features) {
    set_features_from_flag_value(launcher, launcher->enable_features,
                                 COBALT_LAUNCHER_FEATURE_ENABLED);
  }

  if (launcher->disable_features) {
    set_features_from_flag_value(launcher, launcher->disable_features,
                                 COBALT_LAUNCHER_FEATURE_DISABLED);
  }

  if (!launcher_update_environment(launcher, error)) {
    return;
  }

  g_autoptr(GPtrArray) argv = launcher_build_argv(launcher);
  for (int i = 0; i < argv->len - 1; i++) {
    g_debug("Arg: '%s'", (char *)g_ptr_array_index(argv, i));
  }

  execvp(g_ptr_array_index(argv, 0), (char *const *)argv->pdata);

  int saved_errno = errno;
  g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno), "Failed to exec: %s",
              g_strerror(saved_errno));
}

void cobalt_launcher_free(CobaltLauncher *launcher) {
  g_clear_pointer(&launcher->entry_point, g_free);
  g_clear_pointer(&launcher->args, g_ptr_array_unref);  // NOLINT

  g_clear_pointer(&launcher->wrapper_script, g_free);

  g_clear_pointer(&launcher->enable_features, g_free);
  g_clear_pointer(&launcher->disable_features, g_free);

  g_clear_pointer(&launcher->feature_statuses, g_hash_table_unref);  // NOLINT

  g_clear_pointer(&launcher->sandbox_filename, g_free);
  g_clear_pointer(&launcher->expose_widevine_path, g_free);
}
