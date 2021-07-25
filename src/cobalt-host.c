/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "cobalt-host.h"

#include <gio/gdesktopappinfo.h>

#define FLATPAK_INFO_PATH "/.flatpak-info"

#define FLATPAK_INFO_APPLICATION "Application"
#define FLATPAK_INFO_APPLICATION_NAME "name"

#define ZYPAK_WRAPPER_PATH "/app/bin/zypak-wrapper.sh"

#define FLATPAK_PORTAL_NAME "org.freedesktop.portal.Flatpak"
#define FLATPAK_PORTAL_OBJECT "/org/freedesktop/portal/Flatpak"
#define FLATPAK_PORTAL_INTERFACE FLATPAK_PORTAL_NAME

#define FLATPAK_PORTAL_PROPERTY_VERSION "version"
#define FLATPAK_PORTAL_PROPERTY_SUPPORTS "supports"

#define FLATPAK_PORTAL_MINIMUM_VERSION 4

enum FlatpakPortalSupports {
  FLATPAK_PORTAL_SUPPORTS_EXPOSE_PIDS = 1 << 0,
};

enum {
  FLAG_ZYPAK_SET = 1 << 0,
  FLAG_ZYPAK_AVAILABLE = 1 << 1,
  FLAG_EXPOSE_PIDS_SET = 1 << 2,
  FLAG_EXPOSE_PIDS_AVAILABLE = 1 << 3,
};

struct CobaltHost {
  char *app_id;
  char *exec;
  int flags;
};

CobaltHost *cobalt_host_new() { return g_new0(CobaltHost, 1); }

const char *cobalt_host_get_app_id(CobaltHost *host, GError **error) {
  if (!host->app_id) {
    g_autoptr(GKeyFile) key_file = g_key_file_new();
    if (!g_key_file_load_from_file(key_file, FLATPAK_INFO_PATH, G_KEY_FILE_NONE, error)) {
      return NULL;
    }

    host->app_id = g_key_file_get_string(key_file, FLATPAK_INFO_APPLICATION,
                                         FLATPAK_INFO_APPLICATION_NAME, error);
  }

  return host->app_id;
}

const char *cobalt_host_get_app_exec(CobaltHost *host, GError **error) {
  if (!host->exec) {
    const char *app_id = cobalt_host_get_app_id(host, error);
    if (app_id == NULL) {
      g_prefix_error(error, "Getting app ID: ");
      return NULL;
    }

    g_autofree char *desktop_filename = g_strdup_printf("%s.desktop", app_id);
    g_autoptr(GDesktopAppInfo) app_info = g_desktop_app_info_new(desktop_filename);
    if (app_info == NULL) {
      g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND,
                  "Cannot find desktop file for '%s'", app_id);
      return NULL;
    }

    host->exec = g_desktop_app_info_get_string(app_info, G_KEY_FILE_DESKTOP_KEY_EXEC);
    if (host->exec == NULL) {
      g_set_error(error, G_KEY_FILE_ERROR, G_KEY_FILE_ERROR_KEY_NOT_FOUND,
                  "Desktop file is missing 'Exec' key");
    }
  }

  return host->exec;
}

gboolean cobalt_host_get_zypak_available(CobaltHost *host, gboolean *available,
                                         GError **error) {
  if (!(host->flags & FLAG_ZYPAK_SET)) {
    gboolean local_available = access(ZYPAK_WRAPPER_PATH, F_OK | X_OK) != -1;
    if (!local_available && errno != ENOENT && errno != EPERM) {
      int saved_errno = errno;
      g_set_error(error, G_IO_ERROR, g_io_error_from_errno(saved_errno),
                  "Failed to check " ZYPAK_WRAPPER_PATH " existence: %s",
                  g_strerror(saved_errno));
      return FALSE;
    }

    host->flags |= FLAG_ZYPAK_SET;
    if (local_available) {
      g_debug("Zypak is available");
      host->flags |= FLAG_ZYPAK_AVAILABLE;
    } else {
      g_debug("Zypak is not available (" ZYPAK_WRAPPER_PATH " not found)");
    }
  }

  *available = host->flags & FLAG_ZYPAK_AVAILABLE;
  return TRUE;
}

static gboolean get_uint32_property(GDBusProxy *proxy, const char *property,
                                    guint32 *dest, GError **error) {
  g_autoptr(GVariant) value = g_dbus_proxy_get_cached_property(proxy, property);
  if (value == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "Failed to read '%s'", property);
    return FALSE;
  }

  if (!g_variant_type_equal(g_variant_get_type(value), G_VARIANT_TYPE_UINT32)) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Invalid type '%s' for '%s'",
                g_variant_get_type_string(value), property);
    return FALSE;
  }

  g_variant_get(value, "u", dest);
  return TRUE;
}

gboolean cobalt_host_get_expose_pids_available(CobaltHost *host, gboolean *available,
                                               GError **error) {
  if (!(host->flags & FLAG_EXPOSE_PIDS_SET)) {
    g_autoptr(GDBusProxy) proxy = g_dbus_proxy_new_for_bus_sync(
        G_BUS_TYPE_SESSION,
        G_DBUS_PROXY_FLAGS_DO_NOT_CONNECT_SIGNALS | G_DBUS_PROXY_FLAGS_DO_NOT_AUTO_START,
        NULL, FLATPAK_PORTAL_NAME, FLATPAK_PORTAL_OBJECT, FLATPAK_PORTAL_INTERFACE, NULL,
        error);
    if (proxy == NULL) {
      g_prefix_error(error, "Failed to get portal proxy: ");
      return FALSE;
    }

    gboolean local_available = FALSE;

    guint32 version = 0;
    if (!get_uint32_property(proxy, FLATPAK_PORTAL_PROPERTY_VERSION, &version, error)) {
      return FALSE;
    }

    if (version >= FLATPAK_PORTAL_MINIMUM_VERSION) {
      guint32 supports = 0;
      if (!get_uint32_property(proxy, FLATPAK_PORTAL_PROPERTY_SUPPORTS, &supports,
                               error)) {
        return FALSE;
      }

      local_available = supports & FLATPAK_PORTAL_SUPPORTS_EXPOSE_PIDS;
      if (!local_available) {
        g_debug("expose-pids is not supported by the running Flatpak portal "
                "instance");
      }
    } else {
      g_debug("Portal version too old for expose-pids (%d < %d)", version,
              FLATPAK_PORTAL_MINIMUM_VERSION);
    }

    host->flags |= FLAG_EXPOSE_PIDS_SET;
    if (local_available) {
      host->flags |= FLAG_EXPOSE_PIDS_AVAILABLE;
      g_debug("expose-pids is available");
    } else {
      g_debug("expose-pids is not available");
    }
  }

  *available = host->flags & FLAG_EXPOSE_PIDS_AVAILABLE;
  return TRUE;
}

void cobalt_host_free(CobaltHost *host) {
  g_clear_pointer(&host->app_id, g_free);
  g_clear_pointer(&host->exec, g_free);
  g_free(host);
}
