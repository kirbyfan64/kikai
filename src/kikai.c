#include <glib.h>
#include <gio/gio.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "kikai-builderspec.h"
#include "kikai-toolchain.h"
#include "kikai-utils.h"

static void on_error(const gchar *string) {
  fprintf(stderr, KIKAI_CBOLD KIKAI_CRED "Error: " KIKAI_CRESET "%s\n", string);
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

  gpointer *modules_to_run;
  if (argc == 1) {
    guint len;
    modules_to_run = g_hash_table_get_keys_as_array(builder.modules, &len);
  } else {
    modules_to_run = (gpointer*)(argv + 1);
  }

  KikaiToolchain toolchain;
  if (!kikai_toolchain_create(storage, &toolchain, &builder.toolchain)) {
    return 1;
  }

  while (*modules_to_run) {
    const gchar *module_name = *modules_to_run;

    KikaiModuleSpec *module = g_hash_table_lookup(builder.modules, module_name);
    if (module == NULL) {
      g_printerr("Non-existent module: %s", module_name);
      return 1;
    }

    gchar *id = kikai_hash_bytes((guchar*)module_name, -1, NULL);

    kikai_printstatus("build", "Going to build %s.", module_name);
    for (int i = 0; i < module->sources->len; i++) {
      /* kikai_processsource(id, module->sources[i]); */
    }

    modules_to_run++;
  }

  return 0;
}
