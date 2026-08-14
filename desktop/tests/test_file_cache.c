#include <assert.h>
#include <string.h>

#include "windows/vr_file_cache.h"

int
main(void) {
    struct vr_file_cache cache;
    vr_file_cache_init(&cache, "test-device");
    const char *index =
        "drwxr-xr-x\t0\t100.0\t/sdcard/Download\n"
        "-rw-r--r--\t12\t101.5\t/sdcard/Download/a.txt\n"
        "-rw-r--r--\t99\t101.5\t/sdcard/Download/runtime.db\n"
        "-rw-r--r--\t99\t101.5\t/sdcard/Android/data/app/cache/photo.jpg\n"
        "drwxr-xr-x\t0\t102.0\t/sdcard/Old\n"
        "-rw-r--r--\t8\t103.0\t/sdcard/Old/stale.txt\n";
    assert(vr_file_cache_replace_all_output(&cache, index));
    assert(cache.count == 6);

    struct vr_file_manager_entry root[8];
    size_t root_count = vr_file_cache_list_folder(
        &cache, "/sdcard", root, sizeof(root) / sizeof(root[0]));
    assert(root_count == 2);

    size_t matches[8];
    assert(vr_file_cache_search_files(&cache, L"a.txt", matches, 8) == 1);
    assert(!strcmp(cache.entries[matches[0]].path,
                   "/sdcard/Download/a.txt"));
    assert(vr_file_cache_search_files(&cache, L"runtime", matches, 8) == 0);
    assert(vr_file_cache_search_files(&cache, L"photo", matches, 8) == 0);

    struct vr_file_manager_entry refreshed = {
        .type = VR_FILE_MANAGER_ENTRY_DIRECTORY,
        .size = 0,
        .modified = 200,
        .name = "Download",
    };
    assert(vr_file_cache_replace_folder(&cache, "/sdcard", &refreshed, 1));
    assert(cache.count == 3);
    assert(vr_file_cache_search_files(&cache, L"stale", matches, 8) == 0);
    assert(vr_file_cache_search_files(&cache, L"a.txt", matches, 8) == 1);

    vr_file_cache_destroy(&cache);
    return 0;
}
