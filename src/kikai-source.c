#include <archive.h>
#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include <glib.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include <errno.h>
#include <math.h>
#include <string.h>
#include <sys/ioctl.h>

#include "kikai-source.h"
#include "kikai-utils.h"

static void progress(const gchar *descr, double fraction, int elapsed) {
  struct winsize win;
  ioctl(STDOUT_FILENO, TIOCGWINSZ, &win);

  g_autofree gchar *elapsed_s = g_strdup_printf("%02d:%02ds", (int)elapsed / 60,
                                                (int)elapsed % 60);
  int percent = fraction * 100;

  kikai_printstatus(descr, "% 4d%% %s", percent, elapsed_s);

  gint progress_size = win.ws_col - (strlen(descr) + 1 + 13);
  if (progress_size > 3) {
    gint bar_size = progress_size - 3;
    gint bar_complete = ceil(fraction * bar_size);
    gint bar_left = bar_size - bar_complete;

    g_printf(" [");
    for (int i = 0; i < bar_complete; i++) {
      g_printf("#");
    }
    for (int i = 0; i < bar_left; i++) {
      g_printf("-");
    }
    g_printf("]");
    fflush(stdout);
  }
}

static gint xferinfo(CURL *curl, curl_off_t dltotal, curl_off_t dlnow,
                     curl_off_t ultotal, curl_off_t ulnow) {
  double elapsed = 0;
  curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &elapsed);

  double fraction = (double)dlnow / (dltotal == 0 ? 1 : dltotal);
  progress("<download", fraction, elapsed);
  return 0;
}

static gint old_progress(CURL *curl, double dltotal, double dlnow, double ultotal,
                         double ulnow) {
  return xferinfo(curl, (curl_off_t)dltotal, (curl_off_t)dlnow, (curl_off_t)ultotal,
                  (curl_off_t)ulnow);
}

static size_t write_data(void *ptr, size_t size, size_t nitems, GOutputStream *os) {
  g_autoptr(GError) error = NULL;

  gssize written = g_output_stream_write(os, ptr, size * nitems, NULL, &error);
  if (written == -1) {
    g_printerr("Writing data to file: %s", error->message);
    return 0;
  }

  return size * nitems;
}

static gboolean download_file(const gchar *url, GFile *target, guint64 *size) {
  if (g_file_query_exists(target, NULL)) {
    g_file_delete(target, NULL, NULL);
  }

  g_autoptr(GError) error = NULL;
  g_autoptr(GFileIOStream) ios = g_file_create_readwrite(target, G_FILE_CREATE_NONE,
                                                         NULL, &error);
  if (ios == NULL) {
    g_printerr("Failed to create download target: %s", error->message);
    return FALSE;
  }

  CURL *curl = curl_easy_init();
  if (!curl) {
    g_printerr("Failed to initialize libcurl.");
    return FALSE;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

  curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, old_progress);
  curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, curl);

#if LIBCURL_VERSION_NUM >= 0x072000
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, curl);
#endif

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA,
                   g_io_stream_get_output_stream((GIOStream*)ios));

  CURLcode status = curl_easy_perform(curl);
  if (status == CURLE_OK) {
    curl_off_t curlsz = 0;
    curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &curlsz);
    *size = curlsz;
  }

  curl_easy_cleanup(curl);

  g_printf("\n");

  if (status != CURLE_OK) {
    g_printerr("Downloading file: %s", curl_easy_strerror(status));
    return FALSE;
  }

  return TRUE;
}

gboolean copy_archive_data(struct archive *reader, struct archive *writer) {
  for (;;) {
    gconstpointer buffer;
    gsize bufsz;
    la_int64_t offs;

    int rc = archive_read_data_block(reader, &buffer, &bufsz, &offs);
    if (rc == ARCHIVE_EOF) {
      break;
    } else if (rc < ARCHIVE_OK) {
      g_printerr("Reading data block from archive: %s", archive_error_string(reader));
      return FALSE;
    }

    rc = archive_write_data_block(writer, buffer, bufsz, offs);
    if (rc < ARCHIVE_OK) {
      g_printerr("Writing data block from archive: %s", archive_error_string(writer));
      return FALSE;
    }
  }

  return TRUE;
}

