#define UNICODE
#define _UNICODE

#include "vr_file_cache.h"

#include <shlobj.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

static void
safe_component(const char *input, WCHAR *output, size_t output_len) {
    WCHAR wide[256] = L"device";
    MultiByteToWideChar(CP_UTF8, 0, input && input[0] ? input : "device", -1,
                        wide, sizeof(wide) / sizeof(wide[0]));
    size_t pos = 0;
    for (size_t i = 0; wide[i] && pos + 1 < output_len; ++i) {
        WCHAR ch = wide[i];
        output[pos++] = (ch == L'<' || ch == L'>' || ch == L':'
                         || ch == L'"' || ch == L'/' || ch == L'\\'
                         || ch == L'|' || ch == L'?' || ch == L'*')
                        ? L'_'
                        : ch;
    }
    output[pos] = L'\0';
}

static bool
cache_root(WCHAR *path, size_t path_len) {
    WCHAR local[32768];
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
                                NULL, SHGFP_TYPE_CURRENT, local))) {
        return false;
    }
    WCHAR app[32768];
    WCHAR cache[32768];
    if (swprintf(app, sizeof(app) / sizeof(app[0]), L"%ls\\VR Mobile",
                 local) <= 0
            || swprintf(cache, sizeof(cache) / sizeof(cache[0]),
                        L"%ls\\Cache", app) <= 0) {
        return false;
    }
    CreateDirectoryW(app, NULL);
    CreateDirectoryW(cache, NULL);
    if (wcslen(cache) + 1 > path_len) {
        return false;
    }
    wcscpy(path, cache);
    return true;
}

static bool
index_path(const char *serial, WCHAR *path, size_t path_len) {
    WCHAR root[32768];
    WCHAR safe[256];
    if (!cache_root(root, sizeof(root) / sizeof(root[0]))) {
        return false;
    }
    safe_component(serial, safe, sizeof(safe) / sizeof(safe[0]));
    return swprintf(path, path_len, L"%ls\\files-%ls.vr-cache", root,
                    safe) > 0;
}

static void
clear_entries(struct vr_file_cache *cache) {
    for (size_t i = 0; i < cache->count; ++i) {
        free(cache->entries[i].path);
    }
    free(cache->entries);
    cache->entries = NULL;
    cache->count = 0;
    cache->capacity = 0;
}

void
vr_file_cache_init(struct vr_file_cache *cache, const char *serial) {
    memset(cache, 0, sizeof(*cache));
    snprintf(cache->serial, sizeof(cache->serial), "%s", serial ? serial : "");
}

void
vr_file_cache_destroy(struct vr_file_cache *cache) {
    clear_entries(cache);
    cache->serial[0] = '\0';
}

static bool
reserve_entries(struct vr_file_cache *cache, size_t required) {
    if (required <= cache->capacity) {
        return true;
    }
    size_t capacity = cache->capacity ? cache->capacity * 2 : 1024;
    while (capacity < required) {
        capacity *= 2;
    }
    void *entries = realloc(cache->entries, capacity * sizeof(cache->entries[0]));
    if (!entries) {
        return false;
    }
    cache->entries = entries;
    cache->capacity = capacity;
    return true;
}

static bool
add_entry(struct vr_file_cache *cache, enum vr_file_manager_entry_type type,
          uint64_t size, int64_t modified, const char *path) {
    if (!vr_file_manager_path_is_safe(path) || !strcmp(path, "/sdcard")) {
        return false;
    }
    if (!reserve_entries(cache, cache->count + 1)) {
        return false;
    }
    char *copy = _strdup(path);
    if (!copy) {
        return false;
    }
    cache->entries[cache->count++] = (struct vr_file_cache_entry) {
        .type = type, .size = size, .modified = modified, .path = copy,
    };
    return true;
}

