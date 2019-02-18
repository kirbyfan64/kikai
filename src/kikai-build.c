#include <glib.h>

#include "kikai-build.h"
#include "kikai-toolchain.h"
#include "kikai-utils.h"

#include <string.h>

static gboolean needs_update(const gchar *scope, const gchar *module_id,
                             const gchar *step, const gchar *current_hash) {
  g_autofree gchar *key = g_strjoin("::", scope, module_id, step, NULL);
  g_autofree gchar *old_hash = kikai_db_get(key);

  if (strcmp(old_hash, current_hash) != 0) {
    if (old_hash == &kikai_db_missing) {
      g_steal_pointer(&old_hash);
    }

    return TRUE;
  }

  return FALSE;
}

static gboolean set_key(const gchar *scope, const gchar *module_id,
                        const gchar *step, const gchar *hash) {
  g_autofree gchar *key = g_strjoin("::", scope, module_id, step, NULL);
  return kikai_db_set(key, hash);
}

static gboolean parse_options(const gchar *step, GArray *dest, const gchar *options) {
  g_autoptr(GError) error = NULL;

  if (options == NULL) {
    return TRUE;
  }

  gint extra_argc = 0;
  gchar **extra_argv = NULL;
  if (!g_shell_parse_argv(options, &extra_argc, &extra_argv, &error)) {
    g_printerr("Failed to parse %s-options: %s", step, error->message);
    return FALSE;
  }

  for (int i = 0; i < extra_argc; i++) {
    g_array_append_val(dest, extra_argv[i]);
  }

  return TRUE;
}

static gboolean simple_build(KikaiToolchain *toolchain, const gchar *module_id,
                             KikaiModuleBuildSpec spec, GFile *sources,
                             GFile *buildroot, GFile *install, gboolean updated) {
  g_auto(GStrv) env = g_get_environ();
  env = g_environ_setenv(env, "KIKAI_SOURCES", g_file_get_path(sources), TRUE);
  env = g_environ_setenv(env, "KIKAI_PREFIX", g_file_get_path(install), TRUE);
  env = g_environ_setenv(env, "KIKAI_TOOLCHAIN", toolchain->path, TRUE);
  env = g_environ_setenv(env, "KIKAI_TRIPLE", toolchain->triple, TRUE);
  env = g_environ_setenv(env, "KIKAI_CC", toolchain->cc, TRUE);
  env = g_environ_setenv(env, "KIKAI_CXX", toolchain->cxx, TRUE);

  env = g_environ_setenv(env, "SOURCES", g_file_get_path(sources), TRUE);
  env = g_environ_setenv(env, "PREFIX", g_file_get_path(install), TRUE);
  env = g_environ_setenv(env, "CC", toolchain->cc, TRUE);
  env = g_environ_setenv(env, "CXX", toolchain->cxx, TRUE);

  for (int i = 0; i < spec.simple.steps->len; i++) {
    KikaiModuleSimpleBuildStep *step = &g_array_index(spec.simple.steps,
                                                      KikaiModuleSimpleBuildStep, i);

    g_autofree gchar *hash = kikai_hash_bytes(step->run, -1, NULL);
    if (!needs_update("build-simple", module_id, step->name, hash)) {
      continue;
    }

    kikai_printstatus("build", "  - %s", step->name);

    g_autoptr(GError) error = NULL;

    gchar *args[] = {"/bin/sh", "-ec", (gchar *)step->run, NULL};
    gint status;
    if (!g_spawn_sync(g_file_get_path(buildroot), args, env, G_SPAWN_DEFAULT, NULL, NULL,
                        NULL, NULL, &status, &error)) {
      g_printerr("Failed to spawn build step: %s", error->message);
      return FALSE;
    }

    if (!g_spawn_check_exit_status(status, &error)) {
      g_printerr("Build step failed: %s", error->message);
      return FALSE;
    }

    if (!set_key("build-simple", module_id, step->name, hash)) {
      return FALSE;
    }
  }

  return TRUE;
}

#define null_or(value, if_null) ((value) != NULL ? (value) : (if_null))

