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
 - Klik `Refresh Devices` dan pastikan list Android terisi.
 - Pilih satu device lalu klik `Device Status`.
 - Pastikan panel status menampilkan serial, model, Android version, IP,
   screen, battery, dan storage.
 - Klik `Connect` dan pastikan mirror scrcpy terbuka.
 - Saat mirror aktif, klik `Refresh Devices` atau `Device Status` dan pastikan
   command tetap bisa berjalan.
 - Klik `Disconnect` dan pastikan proses mirror berhenti.


## Catatan implementasi

Launcher saat ini sengaja belum memiliki tray, installer, profile editor visual,
file transfer UI, clipboard UI, atau notification bridge. Fitur-fitur itu akan
ditambahkan bertahap setelah dashboard dasar dan process manager stabil.
