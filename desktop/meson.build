desktop_inc = include_directories('src')

desktop_common_src = [
    'src/file_manager_protocol.c',
    'src/internet_pairing_protocol.c',
    'src/launcher_commands.c',
    'src/launcher_json.c',
]

if host_machine.system() == 'windows'
    cc = meson.get_compiler('c')

    executable('vr-mobile',
               desktop_common_src + [
                   'src/windows/vr_file_manager.c',
                   'src/windows/vr_file_cache.c',
                   'src/windows/vr_app_search.c',
                   'src/windows/vr_internet_connect.c',
                   'src/windows/vr_mobile_launcher.c',
                   'src/windows/vr_theme.c',
               ],
               include_directories: desktop_inc,
               dependencies: [
                   cc.find_library('advapi32'),
                   cc.find_library('bcrypt'),
                   cc.find_library('comctl32'),
                   cc.find_library('comdlg32'),
                   cc.find_library('crypt32'),
                   cc.find_library('dwmapi'),
                   cc.find_library('gdi32'),
                   cc.find_library('gdiplus'),
                   cc.find_library('shell32'),
                   cc.find_library('uxtheme'),
                   cc.find_library('user32'),
                   cc.find_library('winhttp'),
               ],
               gui_app: true,
               install: true)
endif

if get_option('buildtype') == 'debug'
    exe = executable('test_launcher_commands',
                     desktop_common_src + [
                         'tests/test_launcher_commands.c',
                     ],
                     include_directories: desktop_inc)
    test('test_launcher_commands', exe)

    exe = executable('test_launcher_json',
                     [
                         'src/launcher_json.c',
                         'tests/test_launcher_json.c',
                     ],
                     include_directories: desktop_inc)
    test('test_launcher_json', exe)

    exe = executable('test_file_manager_protocol',
                     [
                         'src/file_manager_protocol.c',
                         'tests/test_file_manager_protocol.c',
                     ],
                     include_directories: desktop_inc)
    test('test_file_manager_protocol', exe)

    exe = executable('test_internet_pairing_protocol',
                     [
                         'src/internet_pairing_protocol.c',
                         'tests/test_internet_pairing_protocol.c',
                     ],
                     include_directories: desktop_inc)
    test('test_internet_pairing_protocol', exe)

    if host_machine.system() == 'windows'
        exe = executable('test_file_cache',
                         [
                             'src/file_manager_protocol.c',
                             'src/windows/vr_file_cache.c',
                             'tests/test_file_cache.c',
                         ],
                         include_directories: desktop_inc,
                         dependencies: cc.find_library('shell32'))
        test('test_file_cache', exe)
    endif
endif
