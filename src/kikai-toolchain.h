#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "kikai-builderspec.h"

typedef struct KikaiToolchain KikaiToolchain;

struct KikaiToolchain {
  const gchar *platform, *path, *cc, *cxx, *triple;
  gboolean standalone;
};

gboolean kikai_toolchain_create(GFile *storage, GArray *toolchains,
                                const KikaiToolchainSpec* spec);
