/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <glib.h>

typedef struct CobaltConfig CobaltConfig;

typedef enum CobaltConfigZypakStatus CobaltConfigZypakStatus;
typedef enum CobaltConfigExposePids CobaltConfigExposePids;

enum CobaltConfigExposePids {
  COBALT_CONFIG_EXPOSE_PIDS_REQUIRED = 1,
  COBALT_CONFIG_EXPOSE_PIDS_RECOMMENDED,
  COBALT_CONFIG_EXPOSE_PIDS_OPTIONAL,
};

struct CobaltConfig {
  struct {
    // Must be filled with defaults externally if not set.
    char *name;
    char *entry_point;
    char *wrapper_script;
    CobaltConfigExposePids expose_pids;

    // Must be set if widevine.expose_widevine is set.
    char *config_dir;

    // May safely be NULL.
    char **first_run_urls;
    char *migrate_flags_file;
  } application;

  struct {
    // Must be filled with defaults externally if not set.
    gboolean enabled;
    gboolean enabled_was_set_by_user;
    char *sandbox_filename;

    // Filled with defaults by the config parser.
    gboolean expose_widevine;
    // Filled with defaults by the config parser.
    char *widevine_path;
  } zypak;

  struct {
    // Must be filled with defaults externally if not set.
    gboolean enabled;
    gboolean enabled_was_set_by_user;
  } flextop;

  struct {
    GStrv enabled;
    GStrv disabled;
  } default_features;
};

CobaltConfig *cobalt_config_load(GError **error);
void cobalt_config_free(CobaltConfig *config);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CobaltConfig, cobalt_config_free)
