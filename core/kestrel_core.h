/* kestrel_core.h — C ABI of the Rust core (matches core/src/lib.rs) */
#ifndef KESTREL_CORE_H
#define KESTREL_CORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KestrelCore KestrelCore;

void kestrel_string_free(char *p);

/* lifecycle */
KestrelCore *kestrel_core_new(const char *profile_dir);
void kestrel_core_free(KestrelCore *core);

/* filtering */
int kestrel_check_url(KestrelCore *core, const char *url, const char *source_url, const char *req_type);
char *kestrel_cosmetic_json(KestrelCore *core, const char *url);
int kestrel_reload_filters(KestrelCore *core, const char *lists_json);
uint64_t kestrel_session_block_total(KestrelCore *core);
void kestrel_stats_flush(KestrelCore *core);
char *kestrel_stats_json(KestrelCore *core);
void kestrel_stats_clear(KestrelCore *core);
char *kestrel_filter_lists_json(KestrelCore *core);
void kestrel_filter_list_set_enabled(KestrelCore *core, const char *id, int enabled);

/* history */
void kestrel_history_add(KestrelCore *core, const char *url, const char *title, const char *host);
char *kestrel_history_query(KestrelCore *core, const char *search, int limit);
void kestrel_history_delete(KestrelCore *core, const char *url);
void kestrel_history_clear(KestrelCore *core);

/* bookmarks */
int kestrel_bookmark_toggle(KestrelCore *core, const char *url, const char *title);
int kestrel_is_bookmarked(KestrelCore *core, const char *url);
char *kestrel_bookmarks_json(KestrelCore *core);
void kestrel_bookmark_remove(KestrelCore *core, const char *url);
void kestrel_bookmark_rename(KestrelCore *core, const char *url, const char *title);

/* downloads */
void kestrel_download_add(KestrelCore *core, const char *id, const char *url, const char *path,
                          const char *filename, const char *mime, int64_t total);
void kestrel_download_update(KestrelCore *core, const char *id, int64_t received, int64_t total, const char *state);
void kestrel_download_set_path(KestrelCore *core, const char *id, const char *path, const char *filename);
char *kestrel_downloads_json(KestrelCore *core, int limit);
void kestrel_download_remove(KestrelCore *core, const char *id);
void kestrel_downloads_clear(KestrelCore *core);

/* settings */
void kestrel_setting_set(KestrelCore *core, const char *key, const char *value);
char *kestrel_setting_get(KestrelCore *core, const char *key);   /* null when unset */
char *kestrel_settings_all(KestrelCore *core);

/* permissions */
void kestrel_permission_set(KestrelCore *core, const char *host, const char *feature, const char *value);
char *kestrel_permission_get(KestrelCore *core, const char *host, const char *feature);
char *kestrel_permissions_json(KestrelCore *core);
void kestrel_permission_clear(KestrelCore *core, const char *host, const char *feature);

/* zoom */
void kestrel_zoom_set(KestrelCore *core, const char *host, double level);
double kestrel_zoom_get(KestrelCore *core, const char *host);

/* session */
void kestrel_session_save(KestrelCore *core, const char *data);
char *kestrel_session_load(KestrelCore *core);

/* speed dial */
char *kestrel_speeddial_json(KestrelCore *core);
void kestrel_speeddial_add(KestrelCore *core, const char *url, const char *title);
void kestrel_speeddial_remove(KestrelCore *core, const char *url);

/* utility */
char *kestrel_host_of(const char *url);

#ifdef __cplusplus
}
#endif
#endif /* KESTREL_CORE_H */
