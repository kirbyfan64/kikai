#include "kikai-builderspec.h"

#include <glib.h>
#include <glib-object.h>
#include <gmodule.h>
#include <yaml.h>

#include <errno.h>

static void yaml_value_free(gpointer value) {
  g_value_unset((GValue*)value);
}

static void yaml_value_free_owned(gpointer value) {
  yaml_value_free(value);
  g_free(value);
}

static void read_yaml_node(GValue *value, yaml_document_t *doc, yaml_node_t *node) {
  *value = (GValue)G_VALUE_INIT;

  switch (node->type) {
  case YAML_NO_NODE:
    g_printerr("Unexpected empty node @%zu:%zu.", node->start_mark.line,
               node->start_mark.column);
    g_value_init(value, G_TYPE_STRING);
    g_value_set_static_string(value, "");
    break;
  case YAML_SCALAR_NODE:
    if (strncmp((gchar *)node->data.scalar.value, "true", node->data.scalar.length) == 0 ||
        strncmp((gchar *)node->data.scalar.value, "false", node->data.scalar.length) == 0) {
      g_value_init(value, G_TYPE_BOOLEAN);
      g_value_set_boolean(value, *node->data.scalar.value == 't');
    } else {
      g_value_init(value, G_TYPE_STRING);
      g_value_take_string(value, g_strndup((gchar*)node->data.scalar.value,
                                           node->data.scalar.length));
    }
    break;
  case YAML_SEQUENCE_NODE:
    g_value_init(value, G_TYPE_ARRAY);
    GArray *array = g_array_new(FALSE, FALSE, sizeof(GValue));
    g_array_set_clear_func(array, yaml_value_free);

    for (yaml_node_item_t *item = node->data.sequence.items.start;
         item < node->data.sequence.items.top; item++) {
      yaml_node_t *child = yaml_document_get_node(doc, *item);
      if (child != NULL) {
        GValue child_g;
        read_yaml_node(&child_g, doc, child);
        g_array_append_val(array, child_g);
      }
    }

    g_value_take_boxed(value, array);
    break;
  case YAML_MAPPING_NODE:
    g_value_init(value, G_TYPE_HASH_TABLE);
    GHashTable *table = g_hash_table_new_full(g_str_hash, g_str_equal, g_free,
                                              yaml_value_free_owned);

    for (yaml_node_pair_t *pair = node->data.mapping.pairs.start;
         pair < node->data.mapping.pairs.top; pair++) {
      yaml_node_t *key = yaml_document_get_node(doc, pair->key);
      yaml_node_t *value = yaml_document_get_node(doc, pair->value);

      if (key != NULL && value != NULL) {
        if (key->type != YAML_SCALAR_NODE) {
          g_printerr("Unexpected non-scalar key @%zu:%zu.", node->start_mark.line,
                     node->start_mark.column);
          continue;
        }

        GValue *value_g = g_new0(GValue, 1);
        read_yaml_node(value_g, doc, value);
        g_hash_table_insert(table, g_strndup((gchar*)key->data.scalar.value,
                                             key->data.scalar.length), value_g);
      }
    }

    g_value_take_boxed(value, table);
    break;
  default:
    g_printerr("Unexpected node type @%zu:%zu.", node->start_mark.line,
               node->start_mark.column);
    g_value_init(value, G_TYPE_STRING);
    g_value_set_static_string(value, "");
    break;
  }
}

static gboolean read_yaml(GValue *result, const gchar *file) {
  yaml_parser_t parser;
  yaml_document_t doc;
  FILE* fp;
  gboolean success = FALSE;

  if (!yaml_parser_initialize(&parser)) {
    g_printerr("Failed to initialize YAML parser.");
    return FALSE;
  }

  fp = fopen("kikai.yml", "rb");
  if (fp == NULL) {
    g_printerr("Failed to read %s: %s", file, strerror(errno));
    goto done;
  }

  yaml_parser_set_input_file(&parser, fp);

  if (!yaml_parser_load(&parser, &doc))  {
    g_printerr("Failed to parse %s:%zu:%zu: %s", file,parser.problem_mark.line + 1,
               parser.problem_mark.column + 1, parser.problem);
    goto done;
  }

  yaml_node_t *root = yaml_document_get_root_node(&doc);
  if (root == NULL) {
    g_printerr("Failed to read root node from kikai.yml.");
    yaml_document_delete(&doc);
    goto done;
  }

  read_yaml_node(result, &doc, root);
  yaml_document_delete(&doc);

  success = TRUE;

  done:
  yaml_parser_delete(&parser);
  if (fp != NULL) {
    fclose(fp);
  }
  return success;
}

