# VR Mobile Desktop Launcher

Dokumen ini menjelaskan launcher desktop Windows awal untuk fork VR Mobile.
Launcher ini adalah shell ringan di atas `scrcpy.exe`, jadi logic koneksi tetap
berada di core scrcpy dan bisa diuji lewat command-line.


## Status

Launcher awal tersedia sebagai target build:

```bash
build/desktop/vr-mobile.exe
```

Fitur v1:

 - Window dashboard sederhana.
 - Tombol `Connect` untuk menjalankan `scrcpy.exe --connect-manager`.
 - Tombol `Wireless Setup` untuk menjalankan `scrcpy.exe --wireless-setup`.
 - Tombol `Refresh Devices` untuk membaca list device dari connection health.
 - Device list untuk memilih serial saat beberapa Android terhubung.
 - Tombol `Device Status` untuk menampilkan status device dalam panel rapi.
 - Tombol `Disconnect` untuk menghentikan proses `scrcpy.exe` aktif.
 - Panel log untuk stdout/stderr scrcpy dan ADB.

Fitur v2:

 - Proses mirror dan proses utility dipisah.
 - `Refresh Devices` dan `Device Status` bisa dijalankan saat mirroring aktif.
 - Jika device dipilih dari list, `Connect` menjalankan
   `scrcpy --serial=<serial>`.
 - Jika tidak ada device dipilih, `Connect` tetap memakai auto mode
   `scrcpy --connect-manager`.
 - Field `Tailscale address` dan tombol `Tailscale Connect` untuk menjalankan
   `scrcpy --tailscale=<addr>` atau `scrcpy --tailscale` jika field kosong.

Fitur v3:

 - System tray icon dengan menu `Open dashboard`, `Connect`, `Disconnect`,
   `Refresh devices`, `Device status`, `Wireless setup`, `Start with Windows`,
   dan `Exit`.
 - Tombol `Start with Windows` yang menyimpan autostart ke registry Windows
   `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
 - Profile manager visual untuk refresh, create/edit, save, run, dan delete
   profile dari folder config `profiles`.
 - Area file transfer drag & drop. File yang di-drop akan masuk queue dan
   dikirim lewat command core `--send-file=<path>`.
 - File non-APK yang di-drop ke panel launcher atau window mirror akan masuk ke
   `/sdcard/Download/VR Phone Mirror/`. File `.apk` akan dicoba install.


## Cara build

Di environment MSYS2 yang sama dengan build scrcpy:

```bash
ninja -C build
```

Target ini aktif di Windows selama opsi Meson `compile_desktop=true`.


## Cara menjalankan

Dari root project:

```bash
./build/desktop/vr-mobile.exe
```

Launcher mencari `scrcpy.exe` di lokasi berikut:

 - Folder yang sama dengan `vr-mobile.exe`.
 - `../app/scrcpy.exe` relatif dari folder launcher.
 - `build/app/scrcpy.exe` relatif dari current working directory.


## Test

Test otomatis:

```bash
meson test -C build --print-errorlogs
```

Test yang ditambahkan untuk launcher:

 - `test_launcher_commands`

Smoke test manual:

 - Jalankan `build/desktop/vr-mobile.exe`.
 - Pastikan icon VR Mobile muncul di system tray.
 - Klik kanan tray icon dan coba `Open dashboard`, `Refresh devices`, dan
   `Exit`.
 - Centang `Start with Windows`, tutup lalu buka app, dan pastikan checkbox
   tetap mengikuti registry.
 - Klik `Refresh Devices` dan pastikan list Android terisi.
 - Pilih satu device lalu klik `Device Status`.
 - Pastikan panel status menampilkan serial, model, Android version, IP,
   screen, battery, dan storage.
  - Klik `Connect` dan pastikan mirror scrcpy terbuka.
  - Isi `Tailscale address` dengan IP Tailscale/MagicDNS device, lalu klik
    `Tailscale Connect`. Kosongkan field untuk mencoba last saved Tailscale
    address.
  - Saat mirror aktif, klik `Refresh Devices` atau `Device Status` dan pastikan
   command tetap bisa berjalan.
 - Buat profile baru, misalnya `xiaomi14`, isi argumen per baris, klik `Save`,
   lalu klik `Run`.
 - Drop file ke panel `File Transfer` dan pastikan log menampilkan proses
   `--send-file`.
 - Drop file langsung ke window mirror dan pastikan log menampilkan
   `successfully pushed to /sdcard/Download/VR Phone Mirror/`.
 - Untuk menerima file, masukkan path lengkap Android pada kolom File Transfer,
   misalnya `/sdcard/Download/VR Phone Mirror/screenshot.png`, klik `Receive`,
   lalu pilih lokasi penyimpanan PC pada dialog Save As.
 - Klik `Disconnect` dan pastikan proses mirror berhenti.


## Catatan implementasi

Launcher saat ini sengaja belum memiliki installer, clipboard UI, quick actions
UI, atau notification bridge. Fitur-fitur itu akan ditambahkan bertahap setelah
dashboard, tray, profile manager, dan file transfer stabil.
