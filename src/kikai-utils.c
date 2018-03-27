#include <glib.h>
#include <glib/gprintf.h>

#include "kikai-utils.h"

#include <stdlib.h>
#include <string.h>

#include <gdbm.h>


void kikai_printstatus(const gchar *descr, const gchar *fmt, ...) {
  gboolean suffix = TRUE;
  if (descr[0] == '-' || descr[0] == '<') {
    if (descr[0] == '<') {
      g_printf("\r");
    }
    descr++;
    suffix = FALSE;
  }

  g_printf("[" KIKAI_CCYAN "%s" KIKAI_CRESET "] ", descr);

  va_list args;
  va_start(args, fmt);
  g_vprintf(fmt, args);
  va_end(args);

  g_printf(KIKAI_CRESET);
  if (suffix) {
    g_printf("\n");
  }

  fflush(stdout);
}

gboolean kikai_mkdir_parents(GFile *dir) {
  g_autoptr(GError) error = NULL;

  if (!g_file_make_directory_with_parents(dir, NULL, &error) &&
      !g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
    g_printerr("%s", error->message);
    return FALSE;
  }

  return TRUE;
}

gchar *kikai_hash_bytes(const gchar *first, ...) {
  va_list args;
  va_start(args, first);

  g_autoptr(GChecksum) sha = g_checksum_new(G_CHECKSUM_SHA256);
  g_checksum_update(sha, (guchar *)first, va_arg(args, gint));

  for (;;) {
    const gchar *item = va_arg(args, const gchar *);
    if (item == NULL) {
      break;
    }

    g_checksum_update(sha, (guchar *)item, va_arg(args, gint));
  }

  va_end(args);
  return g_strdup(g_checksum_get_string(sha));
}

GFile *kikai_join(GFile *parent, const gchar *child, ...) {
  va_list args;
  va_start(args, child);

  GFile *current = g_file_get_child(parent, child);
  for (;;) {
    const gchar *item = va_arg(args, const gchar *);
    if (item == NULL) {
      break;
    }

    GFile *previous = current;
    current = g_file_get_child(previous, item);
    g_object_unref(previous);
  }

  return current;
}

static GDBM_FILE kikai_db = NULL;
gchar kikai_db_missing = '\0';

gboolean kikai_db_load(GFile *storage) {
  g_autoptr(GFile) db = g_file_get_child(storage, "kikai.db");
  kikai_db = gdbm_open(g_file_get_path(db), 0, GDBM_WRCREAT | GDBM_SYNC, 0644, NULL);
  if (kikai_db == NULL) {
    g_printerr("Failed to load database: %s", gdbm_db_strerror(kikai_db));
    return FALSE;
  }

  atexit(kikai_db_close);
  return TRUE;
}

void kikai_db_close() {
  if (kikai_db != NULL) {
    gdbm_close(kikai_db);
    kikai_db = NULL;
  }
}

gchar *kikai_db_get(const gchar *key) {
  datum dkey = {.dptr = (gchar *)key, .dsize = strlen(key)};
  datum dvalue = gdbm_fetch(kikai_db, dkey);

  if (dvalue.dptr == NULL) {
    if (gdbm_errno == GDBM_ITEM_NOT_FOUND) {
      return &kikai_db_missing;
    } else {
      g_printerr("Failed to retrieve key %s: %s", key, gdbm_db_strerror(kikai_db));
      return NULL;
    }
  }

  return dvalue.dptr;
}

gboolean kikai_db_set(const gchar *key, const gchar *value) {
  datum dkey = {.dptr = (gchar *)key, .dsize = strlen(key)};
  datum dvalue = {.dptr = (gchar *)value, .dsize = strlen(value) + 1};

  if (gdbm_store(kikai_db, dkey, dvalue, GDBM_REPLACE) == -1) {
    g_printerr("Failed store key %s: %s", key, gdbm_db_strerror(kikai_db));
    return FALSE;
  }

  return TRUE;
}
