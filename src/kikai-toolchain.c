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

static gboolean needs_update(GFile *meta, gchar *current_hash) {
  if (!g_file_query_exists(meta, NULL)) {
    return TRUE;
  }

  g_autoptr(GError) error = NULL;
  g_autofree gchar *contents = NULL;

  if (!g_file_load_contents(meta, NULL, &contents, NULL, NULL, &error)) {
    return TRUE;
  }

  return strcmp(current_hash, contents) != 0;
}

gboolean kikai_toolchain_create(GFile *storage, KikaiToolchain *toolchain,
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

  for (int i = 0; i < spec->platforms->len; i++) {
    g_autoptr(GError) error = NULL;

    const gchar *platform = g_array_index(spec->platforms, gchar *, i);
    g_autoptr(GFile) target = kikai_join(storage, "toolchains", platform , NULL);
    g_autoptr(GFile) meta = g_file_get_child(target, ".kikai-meta");

    g_autofree gchar *current_hash = kikai_hash_bytes((guchar*)spec->api, -1,
                                                      (guchar*)spec->stl, -1,
                                                      (guchar*)spec->after, -1,
                                                      NULL);

    if (!needs_update(meta, current_hash)) {
      continue;
    }

    if (g_file_query_exists(meta, NULL)) {
      g_file_delete(meta, NULL, NULL);
    }

    kikai_printstatus("toolchain", "Creating %s toolchain (this may take a while)...",
                      platform);

    g_autoptr(GSubprocess) sp = g_subprocess_new(G_SUBPROCESS_FLAGS_NONE, &error,
                                      g_file_get_path(make_standalone_toolchain),
                                      "--arch", (gchar*)platform, "--api", spec->api,
                                      "--stl", spec->stl, "--force",
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

    if (!g_file_replace_contents(meta, current_hash, strlen(current_hash), "", 0,
                                 G_FILE_CREATE_NONE, NULL, NULL, &error)) {
      g_printerr("Failed to save %s toolchain metadata: %s", platform, error->message);
      return FALSE;
    }
  }

  return TRUE;
}