static gboolean check_type(GValue *value, GType type, const gchar *path, ...)
    G_GNUC_PRINTF(3, 4);
static gboolean check_key_type(GHashTable *table, GType type, const gchar *key,
                               GValue **out, const gchar *path, ...)
                               G_GNUC_PRINTF(5, 6);

static gboolean v_check_type(GValue *value, GType type, const gchar *path,
                             va_list args) {
  if (G_UNLIKELY(!G_VALUE_HOLDS(value, type))) {
    g_autofree gchar *full_path = g_strdup_vprintf(path, args);

    g_printerr("Expected %s to be a %s, got %s.", full_path, g_type_name(type),
               G_VALUE_TYPE_NAME(value));
    return FALSE;
  }

  return TRUE;
}

static gboolean check_type(GValue *value, GType type, const gchar *path, ...) {
  va_list args;
  va_start(args, path);
  gboolean result = v_check_type(value, type, path, args);
  va_end(args);

  return result;
}

static gboolean check_key_type(GHashTable *table, GType type, const gchar *key,
                               GValue **out, const gchar *path, ...) {
  GValue *value = g_hash_table_lookup(table, key);
  if (value == NULL) {
    va_list args;
    va_start(args, path);
    gchar *full_path = g_strdup_vprintf(path, args);
    va_end(args);

    g_printerr("%s is missing.", full_path);
    return FALSE;
  } else {
    va_list args;
    va_start(args, path);
    gboolean result = v_check_type(value, type, path, args);
    va_end(args);

    if (!result) {
      return FALSE;
    }
  }

  *out = value;
  return TRUE;
}

static gboolean yaml_to_toolchain(KikaiToolchainSpec *toolchain, GHashTable *data) {
  GValue *api_g;
  if (!check_key_type(data, G_TYPE_STRING, "api", &api_g, "toolchain.api")) {
    return FALSE;
  }
  toolchain->api = g_value_get_string(api_g);

  GValue *stl_g;
  if (!check_key_type(data, G_TYPE_STRING, "stl", &stl_g, "toolchain.stl")) {
    return FALSE;
  }
  toolchain->stl = g_value_get_string(stl_g);

  GValue *standalone_g = NULL;
  if (g_hash_table_lookup(data, "standalone") &&
      !check_key_type(data, G_TYPE_BOOLEAN, "standalone", &standalone_g,
                      "toolchain.standalone")) {
    return FALSE;
  }
  toolchain->standalone = standalone_g ? g_value_get_boolean(standalone_g) : FALSE;

  GValue *after_g = NULL;
  if (g_hash_table_lookup(data, "after") &&
      !check_key_type(data, G_TYPE_STRING, "after", &after_g, "toolchain.after")) {
    return FALSE;
  }
  toolchain->after = after_g ? g_value_get_string(after_g) : NULL;

  GValue *platforms_g;
  if (!check_key_type(data, G_TYPE_ARRAY, "platforms", &platforms_g,
                      "toolchain.platforms")) {
    return FALSE;
  }
  GArray *platforms = g_value_get_boxed(platforms_g);

  toolchain->platforms = g_array_new(FALSE, FALSE, sizeof(KikaiToolchainPlatform));

  for (int i = 0; i < platforms->len; i++) {
    GValue *item_g = &g_array_index(platforms, GValue, i);
    if (!check_type(item_g, G_TYPE_STRING, "toolchain.platforms[%d]", i)) {
      return FALSE;
    }

    const gchar *item = g_value_get_string(item_g);
    KikaiToolchainPlatform platform;
    if (strcmp(item, "arm") == 0) {
      platform = KIKAI_PLATFORM_ARM;
    } else if (strcmp(item, "x86") == 0) {
      platform = KIKAI_PLATFORM_X86;
    } else {
      g_printerr("Invalid toolchain platform: %s", item);
      return FALSE;
    }
    g_array_append_val(toolchain->platforms, platform);
  }

  return TRUE;
}

