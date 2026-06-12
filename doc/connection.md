# Connection

## Selection

If exactly one device is connected (i.e. listed by `adb devices`), then it is
automatically selected.

However, if there are multiple devices connected, you must specify the one to
use in one of 4 ways:
 - by its serial:
   ```bash
   scrcpy --serial=0123456789abcdef
   scrcpy -s 0123456789abcdef   # short version

   # the serial is the ip:port if connected over TCP/IP (same behavior as adb)
   scrcpy --serial=192.168.1.1:5555
   ```
 - the one connected over USB (if there is exactly one):
   ```bash
   scrcpy --select-usb
   scrcpy -d   # short version
   ```
 - the one connected over TCP/IP (if there is exactly one):
   ```bash
   scrcpy --select-tcpip
   scrcpy -e   # short version
   ```
 - a device already listening on TCP/IP (see [below](#tcpip-wireless)):
   ```bash
   scrcpy --tcpip=192.168.1.1:5555
   scrcpy --tcpip=192.168.1.1        # default port is 5555
   ```

The serial may also be provided via the environment variable `ANDROID_SERIAL`
(also used by `adb`):

```bash
# in bash
export ANDROID_SERIAL=0123456789abcdef
scrcpy
```

```cmd
:: in cmd
set ANDROID_SERIAL=0123456789abcdef
scrcpy
```

```powershell
# in PowerShell
$env:ANDROID_SERIAL = '0123456789abcdef'
scrcpy
```


## VR Mobile connect manager

This fork adds a first version of a connect manager:

```bash
scrcpy --connect-manager
scrcpy --connect-manager=usb
scrcpy --connect-manager=wifi
```

Modes:

 - `auto` (default): prefer one USB device, then one Wi-Fi/TCP/IP device, then
   the last saved Wi-Fi device.
 - `usb`: connect only to one USB device.
 - `wifi`: connect to one Wi-Fi/TCP/IP device, or switch one USB device to
   TCP/IP mode when needed.

The connect manager prints user-facing status such as `USB connected`,
`Wi-Fi connected`, `ADB unauthorized`, and `Device offline`. When a Wi-Fi
connection succeeds, the last Wi-Fi serial or `ip:port` is saved locally for the
next auto connection attempt.

This is the code foundation for the future one-click UI. For now, it is exposed
as a command-line option so it can be tested and used from a desktop shortcut or
launcher.


## VR Mobile wireless setup wizard

This fork also adds a first version of the wireless setup wizard:

```bash
scrcpy --wireless-setup
```

The wizard:

1. checks that exactly one USB device is connected;
2. enables or reuses ADB TCP/IP mode on port `5555`;
3. reconnects to the device over Wi-Fi;
4. saves the Wi-Fi `ip:port` for the connect manager.

After the wizard succeeds, unplug USB and run:

```bash
scrcpy --connect-manager
```

If the Wi-Fi connection fails, verify that the phone and computer can reach each
other on the network. Some routers or guest networks block peer-to-peer traffic
between Wi-Fi clients.


## VR Mobile reconnect and utilities

This fork includes first command-line foundations for the next VR Mobile UX
features. These commands are intended to be called by a future native UI, tray
menu, or dashboard.

Auto reconnect:

```bash
scrcpy --connect-manager --auto-reconnect
scrcpy --connect-manager --auto-reconnect=5
```

When a mirror session exits because the device disconnected, scrcpy waits for
the configured delay and starts the same session again. A normal user close or
time limit does not trigger a reconnect.

Connection health:

```bash
scrcpy --connection-health
```

This prints detected ADB devices, their states, the last saved Wi-Fi device,
and practical hints for common states like unauthorized or offline.

Device status panel:

```bash
scrcpy --connect-manager --device-status
```

This prints the selected device serial, model, Android version, Wi-Fi IP,
screen size, battery dump, and storage information.

Xiaomi helper:

```bash
scrcpy --connect-manager --xiaomi-helper
```

This detects Xiaomi/Redmi/POCO devices and prints the debugging checklist for
keyboard and mouse control, including USB Debugging Security Settings.

Quick actions:

```bash
scrcpy --connect-manager --quick-action=wake
scrcpy --connect-manager --quick-action=lock
scrcpy --connect-manager --quick-action=screen-off
scrcpy --connect-manager --quick-action=screen-on
scrcpy --connect-manager --quick-action=rotate
scrcpy --connect-manager --quick-action=screenshot
scrcpy --connect-manager --quick-action=notification-panel
scrcpy --connect-manager --quick-action=collapse-panels
```

The screenshot action saves the image on the phone at
`/sdcard/Download/VR Phone Mirror/screenshot.png`.

File send:

```bash
scrcpy --connect-manager --send-file=README.md
scrcpy --connect-manager --send-file=app-release.apk
```

Normal files are pushed to `/sdcard/Download/VR Phone Mirror/`. APK files are
installed with `adb install -r`.

Device profiles:

```bash
scrcpy --save-profile=xiaomi14 --connect-manager=wifi --max-fps=60 --max-size=1920 --video-codec=h265 --turn-screen-off
scrcpy --profile=xiaomi14
scrcpy --profile=xiaomi14 --max-fps=30
```

Profiles are saved under the VR Mobile config directory, in the `profiles`
subdirectory. A profile file contains one command-line argument per line.
Arguments passed after `--profile` override values loaded from the profile.

Clipboard history:

```bash
scrcpy --connect-manager --clipboard-history
```

When enabled, incoming device clipboard text is appended to
`clipboard-history.txt` in the VR Mobile config directory. Clipboard autosync
still uses the normal scrcpy control channel.


## TCP/IP (wireless)

_Scrcpy_ uses `adb` to communicate with the device, and `adb` can [connect] to a
device over TCP/IP. The device must be connected on the same network as the
computer.

[connect]: https://developer.android.com/studio/command-line/adb.html#wireless


### Automatic

An option `--tcpip` allows to configure the connection automatically. There are
two variants.

If _adb_ TCP/IP mode is disabled on the device (or if you don't know the IP
address), connect the device over USB, then run:

```bash
scrcpy --tcpip   # without arguments
```

It will automatically find the device IP address and adb port, enable TCP/IP
mode if necessary, then connect to the device before starting.

If the device (accessible at 192.168.1.1 in this example) already listens on a
port (typically 5555) for incoming _adb_ connections, then run:

```bash
scrcpy --tcpip=192.168.1.1       # default port is 5555
scrcpy --tcpip=192.168.1.1:5555
```

Prefix the address with a '+' to force a reconnection:

```bash
scrcpy --tcpip=+192.168.1.1
```


### Manual

Alternatively, it is possible to enable the TCP/IP connection manually using
`adb`:

1. Plug the device into a USB port on your computer.
2. Connect the device to the same Wi-Fi network as your computer.
3. Get your device IP address, in Settings → About phone → Status, or by
   executing this command:

    ```bash
    adb shell ip route | awk '{print $9}'
    ```

4. Enable `adb` over TCP/IP on your device: `adb tcpip 5555`.
5. Unplug your device.
6. Connect to your device: `adb connect DEVICE_IP:5555` _(replace `DEVICE_IP`
with the device IP address you found)_.
7. Run `scrcpy` as usual.
8. Run `adb disconnect` once you're done.

Since Android 11, a [wireless debugging option][adb-wireless] allows you to
bypass having to physically connect your device to your computer.

[adb-wireless]: https://developer.android.com/studio/command-line/adb#wireless-android11-command-line


## Autostart

A small tool (by the scrcpy author) allows you to run arbitrary commands
whenever a new Android device is connected: [AutoAdb]. It can be used to start
scrcpy:

```bash
autoadb scrcpy -s '{}'
```

[AutoAdb]: https://github.com/rom1v/autoadb
