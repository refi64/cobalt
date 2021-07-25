/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#pragma once

#include <gtk/gtk.h>

#define COBALT_TYPE_ALERT cobalt_alert_get_type()
G_DECLARE_FINAL_TYPE(CobaltAlert, cobalt_alert, COBALT, ALERT, GtkDialog)

CobaltAlert *cobalt_alert_new_from_resources(const char *title,
                                             ...) G_GNUC_NULL_TERMINATED;
