#ifndef VR_FILE_MANAGER_PROTOCOL_H
#define VR_FILE_MANAGER_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VR_FILE_MANAGER_MAX_NAME 1024
#define VR_FILE_MANAGER_MAX_PATH 4096
#define VR_FILE_MANAGER_MAX_ENTRIES 5000

enum vr_file_manager_entry_type {
    VR_FILE_MANAGER_ENTRY_FILE,
    VR_FILE_MANAGER_ENTRY_DIRECTORY,
    VR_FILE_MANAGER_ENTRY_SYMLINK,
};

struct vr_file_manager_entry {
    enum vr_file_manager_entry_type type;
    uint64_t size;
    int64_t modified;
    char name[VR_FILE_MANAGER_MAX_NAME];
};

bool
vr_file_manager_path_is_safe(const char *path);

bool
vr_file_manager_name_is_safe(const char *name);

bool
vr_file_manager_join_path(const char *parent, const char *name, char *out,
                          size_t out_len);

size_t
vr_file_manager_parse_listing(const char *input,
                              struct vr_file_manager_entry *entries,
                              size_t max_entries);

#endif
