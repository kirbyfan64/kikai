#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "kikai-builderspec.h"

typedef struct KikaiToolchain KikaiToolchain;

struct KikaiToolchain {
  const gchar *cc, *cxx, *triple;
};

gboolean kikai_toolchain_create(GFile *storage, KikaiToolchain *toolchain,
                                const KikaiToolchainSpec* spec);
