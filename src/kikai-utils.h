#pragma once

#include <glib.h>
#include <gio/gio.h>

#define KIKAI_CRESET "\033[0m"
#define KIKAI_CBOLD "\033[1m"

#define KIKAI_CRED "\033[31m"
#define KIKAI_CCYAN "\033[36m"

void kikai_printstatus(const gchar *descr, const gchar *fmt, ...) G_GNUC_PRINTF(2, 3);
gboolean kikai_mkdir_parents(GFile *dir);
gchar *kikai_hash_bytes(const guchar *first, ...) G_GNUC_NULL_TERMINATED;
GFile *kikai_join(GFile *parent, const gchar *child, ...) G_GNUC_NULL_TERMINATED;

extern gchar kikai_db_missing;

gboolean kikai_db_load(GFile *storage);
void kikai_db_close();
gchar *kikai_db_get(const gchar *key);
gboolean kikai_db_set(const gchar *key, const gchar *value);
