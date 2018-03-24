#pragma once

#include <glib.h>

#include "kikai-builderspec.h"

GFile *kikai_processsource(GFile *storage, gchar *module_id,
                           KikaiModuleSourceSpec *source);
