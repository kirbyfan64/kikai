#pragma once

#include <glib.h>

typedef enum KikaiToolchainPlatform KikaiToolchainPlatform;
typedef struct KikaiToolchainSpec KikaiToolchainSpec;
typedef struct KikaiModuleSourceSpec KikaiModuleSourceSpec;
typedef struct KikaiModuleSimpleBuildStep KikaiModuleSimpleBuildStep;
typedef struct KikaiModuleBuildSpec KikaiModuleBuildSpec;
typedef struct KikaiModuleSpec KikaiModuleSpec;
typedef struct KikaiBuilderSpec KikaiBuilderSpec;

enum KikaiToolchainPlatform { KIKAI_PLATFORM_ARM, KIKAI_PLATFORM_X86 };

struct KikaiToolchainSpec {
  const gchar *api, *stl, *after;
  GArray *platforms;
};

struct KikaiModuleSourceSpec {
  const gchar *url;
  gint strip_parents;
};

struct KikaiModuleSimpleBuildStep {
  const gchar *name, *run;
};

struct KikaiModuleBuildSpec {
  enum { KIKAI_BUILD_SIMPLE, KIKAI_BUILD_AUTOTOOLS } type;
  union {
    struct {
      GArray *steps;
    } simple;
    struct {
      const gchar *configure_options, *make_options;
    } autotools;
  };
};

struct KikaiModuleSpec {
  const gchar *name;
  GArray *sources, *dependencies;
  KikaiModuleBuildSpec build;
};

struct KikaiBuilderSpec {
  const char *install_root;
  KikaiToolchainSpec toolchain;
  GHashTable *modules;
};

gboolean kikai_builderspec_parse(KikaiBuilderSpec* builder, const gchar *file);
