# Cobalt

*<b>C</b>hr<b>o</b>mium <b>B</b>ootstr<b>a</b>p for F<b>l</b>a<b>t</b>paks*

Cobalt is a unified "launcher" for the various Chromium/Chrome-based Flatpaks,
in order to avoid code duplication in their launcher scripts.

Cobalt follows a "convention over configuration" approach, and in most
cases no configuration file at all, or a very short one, is needed for usage.

## Flags

Cobalt also supports reading flags to pass to the browser on startup from
`~/.var/app/APP_ID/config/NAME-flags.conf`, where `NAME` is the value of the
`Name=` key in the config file (see below).

Each line of `NAME-flags.conf` is a single flag to be passed to the browser,
and lines starting with a `#` are treated as comments and skipped:

```
# This is a comment.

# Our first flag:
--disable-gpu
# Our second flag:
--ozone-backend=wayland
```

In addition,  to enable and disable features, a special syntax is supported:
`features+=FEATURE` and `features-=FEATURE`. Unlike `--enable-features=` and
`--disable-features`, `features+=` and `features-=` are cumulative and thus do
*not* clear out all the previous values. In other words, this will only enable
`UseOzonePlatform`, as the second value *overwrites* the first:

```
--enable-features=VaapiVideoEncoder,VaapiVideoDecoder
--enable-features=UseOzonePlatform
```

Whereas this results in all three features enabled:

```
features+=VaapiVideoEncoder
features+=VaapiVideoDecoder
features+=UseOzonePlatform
```

## Debugging

If you're not sure why your configuration isn't working,
setting the variable `G_MESSAGES_DEBUG=cobalt` will print debugging information
on:

- Default values inferred when not present in the config file.
- Environment variables set.
- The full command line of the browser being started.

## Configuration file

The file, if needed, should go into `/app/etc/cobalt.ini`.

```ini
[Application]
# The application's name, defaults to the last component of the app ID,
# lowercased. For instance, given org.chromium.Chromium, the name will be set to
# 'chromium'.
Name=brave

# The actual entry point binary for the browser. This should point to the "true"
# main executable binary, and not any wrapper scripts. If omitted, defaults to
# whichever exists of "/app/Name/Name" or "/app/extra/Name", where "Name" is
EntryPoint=/app/brave/brave

# The "wrapper script" that the Flatpak uses. In other words, this is the
# script that is run when the user clicks on the application icon, and then that
# script is what starts Cobalt. If omitted, Cobalt will check your desktop file
# and pull the script from there.
WrapperScript=/app/bin/brave

# The subdirectory under XDG_CONFIG_HOME that the browser stores its files in.
# This has no default, and it only needs to be set if "ExposeWidevine" is set
# below.
ConfigDir=BraveSoftware/Brave-Browser

# A semicolon-separated list of URLs to show the first time the browser is
# opened.
FirstRunUrls=file:///app/share/flatpak-chrome/first_run.html

# If your browser was previously using a flags file other than NAME-flags.conf,
# you can set this to the old name to automatically migrate the flags within.
MigrateFlagsFile=chrome-flags.conf

# Flatpak 1.8+ comes with a feature known as 'expose-pids', which is used by
# Zypak to run more efficiently. (Chromium Flatpaks that don't use Zypak, like
# Chromium itself and Ungoogled Chromium, generally require this and won't start
# without it.) The feature also depends on unprivileged user namespaces being
# enabled on the host system. This option determines what to do if expose-pids
# is not available:
# - If "required", then an error dialog is shown to the user, and the
#   application will not start.
# - If "recommended", then a warning dialog will be shown to the user, and they
#   can elect to never show it again.
# - If "optional", then this will not be checked at all.
# If omitted, this option will default to "recommended" if Zypak.Enabled is true
# (see blow) and "required" if not.
ExposePids=recommended

[Zypak]
# If true, then zypak-wrapper.sh will be prepended to the command. If omitted,
# it will be set to 'true' of zypak-wrapper.sh is present in the Flatpak.
Enabled=true

# Some Flatpaks have a different name for their sandbox binary. If so, you can
# set it here. If omitted, the sandbox name will try to automatically be
# detected by checking for either a 'chrome-sandbox', or 'NAME-sandbox', or
# 'ENTRYPOINT-sandbox' (where NAME is the value of Application.Name, and
# ENTRYPOINT is the last path component of Application.EntryPoint), all in the
# EntryPoint's directory.
# For more info, see: https://github.com/refi64/zypak#alternate-sandbox-binary-names
SandboxFilename=chrome-sandbox

# If true, then the directory at CONFIG_DIR/WIDEVINE_PATH will be exposed into
# the sandbox so that Widevine can be loaded.
ExposeWidevine=true

# Always defaults to WidevineCdm.
WidevinePath=WidevineCdm

# This lets you enable or disable some Chromium features by default. Each value
# is a semicolon-separated list of features to enable/disable.
[DefaultFeatures]
Enabled=EnablePipeWireRTCCapturer
Disabled=EnablePipeWireRTCCapturer
```