static gboolean autotools_build(KikaiToolchain *toolchain, const gchar *module_id,
                                KikaiModuleBuildSpec spec, GFile *sources,
                                GFile *buildroot, GFile *install, gboolean updated) {
  g_auto(GStrv) env = g_get_environ();
  env = g_environ_setenv(env, "PREFIX", g_file_get_path(install), TRUE);
  env = g_environ_setenv(env, "CC", toolchain->cc, TRUE);
  env = g_environ_setenv(env, "CXX", toolchain->cxx, TRUE);
  env = g_environ_setenv(env, "NOCONFIGURE", "1", TRUE);

  g_autoptr(GError) error = NULL;
  gint status;

  g_autofree gchar
    *configure_hash = kikai_hash_bytes(null_or(spec.autotools.configure_options, ""), -1,
                                       null_or(spec.autotools.cflags, ""), -1,
                                       null_or(spec.autotools.cppflags, ""), -1,
                                       null_or(spec.autotools.ldflags, ""), -1,
                                       NULL),
    *make_hash = kikai_hash_bytes(null_or(spec.autotools.make_options, ""), -1, NULL);

  if (updated || needs_update("build-autotools", module_id, "configure",
      configure_hash)) {
    g_autofree gchar *pkgconf = g_find_program_in_path("pkgconf");
    if (pkgconf == NULL) {
      g_printerr("pkgconf is required.");
      return FALSE;
    }

    g_autoptr(GFile) configure = g_file_get_child(sources, "configure");
    if (!g_file_query_exists(configure, NULL)) {
      g_autoptr(GFile) autogen = g_file_get_child(sources, "autogen.sh");
      g_autofree gchar *autoreconf = g_find_program_in_path("autoreconf");

      const gchar *autogen_args[] = {g_file_get_path(autogen), NULL};
      const gchar *autoreconf_args[] = {autoreconf, "-si", NULL};
      const gchar *descr = NULL;
      gchar **args = NULL;

      if (g_file_query_exists(autogen, NULL)) {
        descr = "autogen.sh";
        args = (gchar**)autogen_args;
      } else if (autoreconf != NULL) {
        descr = "autoreconf";
        args = (gchar**)autoreconf_args;
      } else {
        g_printerr("autogen.sh does not exist, and autoreconf is not available.");
        return FALSE;
      }

      kikai_printstatus("build", "  - %s", descr);

      if (!g_spawn_sync(g_file_get_path(sources), args, env, G_SPAWN_DEFAULT, NULL,
                        NULL, NULL, NULL, &status, &error)) {
        g_printerr("Failed to spawn %s: %s", descr, error->message);
        return FALSE;
      }

      if (!g_spawn_check_exit_status(status, &error)) {
        g_printerr("%s failed: %s", descr, error->message);
        return FALSE;
      }
    }

    g_autoptr(GFile) include = g_file_get_child(install, "include");
    g_autoptr(GFile) lib = g_file_get_child(install, "lib");
    g_autoptr(GFile) pkgconfiglib = g_file_get_child(lib, "pkgconfig");
    g_autoptr(GFile) pkgconfigshare = kikai_join(install, "share", "pkgconfig", NULL);

    g_autofree gchar *include_arg = g_strconcat("-I", g_file_get_path(include), NULL);
    g_autofree gchar *lib_arg = g_strconcat("-L", g_file_get_path(lib), NULL);

    GArray *configure_args = g_array_new(TRUE, FALSE, sizeof(gchar *));

    const gchar *configure_path = g_file_get_path(configure);
    g_array_append_val(configure_args, configure_path);

    g_autofree gchar *configure_cc = g_strjoin("=", "CC", toolchain->cc, NULL);
    g_array_append_val(configure_args, configure_cc);

    g_autofree gchar *configure_cxx = g_strjoin("=", "CXX", toolchain->cxx, NULL);
    g_array_append_val(configure_args, configure_cxx);

    g_autofree gchar *cflags = g_strjoin(" ", include_arg, "-fPIC", "-fPIE",
                                         spec.autotools.cflags, NULL);
    g_autofree gchar *configure_cflags = g_strjoin("=", "CFLAGS", cflags, NULL);
    g_array_append_val(configure_args, configure_cflags);

    g_autofree gchar *cppflags = g_strjoin(" ", include_arg, spec.autotools.cppflags,
                                           NULL);
    g_autofree gchar *configure_cppflags = g_strjoin("=", "CPPFLAGS", cppflags, NULL);
    g_array_append_val(configure_args, configure_cppflags);

    g_autofree gchar *ldflags = g_strjoin(" ", lib_arg, "-pie", spec.autotools.ldflags,
                                          NULL);
    g_autofree gchar *configure_ldflags = g_strjoin("=", "LDFLAGS", ldflags, NULL);
    g_array_append_val(configure_args, configure_ldflags);

    g_autofree gchar *pkgconfig = g_strjoin(" ", pkgconf, "--env-only", NULL);
    g_autofree gchar *configure_pkgconfig = g_strjoin("=", "PKG_CONFIG", pkgconfig,
                                                      NULL);
    g_array_append_val(configure_args, configure_pkgconfig);

    g_autofree gchar *pkgconfigpath = g_strjoin(":", g_file_get_path(pkgconfiglib),
                                                g_file_get_path(pkgconfigshare), NULL);
    g_autofree gchar *configure_pkgconfigpath = g_strjoin("=", "PKG_CONFIG_PATH",
                                                          pkgconfigpath, NULL);
    g_array_append_val(configure_args, configure_pkgconfigpath);

    g_autofree gchar *configure_prefix = g_strjoin("=", "--prefix",
                                                   g_file_get_path(install), NULL);
    g_array_append_val(configure_args, configure_prefix);

    g_autofree gchar *configure_host = g_strjoin("=", "--host", toolchain->triple, NULL);
    g_array_append_val(configure_args, configure_host);

    if (!parse_options("configure", configure_args, spec.autotools.configure_options)) {
      return FALSE;
    }

    kikai_printstatus("build", "  - configure");

    if (!g_spawn_sync(g_file_get_path(buildroot), (gchar **)configure_args->data, env,
                      G_SPAWN_DEFAULT, NULL, NULL, NULL, NULL, &status, &error)) {
      g_printerr("Failed to spawn configure: %s", error->message);
      return FALSE;
    }

    if (!g_spawn_check_exit_status(status, &error)) {
      g_printerr("configure failed: %s", error->message);
      return FALSE;
    }

    if (!set_key("build-autotools", module_id, "configure", configure_hash)) {
      return FALSE;
    }
  }

  if (updated || needs_update("build-autotools", module_id, "make", make_hash)) {
    g_autoptr(GFile) makefile = g_file_get_child(buildroot, "Makefile");
    if (!g_file_query_exists(makefile, NULL)) {
      g_printerr("Makefile does not exist.");
      return FALSE;
    }

    GArray *make_args = g_array_new(TRUE, FALSE, sizeof(gchar *));

    g_autofree gchar *make_path = toolchain->standalone
                                  ? g_build_filename(toolchain->path, "bin", "make", NULL)
                                  : g_strdup("make");
    g_array_append_val(make_args, make_path);

    const gchar *make_install = "install";
    g_array_append_val(make_args, make_install);

    if (!parse_options("make", make_args, spec.autotools.make_options)) {
      return FALSE;
    }

    kikai_printstatus("build", "  - make");

    if (!g_spawn_sync(g_file_get_path(buildroot), (gchar **)make_args->data, env,
                      G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, NULL, &status, &error)) {
      g_printerr("Failed to spawn make: %s", error->message);
      return FALSE;
    }

    if (!g_spawn_check_exit_status(status, &error)) {
      g_printerr("make failed: %s", error->message);
      return FALSE;
    }

    if (!set_key("build-autotools", module_id, "make", make_hash)) {
      return FALSE;
    }
  }

  return TRUE;
}

gboolean kikai_build(KikaiToolchain *toolchain, const gchar *module_id,
                     KikaiModuleBuildSpec spec, GFile *sources, GFile *buildroot,
                     GFile *install, gboolean updated) {
  switch (spec.type) {
  case KIKAI_BUILD_SIMPLE:
    return simple_build(toolchain, module_id, spec, sources, buildroot, install,
                        updated);
  case KIKAI_BUILD_AUTOTOOLS:
    return autotools_build(toolchain, module_id, spec, sources, buildroot, install,
                           updated);
  }
}