bool
vr_file_cache_save(const struct vr_file_cache *cache) {
    WCHAR path[32768];
    WCHAR temp[32768];
    if (!index_path(cache->serial, path, sizeof(path) / sizeof(path[0]))
            || swprintf(temp, sizeof(temp) / sizeof(temp[0]), L"%ls.tmp",
                        path) <= 0) {
        return false;
    }
    FILE *file = _wfopen(temp, L"wb");
    if (!file) {
        return false;
    }
    fputs("VRFILES1\n", file);
    for (size_t i = 0; i < cache->count; ++i) {
        const struct vr_file_cache_entry *entry = &cache->entries[i];
        char type = entry->type == VR_FILE_MANAGER_ENTRY_DIRECTORY ? 'd'
                  : entry->type == VR_FILE_MANAGER_ENTRY_SYMLINK ? 'l' : 'f';
        fprintf(file, "%c\t%llu\t%lld\t%s\n", type,
                (unsigned long long) entry->size,
                (long long) entry->modified, entry->path);
    }
    bool ok = !ferror(file);
    if (fclose(file) != 0) {
        ok = false;
    }
    if (ok) {
        ok = MoveFileExW(temp, path, MOVEFILE_REPLACE_EXISTING
                                  | MOVEFILE_WRITE_THROUGH) != 0;
    }
    if (!ok) {
        DeleteFileW(temp);
    }
    return ok;
}

bool
vr_file_cache_load(struct vr_file_cache *cache) {
    WCHAR path[32768];
    if (!index_path(cache->serial, path, sizeof(path) / sizeof(path[0]))) {
        return false;
    }
    FILE *file = _wfopen(path, L"rb");
    if (!file) {
        return false;
    }
    clear_entries(cache);
    char *line = malloc(VR_FILE_MANAGER_MAX_PATH + 128);
    if (!line || !fgets(line, VR_FILE_MANAGER_MAX_PATH + 128, file)
            || strcmp(line, "VRFILES1\n")) {
        free(line);
        fclose(file);
        return false;
    }
    while (fgets(line, VR_FILE_MANAGER_MAX_PATH + 128, file)) {
        char *type = line;
        char *size = strchr(type, '\t');
        char *modified = size ? strchr(size + 1, '\t') : NULL;
        char *remote = modified ? strchr(modified + 1, '\t') : NULL;
        if (!size || !modified || !remote) {
            continue;
        }
        *size++ = '\0';
        *modified++ = '\0';
        *remote++ = '\0';
        remote[strcspn(remote, "\r\n")] = '\0';
        enum vr_file_manager_entry_type kind = *type == 'd'
            ? VR_FILE_MANAGER_ENTRY_DIRECTORY
            : *type == 'l' ? VR_FILE_MANAGER_ENTRY_SYMLINK
                           : VR_FILE_MANAGER_ENTRY_FILE;
        add_entry(cache, kind, _strtoui64(size, NULL, 10),
                  _strtoi64(modified, NULL, 10), remote);
    }
    free(line);
    fclose(file);
    return true;
}

static int
base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static bool
decode_base64(const char *input, char *output, size_t output_len) {
    size_t pos = 0;
    unsigned value = 0;
    int bits = -8;
    for (const unsigned char *p = (const unsigned char *) input; *p; ++p) {
        if (*p == '=') break;
        int digit = base64_value(*p);
        if (digit < 0) return false;
        value = (value << 6) | (unsigned) digit;
        bits += 6;
        if (bits >= 0) {
            if (pos + 1 >= output_len) return false;
            output[pos++] = (char) ((value >> bits) & 0xff);
            bits -= 8;
        }
    }
    output[pos] = '\0';
    return true;
}

