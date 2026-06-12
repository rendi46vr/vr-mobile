#include "common.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "cli.h"
#include "options.h"

static void test_flag_version(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_flag_help(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "-v"};

    bool ok = scrcpy_parse_args(&args, 2, argv);
    assert(ok);
    assert(!args.help);
    assert(args.version);
}

static void test_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {
        "scrcpy",
        "--always-on-top",
        "--video-bit-rate", "5M",
        "--crop", "100:200:300:400",
        "--fullscreen",
        "--max-fps", "30",
        "--max-size", "1024",
        // "--no-control" is not compatible with "--turn-screen-off"
        // "--no-playback" is not compatible with "--fulscreen"
        "--port", "1234:1236",
        "--push-target", "/sdcard/Movies",
        "--record", "file",
        "--record-format", "mkv",
        "--serial", "0123456789abcdef",
        "--show-touches",
        "--turn-screen-off",
        "--prefer-text",
        "--window-title", "my device",
        "--window-x", "100",
        "--window-y", "-1",
        "--window-width", "600",
        "--window-height", "0",
        "--window-borderless",
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(opts->always_on_top);
    assert(opts->video_bit_rate == 5000000);
    assert(!strcmp(opts->crop, "100:200:300:400"));
    assert(opts->fullscreen);
    assert(!strcmp(opts->max_fps, "30"));
    assert(opts->max_size == 1024);
    assert(opts->port_range.first == 1234);
    assert(opts->port_range.last == 1236);
    assert(!strcmp(opts->push_target, "/sdcard/Movies"));
    assert(!strcmp(opts->record_filename, "file"));
    assert(opts->record_format == SC_RECORD_FORMAT_MKV);
    assert(!strcmp(opts->serial, "0123456789abcdef"));
    assert(opts->show_touches);
    assert(opts->turn_screen_off);
    assert(opts->key_inject_mode == SC_KEY_INJECT_MODE_TEXT);
    assert(!strcmp(opts->window_title, "my device"));
    assert(opts->window_x == 100);
    assert(opts->window_y == -1);
    assert(opts->window_width == 600);
    assert(opts->window_height == 0);
    assert(opts->window_borderless);
}

static void test_options2(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {
        "scrcpy",
        "--no-control",
        "--no-playback",
        "--record", "file.mp4", // cannot enable --no-playback without recording
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);

    const struct scrcpy_options *opts = &args.opts;
    assert(!opts->control);
    assert(!opts->video_playback);
    assert(!opts->audio_playback);
    assert(!strcmp(opts->record_filename, "file.mp4"));
    assert(opts->record_format == SC_RECORD_FORMAT_MP4);
}

static void test_connect_manager_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "--connect-manager"};

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_AUTO);

    args.opts = scrcpy_options_default;
    char *argv_wifi[] = {"scrcpy", "--connect-manager=wifi"};

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_wifi), argv_wifi);
    assert(ok);
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_WIFI);

    args.opts = scrcpy_options_default;
    char *argv_invalid[] = {"scrcpy", "--connect-manager=bluetooth"};

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_invalid), argv_invalid);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_conflict[] = {"scrcpy", "--connect-manager", "--tcpip"};

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_conflict), argv_conflict);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_conflict_wizard[] = {
        "scrcpy",
        "--connect-manager",
        "--wireless-setup",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_conflict_wizard),
                           argv_conflict_wizard);
    assert(!ok);
}

static void test_wireless_setup_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv[] = {"scrcpy", "--wireless-setup"};

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv), argv);
    assert(ok);
    assert(args.opts.wireless_setup);

    args.opts = scrcpy_options_default;
    char *argv_conflict_tcpip[] = {"scrcpy", "--wireless-setup", "--tcpip"};

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_conflict_tcpip),
                           argv_conflict_tcpip);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_conflict_list[] = {
        "scrcpy",
        "--wireless-setup",
        "--list-displays",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_conflict_list),
                           argv_conflict_list);
    assert(!ok);
}

