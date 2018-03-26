#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "kikai-builderspec.h"
#include "kikai-toolchain.h"

gboolean kikai_build(KikaiToolchain *toolchain, const gchar *module_id,
                     KikaiModuleBuildSpec spec, GFile *where, GFile *install,
                     gboolean updated);
