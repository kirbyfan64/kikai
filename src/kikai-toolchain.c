#include <glib.h>
#include <gio/gio.h>

#include <stdlib.h>
#include <string.h>

#include "kikai-toolchain.h"
#include "kikai-utils.h"

static gchar *find_ndk() {
  gchar *ndk = getenv("ANDROID_NDK");
  if (ndk != NULL) {
    return ndk;
  }
  ndk = getenv("ANDROID_NDK_ROOT");
  if (ndk != NULL) {
    return ndk;
  }

  g_printerr("Failed to locate Android NDK. Try setting ANDROID_NDK_ROOT or ANDROID_NDK"
             " to the NDK root directory.");
  return NULL;
}

static gboolean needs_update(gchar *current_hash) {
  g_autofree gchar *old_hash = kikai_db_get("toolchain");
  if (old_hash == NULL || old_hash == &kikai_db_missing) {
    g_steal_pointer(&old_hash);
    return TRUE;
  }
  return strcmp(current_hash, old_hash) != 0;
}

gboolean kikai_toolchain_create(GFile *storage, GArray *toolchains,
                                const KikaiToolchainSpec *spec) {
  gchar *ndk = find_ndk();
  if (ndk == NULL) {
    return FALSE;
  }

  g_autoptr(GFile) make_standalone_toolchain =
    kikai_join(g_file_new_for_path(ndk), "build", "tools",
               "make_standalone_toolchain.py", NULL);
  if (!g_file_query_exists(make_standalone_toolchain, NULL)) {
    g_printerr("%s does not exist.", g_file_get_path(make_standalone_toolchain));
    return FALSE;
  }

  const gchar *platform_names[] = {"arm", "x86"};
  const gchar *platform_triples[] = {"arm-linux-androideabi", "i686-linux-android"};

  for (int i = 0; i < spec->platforms->len; i++) {
    g_autoptr(GError) error = NULL;

    KikaiToolchainPlatform platform = g_array_index(spec->platforms,
                                                    KikaiToolchainPlatform, i);
    const gchar *platform_name = platform_names[platform];
    g_autoptr(GFile) target = kikai_join(storage, "toolchains", platform_name, NULL);

    const gchar *triple = platform_triples[platform];

    KikaiToolchain toolchain;
    toolchain.platform = g_strdup(platform_name);
    toolchain.path = g_strdup(g_file_get_path(target));

    g_autofree gchar *cc = g_strjoin("-", triple, "clang", NULL);
    toolchain.cc = g_build_filename(g_file_get_path(target), "bin", cc, NULL);

    g_autofree gchar *cxx = g_strjoin("-", triple, "clang++", NULL);
    toolchain.cxx = g_build_filename(g_file_get_path(target), "bin", cxx, NULL);

    toolchain.triple = g_strdup(triple);

    g_array_append_val(toolchains, toolchain);

    g_autofree gchar *current_hash = kikai_hash_bytes(spec->api, -1, spec->stl, -1,
                                                      spec->after, -1, NULL);

    if (!needs_update(current_hash)) {
      continue;
    }

    kikai_printstatus("toolchain", "Creating %s toolchain (this may take a while)...",
                      platform_name);

    g_autoptr(GSubprocess) sp = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
                                      g_file_get_path(make_standalone_toolchain),
                                      "--arch", (gchar*)platform_name,
                                      "--api", spec->api, "--stl", spec->stl, "--force",
                                      "--install-dir", g_file_get_path(target), NULL);
    if (sp == NULL) {
      g_printerr("Failed to spawn make_standalone_toolchain.py: %s", error->message);
      return FALSE;
    }

    if (!g_subprocess_wait_check(sp, NULL, &error)) {
      g_printerr("make_standalone_toolchain failed: %s", error->message);
      return FALSE;
    }

    if (spec->after != NULL) {
      gchar *args[] = {"/bin/sh", "-ec", (gchar*)spec->after, NULL};
      gint status;
      if (!g_spawn_sync(g_file_get_path(target), args, NULL, G_SPAWN_DEFAULT, NULL, NULL,
                        NULL, NULL, &status, &error)) {
        g_printerr("Failed to spawn toolchain.after: %s", error->message);
        return FALSE;
      }

      if (!g_spawn_check_exit_status(status, &error)) {
        g_printerr("toolchain.after failed: %s", error->message);
        return FALSE;
      }
    }

    if (!kikai_db_set("toolchain", current_hash)) {
      return FALSE;
    }
  }

  return TRUE;
}
