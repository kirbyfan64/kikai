#include <glib.h>
#include <glib/gprintf.h>

#include "kikai-utils.h"

void kikai_printstatus(const gchar *descr, const gchar *fmt, ...) {
  gboolean suffix = TRUE;
  if (descr[0] == '-') {
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
    g_printf("...\n");
  }
}

gboolean kikai_mkdir_parents(GFile *dir) {
  GError *error = NULL;
  gboolean success = TRUE;

  if (!g_file_make_directory_with_parents(dir, NULL, &error)) {
    if (!g_error_matches(error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
      g_printerr("%s", error->message);
      success = FALSE;
    }
    g_error_free(error);
  }

  return success;
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
