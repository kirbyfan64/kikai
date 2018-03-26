#pragma once

#include <glib.h>

#include "kikai-builderspec.h"

gboolean kikai_processsource(GFile *storage, GFile *extracted, gchar *module_id,
                             KikaiModuleSourceSpec *source);
