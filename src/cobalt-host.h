/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <glib.h>

typedef struct CobaltHost CobaltHost;

CobaltHost *cobalt_host_new();
void cobalt_host_free(CobaltHost *host);

const char *cobalt_host_get_app_id(CobaltHost *host, GError **error);

const char *cobalt_host_get_app_exec(CobaltHost *host, GError **error);

gboolean cobalt_host_get_zypak_available(CobaltHost *host, gboolean *available,
                                         GError **error);
gboolean cobalt_host_get_expose_pids_available(CobaltHost *host, gboolean *available,
                                               GError **error);

G_DEFINE_AUTOPTR_CLEANUP_FUNC(CobaltHost, cobalt_host_free)
