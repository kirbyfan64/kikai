project('kikai', 'c')

glib = dependency('glib-2.0')
gio = dependency('gio-2.0')
gobject = dependency('gobject-2.0')
libarchive = dependency('libarchive')
libcurl = dependency('libcurl')
yaml = dependency('yaml-0.1')

cc = meson.get_compiler('c')

if not cc.has_header('gdbm.h')
  error('gdbm.h is required.')
endif
gdbm = cc.find_library('gdbm')

libm = cc.find_library('m', required : false)

executable(
  'kikai',
  [
    'src/kikai.c', 'src/kikai-build.c', 'src/kikai-builderspec.c', 'src/kikai-source.c',
    'src/kikai-toolchain.c', 'src/kikai-utils.c',
  ],
  dependencies : [gdbm, glib, gio, gobject, libarchive, libcurl, yaml, libm])