bool
vr_file_cache_replace_all_output(struct vr_file_cache *cache,
                                 const char *output) {
    struct vr_file_cache fresh;
    vr_file_cache_init(&fresh, cache->serial);
    char *copy = _strdup(output ? output : "");
    if (!copy) return false;
    char *context = NULL;
    for (char *line = strtok_s(copy, "\r\n", &context); line;
         line = strtok_s(NULL, "\r\n", &context)) {
        if (strchr(line, '\t')) {
            char *size = strchr(line, '\t');
            char *modified = size ? strchr(size + 1, '\t') : NULL;
            char *path = modified ? strchr(modified + 1, '\t') : NULL;
            if (!size || !modified || !path) continue;
            *size++ = '\0'; *modified++ = '\0'; *path++ = '\0';
            enum vr_file_manager_entry_type kind = line[0] == 'd'
                ? VR_FILE_MANAGER_ENTRY_DIRECTORY
                : line[0] == 'l' ? VR_FILE_MANAGER_ENTRY_SYMLINK
                                 : VR_FILE_MANAGER_ENTRY_FILE;
            add_entry(&fresh, kind, _strtoui64(size, NULL, 10),
                      _strtoi64(modified, NULL, 10), path);
            continue;
        }
        char *type = line;
        char *size = strchr(type, '|');
        char *modified = size ? strchr(size + 1, '|') : NULL;
        char *encoded = modified ? strchr(modified + 1, '|') : NULL;
        if (!size || !modified || !encoded) continue;
        *size++ = '\0'; *modified++ = '\0'; *encoded++ = '\0';
        char path[VR_FILE_MANAGER_MAX_PATH];
        if (!decode_base64(encoded, path, sizeof(path))) continue;
        enum vr_file_manager_entry_type kind = *type == 'd'
            ? VR_FILE_MANAGER_ENTRY_DIRECTORY
            : *type == 'l' ? VR_FILE_MANAGER_ENTRY_SYMLINK
                           : VR_FILE_MANAGER_ENTRY_FILE;
        add_entry(&fresh, kind, _strtoui64(size, NULL, 10),
                  _strtoi64(modified, NULL, 10), path);
    }
    free(copy);
    clear_entries(cache);
    cache->entries = fresh.entries;
    cache->count = fresh.count;
    cache->capacity = fresh.capacity;
    fresh.entries = NULL;
    return true;
}

static bool
is_direct_child(const char *folder, const char *path, const char **name) {
    size_t len = strlen(folder);
    if (strncmp(folder, path, len) || path[len] != '/') return false;
    const char *rest = path + len + 1;
    if (!*rest || strchr(rest, '/')) return false;
    if (name) *name = rest;
    return true;
}

static bool
has_extension(const char *path, const char *const *extensions,
              size_t extension_count) {
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    const char *dot = strrchr(name, '.');
    if (!dot || dot == name || !dot[1]) {
        return false;
    }
    for (size_t i = 0; i < extension_count; ++i) {
        if (!_stricmp(dot + 1, extensions[i])) {
            return true;
        }
    }
    return false;
}

static bool
is_finder_user_file(const char *path) {
    static const char *const extensions[] = {
        /* Android packages and install bundles. */
        "apk", "apks", "apkm", "xapk",
        /* Images and design files. */
        "jpg", "jpeg", "png", "gif", "webp", "bmp", "svg", "heic",
        "heif", "tif", "tiff", "dng", "raw", "psd", "ai",
        /* Documents, spreadsheets, presentations, and ebooks. */
        "pdf", "doc", "docx", "odt", "rtf", "txt", "md",
        "xls", "xlsx", "xlsm", "ods", "csv", "tsv",
        "ppt", "pptx", "pps", "ppsx", "odp", "key",
        "epub", "mobi",
        /* Common audio, video, and subtitle files. */
        "mp3", "m4a", "aac", "wav", "flac", "ogg", "opus",
        "mp4", "m4v", "mkv", "avi", "mov", "webm", "3gp",
        "srt", "vtt",
        /* Archives. */
        "zip", "rar", "7z", "tar", "gz", "bz2", "xz",
        /* Game packages, saves, and common ROM images. */
        "game", "obb", "sav", "save", "rom", "iso", "nes", "sfc",
        "gba", "gbc", "nds", "3ds", "cia", "nsp", "xci",
        /* Keys and certificates explicitly requested by the user. */
        "pem", "p12", "pfx", "jks", "keystore", "cer", "crt", "pub",
    };
    if (!_strnicmp(path, "/sdcard/Android/data/", 21)
            || !_strnicmp(path, "/sdcard/Android/obb/", 20)
            || strstr(path, "/.cache/") || strstr(path, "/cache/")
            || strstr(path, "/Cache/") || strstr(path, "/tmp/")
            || strstr(path, "/Temp/") || strstr(path, "/logs/")
            || strstr(path, "/Logs/") || strstr(path, "/.")) {
        return false;
    }
    const char *name = strrchr(path, '/');
    if (name && name[1] == '.') {
        return false;
    }
    return has_extension(path, extensions,
                         sizeof(extensions) / sizeof(extensions[0]));
}

