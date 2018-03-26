#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "kikai-builderspec.h"
#include "kikai-toolchain.h"

gboolean kikai_build(KikaiToolchain *toolchain, const gchar *module_id,
                     KikaiModuleBuildSpec spec, GFile *sources, GFile *buildroot,
                     GFile *install, gboolean updated);
