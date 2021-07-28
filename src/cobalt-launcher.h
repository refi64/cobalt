/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include "cobalt-host.h"

#include <strings.h>
#include <gio/gio.h>
#include <glib.h>

typedef enum CobaltLauncherFeatureStatus CobaltLauncherFeatureStatus;
typedef struct CobaltLauncher CobaltLauncher;

enum CobaltLauncherFeatureStatus {
  COBALT_LAUNCHER_FEATURE_DISABLED,
  COBALT_LAUNCHER_FEATURE_ENABLED,
};

CobaltLauncher *cobalt_launcher_new(CobaltHost *hsot, const char *entry_point,
                                    const char *wrapper_script);

void cobalt_launcher_zypak_enable(CobaltLauncher *launcher);
void cobalt_launcher_zypak_set_sandbox_filename(CobaltLauncher *launcher,
                                                const char *sandbox_filename);
void cobalt_launcher_zypak_expose_widevine_path(CobaltLauncher *launcher,
                                                const char *widevine_path);

void cobalt_launcher_set_feature(CobaltLauncher *launcher, const char *feature,
                                 CobaltLauncherFeatureStatus status);
void cobalt_launcher_set_features(CobaltLauncher *launcher, char **features,
                                  CobaltLauncherFeatureStatus status);

gboolean cobalt_launcher_read_flags_file(CobaltLauncher *launcher, GFile *file,
                                         GError **error);

void cobalt_launcher_add_arg(CobaltLauncher *launcher, const char *arg);
void cobalt_launcher_add_argv(CobaltLauncher *launcher, char **argv);

void cobalt_launcher_exec(CobaltLauncher *launcher, GError **error);

void cobalt_launcher_free(CobaltLauncher *launcher);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CobaltLauncher, cobalt_launcher_free)
