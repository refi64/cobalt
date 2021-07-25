project('cobalt', 'c')

add_project_arguments('-DG_LOG_DOMAIN="cobalt"', language : 'c')

deps = [
  dependency('glib-2.0', required : true),
  dependency('gio-2.0', required : true),
  dependency('gtk+-3.0', required : true),
]

gnome = import('gnome')
resources = gnome.compile_resources('cobalt-resources',
    'data/cobalt.gresource.xml',
    source_dir : 'data',
    c_name : 'cobalt')

executable('cobalt',
    [
      'src/cobalt-alert.c',
      'src/cobalt-config.c',
      'src/cobalt-host.c',
      'src/cobalt-launcher.c',
      'src/cobalt-main.c',
    ] + resources,
    dependencies : deps,
    install : true)
