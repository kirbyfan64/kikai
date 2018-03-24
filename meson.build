project('kikai', 'c')

glib = dependency('glib-2.0')
gio = dependency('gio-2.0')
gobject = dependency('gobject-2.0')
libarchive = dependency('libarchive')
libcurl = dependency('libcurl')
yaml = dependency('yaml-0.1')

libm = meson.get_compiler('c').find_library('m', required : false)

executable(
  'kikai',
  [
    'src/kikai.c', 'src/kikai-builderspec.c', 'src/kikai-source.c',
    'src/kikai-toolchain.c', 'src/kikai-utils.c',
  ],
  dependencies : [glib, gio, gobject, libarchive, libcurl, yaml, libm])
