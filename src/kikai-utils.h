#pragma once

#include <glib.h>
#include <gio/gio.h>

#define KIKAI_CRESET "\033[0m"
#define KIKAI_CBOLD "\033[1m"

#define KIKAI_CRED "\033[31m"
#define KIKAI_CCYAN "\033[36m"

void kikai_printstatus(const gchar *descr, const gchar *fmt, ...);
gboolean kikai_mkdir_parents(GFile *dir);
gchar *kikai_hash_bytes(const guchar *first, ...);