static gboolean yaml_to_sources(GArray **sources, KikaiModuleSpec *module,
                                GArray *data) {
  *sources = g_array_new(FALSE, FALSE, sizeof(KikaiModuleSourceSpec));

  for (int i = 0; i < data->len; i++) {
    GValue *source_g = &g_array_index(data, GValue, i);
    if (!check_type(source_g, G_TYPE_HASH_TABLE, "modules.%s.sources[%d]",
                    module->name, i)) {
      return FALSE;
    }

    GHashTable *source_data = g_value_get_boxed(source_g);
    GValue *url_g, *after_g = NULL, *strip_parents_g = NULL;

    if (!check_key_type(source_data, G_TYPE_STRING, "url", &url_g,
                        "modules.%s.sources[%d].url", module->name, i)) {
      return FALSE;
    }

    if (g_hash_table_lookup(source_data, "after") != NULL &&
        !check_key_type(source_data, G_TYPE_STRING, "after", &after_g,
                        "modules.%s.sources[%d].after", module->name, i)) {
      return FALSE;
    }

    if (g_hash_table_lookup(source_data, "strip-parents") != NULL &&
        !check_key_type(source_data, G_TYPE_STRING, "strip-parents", &strip_parents_g,
                        "modules.%s.sources[%d].strip-parents", module->name, i)) {
      return FALSE;
    }

    KikaiModuleSourceSpec source;
    source.url = g_value_get_string(url_g);
    source.after = after_g ? g_value_get_string(after_g) : NULL;
    source.strip_parents = strip_parents_g ? atoi(g_value_get_string(strip_parents_g))
                            : -1;
    g_array_append_val(*sources, source);
  }

  return TRUE;
}

static gboolean yaml_to_dependencies(GArray **dependencies, KikaiModuleSpec *module,
                                     GArray *data) {
  *dependencies = g_array_new(TRUE, FALSE, sizeof(gchar*));

  for (int i = 0; i < data->len; i++) {
    GValue *dep_v = &g_array_index(data, GValue, i);
    if (!check_type(dep_v, G_TYPE_STRING, "modules.%s.dependencies[%d]", module->name,
                    i)) {
      return FALSE;
    }

    const gchar *dep = g_value_get_string(dep_v);
    g_array_append_val(*dependencies, dep);
  }

  return TRUE;
}

static gboolean yaml_to_steps(GArray **steps, KikaiModuleSpec *module, GArray *data) {
  *steps = g_array_new(FALSE, FALSE, sizeof(KikaiModuleSimpleBuildStep));

  for (int i = 0; i < data->len; i++) {
    GValue *step_g = &g_array_index(data, GValue, i);
    if (!check_type(step_g, G_TYPE_HASH_TABLE, "modules.%s.steps[%d]",
        module->name, i)) {
      return FALSE;
    }

    GHashTable *step_data = g_value_get_boxed(step_g);
    GValue *name_g, *run_g;

    if (!check_key_type(step_data, G_TYPE_STRING, "name", &name_g,
                        "modules.%s.steps[%d].name", module->name, i)) {
      return FALSE;
    }

    if (!check_key_type(step_data, G_TYPE_STRING, "run", &run_g,
                        "modules.%s.steps[%d].run", module->name, i)) {
      return FALSE;
    }

    KikaiModuleSimpleBuildStep step;
    step.name = g_value_get_string(name_g);
    step.run = g_value_get_string(run_g);
    g_array_append_val(*steps, step);
  }

  return TRUE;
}

static gboolean yaml_to_build(KikaiModuleBuildSpec *build, KikaiModuleSpec *module,
                              GHashTable *data) {
  GValue *type_g;
  if (!check_key_type(data, G_TYPE_STRING, "type", &type_g, "module.%s.build.type",
                      module->name)) {
    return FALSE;
  }

  const gchar *type = g_value_get_string(type_g);

  if (strcmp(type, "simple") == 0) {
    GValue *steps_g;
    if (!check_key_type(data, G_TYPE_ARRAY, "steps", &steps_g, "module.%s.build.steps",
                        module->name)) {
      return FALSE;
    }

    build->type = KIKAI_BUILD_SIMPLE;
    if (!yaml_to_steps(&build->simple.steps, module, g_value_get_boxed(steps_g))) {
      return FALSE;
    }

  } else if (strcmp(type, "autotools") == 0) {
    GValue *configure_options_g = NULL;
    if (g_hash_table_lookup(data, "configure-options") &&
        !check_key_type(data, G_TYPE_STRING, "configure-options",
                        &configure_options_g, "modules.%s.build.configure-options",
                        module->name)) {
      return FALSE;
    }

    GValue *make_options_g = NULL;
    if (g_hash_table_lookup(data, "make-options") &&
        !check_key_type(data, G_TYPE_STRING, "make-options", &make_options_g,
                        "modules.%s.build.make-options", module->name)) {
      return FALSE;
    }

    GValue *cflags_g = NULL;
    if (g_hash_table_lookup(data, "cflags") &&
        !check_key_type(data, G_TYPE_STRING, "cflags", &cflags_g,
                        "modules.%s.build.cflags", module->name)) {
      return FALSE;
    }

    GValue *cppflags_g = NULL;
    if (g_hash_table_lookup(data, "cppflags") &&
        !check_key_type(data, G_TYPE_STRING, "cppflags", &cppflags_g,
                        "modules.%s.build.cppflags", module->name)) {
      return FALSE;
    }

    GValue *ldflags_g = NULL;
    if (g_hash_table_lookup(data, "ldflags") &&
        !check_key_type(data, G_TYPE_STRING, "ldflags", &ldflags_g,
                        "modules.%s.build.ldflags", module->name)) {
      return FALSE;
    }

    build->type = KIKAI_BUILD_AUTOTOOLS;
    build->autotools.configure_options = configure_options_g ?
                                         g_value_get_string(configure_options_g) : NULL;
    build->autotools.make_options = make_options_g ? g_value_get_string(make_options_g)
                                    : NULL;
    build->autotools.cflags = cflags_g ? g_value_get_string(cflags_g) : NULL;
    build->autotools.cppflags = cppflags_g ? g_value_get_string(cppflags_g) : NULL;
    build->autotools.ldflags = ldflags_g ? g_value_get_string(ldflags_g) : NULL;
  } else {
    g_printerr("module.%s.build.type must be either simple or autotools.",
               module->name);
    return FALSE;
  }

  return TRUE;
}