bool
vr_file_cache_replace_folder(struct vr_file_cache *cache, const char *folder,
                             const struct vr_file_manager_entry *entries,
                             size_t count) {
    size_t dst = 0;
    for (size_t i = 0; i < cache->count; ++i) {
        bool remove = is_direct_child(folder, cache->entries[i].path, NULL);
        size_t folder_len = strlen(folder);
        if (!remove && !strncmp(cache->entries[i].path, folder, folder_len)
                && cache->entries[i].path[folder_len] == '/') {
            const char *first = cache->entries[i].path + folder_len + 1;
            const char *slash = strchr(first, '/');
            if (slash) {
                size_t first_len = (size_t) (slash - first);
                bool parent_still_exists = false;
                for (size_t j = 0; j < count; ++j) {
                    if (entries[j].type == VR_FILE_MANAGER_ENTRY_DIRECTORY
                            && strlen(entries[j].name) == first_len
                            && !strncmp(entries[j].name, first, first_len)) {
                        parent_still_exists = true;
                        break;
                    }
                }
                remove = !parent_still_exists;
            }
        }
        if (remove) {
            free(cache->entries[i].path);
        } else {
            if (dst != i) cache->entries[dst] = cache->entries[i];
            ++dst;
        }
    }
    cache->count = dst;
    for (size_t i = 0; i < count; ++i) {
        char path[VR_FILE_MANAGER_MAX_PATH];
        if (vr_file_manager_join_path(folder, entries[i].name, path,
                                      sizeof(path))) {
            add_entry(cache, entries[i].type, entries[i].size,
                      entries[i].modified, path);
        }
    }
    return true;
}

size_t
vr_file_cache_list_folder(const struct vr_file_cache *cache, const char *folder,
                          struct vr_file_manager_entry *entries,
                          size_t max_entries) {
    size_t count = 0;
    for (size_t i = 0; i < cache->count && count < max_entries; ++i) {
        const char *name;
        if (!is_direct_child(folder, cache->entries[i].path, &name)
                || !vr_file_manager_name_is_safe(name)) continue;
        entries[count].type = cache->entries[i].type;
        entries[count].size = cache->entries[i].size;
        entries[count].modified = cache->entries[i].modified;
        snprintf(entries[count].name, sizeof(entries[count].name), "%s", name);
        ++count;
    }
    return count;
}

size_t
vr_file_cache_search_files(const struct vr_file_cache *cache,
                           const WCHAR *query, size_t *indices,
                           size_t max_indices) {
    char query_utf8[1024] = "";
    if (query && *query
            && !WideCharToMultiByte(CP_UTF8, 0, query, -1, query_utf8,
                                    sizeof(query_utf8), NULL, NULL)) {
        return 0;
    }
    size_t query_len = strlen(query_utf8);
    size_t count = 0;
    for (size_t i = 0; i < cache->count && count < max_indices; ++i) {
        if (cache->entries[i].type != VR_FILE_MANAGER_ENTRY_FILE
                || !is_finder_user_file(cache->entries[i].path)) continue;
        bool match = !query_len;
        if (!match) {
            for (const unsigned char *p =
                     (const unsigned char *) cache->entries[i].path;
                 *p; ++p) {
                size_t j = 0;
                while (j < query_len && p[j]) {
                    unsigned char left = p[j];
                    unsigned char right = (unsigned char) query_utf8[j];
                    if (left >= 'A' && left <= 'Z') left += 'a' - 'A';
                    if (right >= 'A' && right <= 'Z') right += 'a' - 'A';
                    if (left != right) break;
                    ++j;
                }
                if (j == query_len) { match = true; break; }
            }
        }
        if (match) indices[count++] = i;
    }
    return count;
}

