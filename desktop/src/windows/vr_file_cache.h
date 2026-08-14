#ifndef VR_FILE_CACHE_H
#define VR_FILE_CACHE_H

#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "file_manager_protocol.h"

#define VR_FILE_CACHE_SEARCH_LIMIT 512

struct vr_file_cache_entry {
    enum vr_file_manager_entry_type type;
    uint64_t size;
    int64_t modified;
    char *path;
};

struct vr_file_cache {
    struct vr_file_cache_entry *entries;
    size_t count;
    size_t capacity;
    char serial[256];
};

void vr_file_cache_init(struct vr_file_cache *cache, const char *serial);
void vr_file_cache_destroy(struct vr_file_cache *cache);
bool vr_file_cache_load(struct vr_file_cache *cache);
bool vr_file_cache_save(const struct vr_file_cache *cache);

bool vr_file_cache_replace_all_output(struct vr_file_cache *cache,
                                      const char *output);
bool vr_file_cache_replace_folder(struct vr_file_cache *cache,
                                  const char *folder,
                                  const struct vr_file_manager_entry *entries,
                                  size_t count);
size_t vr_file_cache_list_folder(const struct vr_file_cache *cache,
                                 const char *folder,
                                 struct vr_file_manager_entry *entries,
                                 size_t max_entries);
size_t vr_file_cache_search_files(const struct vr_file_cache *cache,
                                  const WCHAR *query, size_t *indices,
                                  size_t max_indices);

bool vr_file_cache_download_path(const char *serial,
                                 const struct vr_file_cache_entry *entry,
                                 WCHAR *path, size_t path_len);
bool vr_file_cache_download_is_current(
    const char *serial, const struct vr_file_cache_entry *entry,
    WCHAR *path, size_t path_len);
bool vr_file_cache_mark_downloaded(
    const char *serial, const struct vr_file_cache_entry *entry);

#endif
