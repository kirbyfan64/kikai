#include <glib.h>
#include <gio/gio.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kikai-builderspec.h"
#include "kikai-build.h"
#include "kikai-source.h"
#include "kikai-toolchain.h"
#include "kikai-utils.h"

static void on_error(const gchar *string) {
  fprintf(stderr, KIKAI_CBOLD KIKAI_CRED "Error: " KIKAI_CRESET "%s\n", string);
}

static gboolean process_deps(GArray *modules_to_run, GHashTable *already_present,
                             GHashTable *all_modules, gchar **requested_modules) {
  for (; *requested_modules; requested_modules++) {
    gchar *module_name = *requested_modules;
    if (!g_hash_table_add(already_present, module_name)) {
      continue;
    }

    KikaiModuleSpec *module = g_hash_table_lookup(all_modules, module_name);
    if (module == NULL) {
      g_printerr("Non-existent module: %s", module_name);
      return FALSE;
    }

    if (!process_deps(modules_to_run, already_present, all_modules,
                      (gchar **)module->dependencies->data)) {
      return FALSE;
    }

    g_array_append_val(modules_to_run, module);
  }

  return TRUE;
}

int main(int argc, char **argv) {
  g_set_printerr_handler(on_error);

  if (argc >= 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    puts("usage: kikai [modules...]");
    return 0;
  }

  KikaiBuilderSpec builder;
  if (!kikai_builderspec_parse(&builder, "kikai.yml")) {
    return 1;
  }

  GFile *storage = g_file_new_for_path(".kikai");
  if (!kikai_mkdir_parents(storage)) {
    return 1;
  }
  g_assert(g_file_query_exists(storage, NULL));

  if (!kikai_db_load(storage)) {
    return 1;
  }

  gchar **requested_modules;
  if (argc == 1) {
    guint len;
    requested_modules = (gchar **)g_hash_table_get_keys_as_array(builder.modules, &len);
  } else {
    requested_modules = argv + 1;
  }

  g_autoptr(GArray) modules_to_run = g_array_new(FALSE, FALSE, sizeof(gchar *));
  g_autoptr(GHashTable) already_present = g_hash_table_new(g_str_hash, g_str_equal);
  if (!process_deps(modules_to_run, already_present,
                    builder.modules, requested_modules)) {
    return FALSE;
  }

  GArray *toolchains = g_array_new(FALSE, FALSE, sizeof(KikaiToolchain));
  if (!kikai_toolchain_create(storage, toolchains, &builder.toolchain)) {
    return 1;
  }

  g_autoptr(GFile) install_root = g_file_new_for_path(builder.install_root);
  if (!kikai_mkdir_parents(install_root)) {
    return 1;
  }

  for (int i = 0; i < modules_to_run->len; i++) {
    KikaiModuleSpec *module = g_array_index(modules_to_run, KikaiModuleSpec*, i);

    gchar *id = kikai_hash_bytes((guchar*)module->name, -1, NULL);
    g_autoptr(GFile) extracted = kikai_join(storage, "extracted", id, NULL);

    gboolean updated = FALSE;

    kikai_printstatus("build", "Building: %s", module->name);
    for (int i = 0; i < module->sources->len; i++) {
      KikaiModuleSourceSpec *source = &g_array_index(module->sources,
                                                     KikaiModuleSourceSpec, i);
      if (!kikai_processsource(storage, extracted, id, source, &updated)) {
        return 1;
      }
    }

    for (int i = 0; i < toolchains->len; i++) {
      KikaiToolchain *toolchain = &g_array_index(toolchains, KikaiToolchain, i);
      kikai_printstatus("build", "- %s", toolchain->platform);

      if (!kikai_build(toolchain, id, module->build, extracted, install_root, updated)) {
        return 1;
      }
    }
  }

  return 0;
}