gboolean extract_file_to_cwd(GFile *archive, guint64 size, int strip_parents) {
  struct archive *reader = archive_read_new(), *writer = archive_write_disk_new();

  g_autoptr(GTimer) timer = g_timer_new();
  gboolean success = FALSE;

  if (archive_read_support_filter_all(reader) == ARCHIVE_FATAL ||
      archive_read_support_format_all(reader) == ARCHIVE_FATAL) {
    g_printerr("Loading archive formats: %s", archive_error_string(reader));
    goto done;
  }

  if (archive_write_disk_set_options(writer, ARCHIVE_EXTRACT_TIME |
                                             ARCHIVE_EXTRACT_PERM |
                                             ARCHIVE_EXTRACT_ACL |
                                             ARCHIVE_EXTRACT_FFLAGS) == ARCHIVE_FATAL ||
      archive_write_disk_set_standard_lookup(writer) == ARCHIVE_FATAL) {
    g_printerr("Setting writer options: %s", archive_error_string(writer));
    goto done;
  }

  if (archive_read_open_filename(reader, g_file_get_path(archive), 10240) !=
      ARCHIVE_OK) {
    g_printerr("Reading archive: %s", archive_error_string(reader));
    goto done;
  }

  g_timer_start(timer);

  for (;;) {
    struct archive_entry *entry;
    int rc = archive_read_next_header(reader, &entry);
    if (rc == ARCHIVE_EOF) {
      break;
    } else if (rc < ARCHIVE_OK) {
      g_printerr("Reading archive entry header: %s", archive_error_string(reader));
      if (rc == ARCHIVE_RETRY) {
        continue;
      } else if (rc < ARCHIVE_WARN) {
        goto done;
      }
    }

    const gchar *path = archive_entry_pathname(entry);
    if (path[0] == '\0') {
      g_printerr("Archive has entry with no pathname.");
      continue;
    }

    if (strip_parents != 0) {
      if (strip_parents < 0) {
        g_autofree gchar *basename = g_path_get_basename(path);
        archive_entry_copy_pathname(entry, basename);
      } else {
        g_auto(GStrv) parts = g_strsplit(path, G_DIR_SEPARATOR_S, -1);
        guint nparts = g_strv_length(parts);
        if (parts[nparts - 1][0] == '\0') {
          nparts--;
        }

        if (nparts > strip_parents) {
          g_autofree gchar* stripped = g_build_filenamev(parts + strip_parents);
          archive_entry_copy_pathname(entry, stripped);
        } else {
          // Skip this entry.
          continue;
        }
      }
    }

    rc = archive_write_header(writer, entry);
    if (rc < ARCHIVE_OK) {
      g_printerr("Writing archive entry header: %s", archive_error_string(writer));
      goto done;
    }

    if (archive_entry_size(entry) > 0) {
      if (!copy_archive_data(reader, writer)) {
        goto done;
      }
    }

    rc = archive_write_finish_entry(writer);
    if (rc < ARCHIVE_OK) {
      g_printerr("Finishing archive entry: %s", archive_error_string(writer));
      if (rc < ARCHIVE_WARN) {
        goto done;
      }
    }

    la_int64_t completed = archive_filter_bytes(reader, -1);
    double fraction = (double)completed / size;
    int elapsed = g_timer_elapsed(timer, NULL);
    progress("<extract", fraction, elapsed);
  }

  success = TRUE;

  done:
  archive_read_close(reader);
  archive_read_free(reader);
  archive_write_close(writer);
  archive_write_free(writer);

  return success;
}

gboolean extract_file(GFile *archive, GFile *extracted, guint64 size,
                      int strip_parents) {
  gchar *orig_cwd = g_get_current_dir();
  if (g_chdir(g_file_get_path(extracted)) == -1) {
    g_printerr("Setting working directory: %s", strerror(errno));
    return FALSE;
  }

  gboolean status = extract_file_to_cwd(archive, size, strip_parents);
  g_chdir(orig_cwd);
  return status;
}

GFile *kikai_processsource(GFile *storage, gchar *module_id,
                           KikaiModuleSourceSpec *source) {
  g_autofree gchar *download_id = kikai_hash_bytes((guchar*)source->url, -1, NULL);
  g_autoptr(GFile) downloads = kikai_join(storage, "downloads", module_id, NULL);

  g_autoptr(GFile) extracted = kikai_join(storage, "extracted", module_id, download_id,
                                          NULL);
  g_autoptr(GFile) meta = g_file_get_child(extracted, ".kikai-meta");

  if (g_file_query_exists(meta, NULL)) {
    return g_steal_pointer(&extracted);
  }

  kikai_printstatus("source", "Processing: %s", source->url);

  if (!kikai_mkdir_parents(downloads)) {
    return NULL;
  }

  g_autoptr(GFile) download = g_file_get_child(downloads, download_id);
  guint64 size = 0;
  if (!download_file(source->url, download, &size)) {
    return NULL;
  }

  if (!kikai_mkdir_parents(extracted)) {
    return NULL;
  }
  if (!extract_file(download, extracted, size, source->strip_parents)) {
    return NULL;
  }

  g_autoptr(GError) error = NULL;
  if (!g_file_replace_contents(meta, "", 0, "", 0, G_FILE_CREATE_NONE, NULL, NULL,
      &error)) {
    g_printerr("Failed to save archive metadata: %s", error->message);
    return NULL;
  }

  return g_steal_pointer(&extracted);
}
