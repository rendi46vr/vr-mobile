desktop_inc = include_directories('src')

desktop_common_src = [
    'src/launcher_commands.c',
]

if host_machine.system() == 'windows'
    cc = meson.get_compiler('c')

    executable('vr-mobile',
               desktop_common_src + [
                   'src/windows/vr_mobile_launcher.c',
               ],
               include_directories: desktop_inc,
               dependencies: [
                   cc.find_library('gdi32'),
                   cc.find_library('user32'),
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
endif
