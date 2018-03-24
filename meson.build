project('kikai', 'c')

glib = dependency('glib-2.0')
gio = dependency('gio-2.0')
gobject = dependency('gobject-2.0')
yaml = dependency('yaml-0.1')

executable(
  'kikai',
  [
    'src/kikai.c', 'src/kikai-builderspec.c', 'src/kikai-toolchain.c',
    'src/kikai-utils.c',
  ],
  dependencies : [glib, gio, gobject, yaml])
