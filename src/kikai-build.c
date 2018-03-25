#include "kikai-build.h"
#include "kikai-utils.h"

#include <glib.h>

static gboolean simple_build(KikaiModuleBuildSpec spec, GFile *where, GFile *install) {
  for (int i = 0; i < spec.simple.steps->len; i++) {
    KikaiModuleSimpleBuildStep *step = &g_array_index(spec.simple.steps,
                                                      KikaiModuleSimpleBuildStep, i);
    kikai_printstatus("build", "  %s", step->name);
  }

  return TRUE;
}

static gboolean autotools_build(KikaiModuleBuildSpec spec, GFile *where,
                                GFile *install) {
  return TRUE;
}

gboolean kikai_build(KikaiModuleBuildSpec spec, GFile *where, GFile *install) {
  switch (spec.type) {
  case KIKAI_BUILD_SIMPLE:
    return simple_build(spec, where, install);
  case KIKAI_BUILD_AUTOTOOLS:
    return autotools_build(spec, where, install);
  }
}