static uint64_t
path_hash(const char *path) {
    uint64_t hash = UINT64_C(1469598103934665603);
    for (const unsigned char *p = (const unsigned char *) path; *p; ++p) {
        hash ^= *p;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

bool
vr_file_cache_download_path(const char *serial,
                            const struct vr_file_cache_entry *entry,
                            WCHAR *path, size_t path_len) {
    WCHAR root[32768], downloads[32768], device[32768], safe[256];
    if (!cache_root(root, sizeof(root) / sizeof(root[0]))) return false;
    safe_component(serial, safe, sizeof(safe) / sizeof(safe[0]));
    if (swprintf(downloads, sizeof(downloads) / sizeof(downloads[0]),
                 L"%ls\\downloads", root) <= 0
            || swprintf(device, sizeof(device) / sizeof(device[0]),
                        L"%ls\\%ls", downloads, safe) <= 0) return false;
    CreateDirectoryW(downloads, NULL);
    CreateDirectoryW(device, NULL);
    const char *name = strrchr(entry->path, '/');
    const char *dot = name ? strrchr(name + 1, '.') : NULL;
    WCHAR extension[32] = L"";
    if (dot && strlen(dot) < sizeof(extension) / sizeof(extension[0])) {
        MultiByteToWideChar(CP_UTF8, 0, dot, -1, extension,
                            sizeof(extension) / sizeof(extension[0]));
        for (WCHAR *p = extension; *p; ++p) {
            if (!((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z')
                    || (*p >= L'0' && *p <= L'9') || *p == L'.')) {
                extension[0] = L'\0'; break;
            }
        }
    }
    return swprintf(path, path_len, L"%ls\\%016llx%ls", device,
                    (unsigned long long) path_hash(entry->path), extension) > 0;
}

static bool
meta_path(const char *serial, const struct vr_file_cache_entry *entry,
          WCHAR *path, size_t path_len) {
    WCHAR download[32768];
    return vr_file_cache_download_path(serial, entry, download,
                                       sizeof(download) / sizeof(download[0]))
        && swprintf(path, path_len, L"%ls.meta", download) > 0;
}

bool
vr_file_cache_download_is_current(const char *serial,
                                  const struct vr_file_cache_entry *entry,
                                  WCHAR *path, size_t path_len) {
    WCHAR meta[32768];
    if (!vr_file_cache_download_path(serial, entry, path, path_len)
            || GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES
            || !meta_path(serial, entry, meta,
                          sizeof(meta) / sizeof(meta[0]))) return false;
    FILE *file = _wfopen(meta, L"rb");
    if (!file) return false;
    char remote[VR_FILE_MANAGER_MAX_PATH];
    unsigned long long size;
    long long modified;
    bool ok = fscanf(file, "%llu\t%lld\t%4095[^\n]", &size, &modified,
                     remote) == 3
           && size == entry->size && modified == entry->modified
           && !strcmp(remote, entry->path);
    fclose(file);
    return ok;
}

bool
vr_file_cache_mark_downloaded(const char *serial,
                              const struct vr_file_cache_entry *entry) {
    WCHAR meta[32768];
    if (!meta_path(serial, entry, meta, sizeof(meta) / sizeof(meta[0]))) {
        return false;
    }
    FILE *file = _wfopen(meta, L"wb");
    if (!file) return false;
    fprintf(file, "%llu\t%lld\t%s\n", (unsigned long long) entry->size,
            (long long) entry->modified, entry->path);
    return fclose(file) == 0;
}