static void test_vr_mobile_utility_options(void) {
    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv_auto_reconnect[] = {"scrcpy", "--auto-reconnect"};

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_auto_reconnect),
                                argv_auto_reconnect);
    assert(ok);
    assert(args.opts.auto_reconnect);
    assert(args.opts.auto_reconnect_delay == 3);

    args.opts = scrcpy_options_default;
    char *argv_auto_reconnect_delay[] = {
        "scrcpy",
        "--auto-reconnect=7",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_auto_reconnect_delay),
                           argv_auto_reconnect_delay);
    assert(ok);
    assert(args.opts.auto_reconnect);
    assert(args.opts.auto_reconnect_delay == 7);

    args.opts = scrcpy_options_default;
    char *argv_device_status[] = {
        "scrcpy",
        "--connect-manager",
        "--device-status",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_device_status),
                           argv_device_status);
    assert(ok);
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_AUTO);
    assert(args.opts.device_status);

    args.opts = scrcpy_options_default;
    char *argv_xiaomi_helper[] = {
        "scrcpy",
        "--connect-manager",
        "--xiaomi-helper",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_xiaomi_helper),
                           argv_xiaomi_helper);
    assert(ok);
    assert(args.opts.xiaomi_helper);

    args.opts = scrcpy_options_default;
    char *argv_quick_action[] = {
        "scrcpy",
        "--quick-action=wake",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_quick_action),
                           argv_quick_action);
    assert(ok);
    assert(args.opts.quick_action == SC_QUICK_ACTION_WAKE);

    args.opts = scrcpy_options_default;
    char *argv_notification_action[] = {
        "scrcpy",
        "--quick-action=notification-panel",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_notification_action),
                           argv_notification_action);
    assert(ok);
    assert(args.opts.quick_action == SC_QUICK_ACTION_NOTIFICATION_PANEL);

    args.opts = scrcpy_options_default;
    char *argv_invalid_action[] = {
        "scrcpy",
        "--quick-action=invalid",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_invalid_action),
                           argv_invalid_action);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_send_file[] = {
        "scrcpy",
        "--send-file=demo.txt",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_send_file), argv_send_file);
    assert(ok);
    assert(!strcmp(args.opts.send_file, "demo.txt"));

    args.opts = scrcpy_options_default;
    char *argv_health[] = {"scrcpy", "--connection-health"};

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_health), argv_health);
    assert(ok);
    assert(args.opts.connection_health);

    args.opts = scrcpy_options_default;
    char *argv_json_output[] = {
        "scrcpy",
        "--connection-health",
        "--output-format=json",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_json_output),
                           argv_json_output);
    assert(ok);
    assert(args.opts.connection_health);
    assert(args.opts.output_format == SC_OUTPUT_FORMAT_JSON);

    args.opts = scrcpy_options_default;
    char *argv_bad_output[] = {
        "scrcpy",
        "--output-format=xml",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_bad_output), argv_bad_output);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_clipboard_history[] = {
        "scrcpy",
        "--clipboard-history",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_clipboard_history),
                           argv_clipboard_history);
    assert(ok);
    assert(args.opts.clipboard_history);

    args.opts = scrcpy_options_default;
    char *argv_health_conflict[] = {
        "scrcpy",
        "--connection-health",
        "--connect-manager",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_health_conflict),
                           argv_health_conflict);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_reconnect_conflict[] = {
        "scrcpy",
        "--auto-reconnect",
        "--device-status",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_reconnect_conflict),
                           argv_reconnect_conflict);
    assert(!ok);

    args.opts = scrcpy_options_default;
    char *argv_utility_list_conflict[] = {
        "scrcpy",
        "--device-status",
        "--list-displays",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_utility_list_conflict),
                           argv_utility_list_conflict);
    assert(!ok);
}

static void test_device_profile_options(void) {
#ifdef _WIN32
    _putenv("VR_MOBILE_CONFIG_DIR=test-vr-mobile-config");
#else
    setenv("VR_MOBILE_CONFIG_DIR", "test-vr-mobile-config", true);
#endif

    struct scrcpy_cli_args args = {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };

    char *argv_save_profile[] = {
        "scrcpy",
        "--save-profile=xiaomi14",
        "--connect-manager=wifi",
        "--max-fps=60",
        "--max-size=1920",
        "--video-codec=h265",
        "--no-audio",
        "--turn-screen-off",
    };

    bool ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_save_profile),
                                argv_save_profile);
    assert(ok);
    assert(args.profile_saved);
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_WIFI);
    assert(!strcmp(args.opts.max_fps, "60"));
    assert(args.opts.max_size == 1920);
    assert(args.opts.video_codec == SC_CODEC_H265);
    assert(!args.opts.audio);
    assert(args.opts.turn_screen_off);

    args = (struct scrcpy_cli_args) {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };
    char *argv_load_profile[] = {
        "scrcpy",
        "--profile=xiaomi14",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_load_profile),
                           argv_load_profile);
    assert(ok);
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_WIFI);
    assert(!strcmp(args.opts.max_fps, "60"));
    assert(args.opts.max_size == 1920);
    assert(args.opts.video_codec == SC_CODEC_H265);
    assert(!args.opts.audio);
    assert(args.opts.turn_screen_off);

    args = (struct scrcpy_cli_args) {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };
    char *argv_override_profile[] = {
        "scrcpy",
        "--profile=xiaomi14",
        "--max-fps=30",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_override_profile),
                           argv_override_profile);
    assert(ok);
    assert(!strcmp(args.opts.max_fps, "30"));
    assert(args.opts.connect_manager == SC_CONNECT_MANAGER_WIFI);

    args = (struct scrcpy_cli_args) {
        .opts = scrcpy_options_default,
        .help = false,
        .version = false,
    };
    char *argv_bad_profile[] = {
        "scrcpy",
        "--profile=bad/name",
    };

    ok = scrcpy_parse_args(&args, ARRAY_LEN(argv_bad_profile),
                           argv_bad_profile);
    assert(!ok);
}

static void test_parse_shortcut_mods(void) {
    uint8_t mods;
    bool ok;

    ok = sc_parse_shortcut_mods("lctrl", &mods);
    assert(ok);
    assert(mods == SC_SHORTCUT_MOD_LCTRL);

    ok = sc_parse_shortcut_mods("rctrl,lalt", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_RCTRL | SC_SHORTCUT_MOD_LALT));

    ok = sc_parse_shortcut_mods("lsuper,rsuper,lctrl", &mods);
    assert(ok);
    assert(mods == (SC_SHORTCUT_MOD_LSUPER
                  | SC_SHORTCUT_MOD_RSUPER
                  | SC_SHORTCUT_MOD_LCTRL));

    ok = sc_parse_shortcut_mods("", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl+", &mods);
    assert(!ok);

    ok = sc_parse_shortcut_mods("lctrl,", &mods);
    assert(!ok);
}

int main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_flag_version();
    test_flag_help();
    test_options();
    test_options2();
    test_connect_manager_options();
    test_wireless_setup_options();
    test_vr_mobile_utility_options();
    test_device_profile_options();
    test_parse_shortcut_mods();
    return 0;
}