static gboolean yaml_to_modules(GHashTable **modules, GHashTable *data) {
  *modules = g_hash_table_new_full(g_str_hash, g_str_equal, NULL, g_free /*TODO*/);

  GList *keys = g_hash_table_get_keys(data);
  while (keys) {
    const gchar *key = keys->data;
    GValue *value_g = g_hash_table_lookup(data, key);
    g_return_val_if_fail(value_g != NULL, FALSE);

    if (!check_type(value_g, G_TYPE_HASH_TABLE, "modules.%s", key)) {
      return FALSE;
    }

    KikaiModuleSpec *module = g_new0(KikaiModuleSpec, 1);
    module->name = key;

    GHashTable *module_data = g_value_get_boxed(value_g);

    GValue *sources_g;
    if (!check_key_type(module_data, G_TYPE_ARRAY, "sources", &sources_g,
                        "modules.%s.sources", key)) {
      return FALSE;
    }

    if (!yaml_to_sources(&module->sources, module, g_value_get_boxed(sources_g))) {
      return FALSE;
    }

    GValue *dependencies_g;
    if (!check_key_type(module_data, G_TYPE_ARRAY, "dependencies", &dependencies_g,
                        "modules.%s.dependencies", key)) {
      return FALSE;
    }

    if (!yaml_to_dependencies(&module->dependencies, module,
                              g_value_get_boxed(dependencies_g))) {
      return FALSE;
    }

    GValue *build_g;
    if (!check_key_type(module_data, G_TYPE_HASH_TABLE, "build", &build_g,
                        "modules.%s.build", key)) {
      return FALSE;
    }

    if (!yaml_to_build(&module->build, module, g_value_get_boxed(build_g))) {
      return FALSE;
    }

    g_hash_table_insert(*modules, (gpointer)key, module);
    keys = keys->next;
  }

  return TRUE;
}

static gboolean yaml_to_builder(KikaiBuilderSpec *builder, GValue *top_g) {
  if (!check_type(top_g, G_TYPE_HASH_TABLE, "<top-level>")) {
    return FALSE;
  }
  GHashTable *top = g_value_get_boxed(top_g);

  GValue *install_root_g;
  if (!check_key_type(top, G_TYPE_STRING, "install-root", &install_root_g,
      "install-root")) {
    return FALSE;
  }
  builder->install_root = g_value_get_string(install_root_g);

  GValue *toolchain_g;
  if (!check_key_type(top, G_TYPE_HASH_TABLE, "toolchain", &toolchain_g, "toolchain")) {
    return FALSE;
  }

  if (!yaml_to_toolchain(&builder->toolchain, g_value_get_boxed(toolchain_g))) {
    return FALSE;
  }

  GValue *modules_g;
  if (!check_key_type(top, G_TYPE_HASH_TABLE, "modules", &modules_g, "modules")) {
    return FALSE;
  }

  if (!yaml_to_modules(&builder->modules, g_value_get_boxed(modules_g))) {
    return FALSE;
  }

  return TRUE;
}

gboolean kikai_builderspec_parse(KikaiBuilderSpec* builder, const gchar *file) {
  GValue top = G_VALUE_INIT;
  return read_yaml(&top, file) && yaml_to_builder(builder, &top);
}
