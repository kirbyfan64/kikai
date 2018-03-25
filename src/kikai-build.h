#pragma once

#include <glib.h>
#include <gio/gio.h>

#include "kikai-builderspec.h"

gboolean kikai_build(KikaiModuleBuildSpec spec, GFile *where, GFile *install);
