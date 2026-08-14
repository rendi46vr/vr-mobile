#include <assert.h>
#include <string.h>

#include "file_manager_protocol.h"

static void
test_safe_paths(void) {
    assert(vr_file_manager_path_is_safe("/sdcard"));
    assert(vr_file_manager_path_is_safe("/sdcard/Download"));
    assert(vr_file_manager_path_is_safe("/sdcard/Android/data/com.example"));
    assert(!vr_file_manager_path_is_safe("/"));
    assert(!vr_file_manager_path_is_safe("/data/data"));
    assert(!vr_file_manager_path_is_safe("/sdcard/"));
    assert(!vr_file_manager_path_is_safe("/sdcard/../data"));
    assert(!vr_file_manager_path_is_safe("/sdcard/Download/./file"));
    assert(!vr_file_manager_path_is_safe("/sdcard//Download"));
}

static void
test_join_path(void) {
    char path[256];
    assert(vr_file_manager_join_path("/sdcard/Download", "demo file.txt",
                                     path, sizeof(path)));
    assert(!strcmp(path, "/sdcard/Download/demo file.txt"));
    assert(!vr_file_manager_join_path("/sdcard", "..", path, sizeof(path)));
    assert(!vr_file_manager_join_path("/data", "demo", path, sizeof(path)));
    assert(!vr_file_manager_join_path("/sdcard", "bad/name", path,
                                      sizeof(path)));
}

static void
test_listing_parser(void) {
    const char *listing =
        "d|4096|1700000000|Rm9sZGVyIHdpdGggc3BhY2Vz\n"
        "f|12345|1700000001|cGhvdG8uanBn\n"
        "l|12|1700000002|bGluaw==\n"
        "x|0|0|aWdub3Jl\n"
        "f|bad|0|aWdub3Jl\n";

    struct vr_file_manager_entry entries[8];
    size_t count = vr_file_manager_parse_listing(listing, entries, 8);
    assert(count == 3);
    assert(entries[0].type == VR_FILE_MANAGER_ENTRY_DIRECTORY);
    assert(!strcmp(entries[0].name, "Folder with spaces"));
    assert(entries[1].type == VR_FILE_MANAGER_ENTRY_FILE);
    assert(entries[1].size == 12345);
    assert(!strcmp(entries[1].name, "photo.jpg"));
    assert(entries[2].type == VR_FILE_MANAGER_ENTRY_SYMLINK);
}

int
main(void) {
    test_safe_paths();
    test_join_path();
    test_listing_parser();
    return 0;
}
