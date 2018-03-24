#include <glib.h>
#include <glib/gprintf.h>

#include "kikai-utils.h"

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

gchar *kikai_hash_bytes(const guchar *first, ...) {
  va_list args;
  va_start(args, first);

  g_autoptr(GChecksum) sha = g_checksum_new(G_CHECKSUM_SHA256);
  g_checksum_update(sha, first, va_arg(args, gint));

  for (;;) {
    const guchar *item = va_arg(args, const guchar *);
    if (item == NULL) {
      break;
    }

    g_checksum_update(sha, item, va_arg(args, gint));
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
