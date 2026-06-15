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
 - Tombol `Device Status` untuk menjalankan status device dalam format JSON.
 - Tombol `Health` untuk menjalankan connection health dalam format JSON.
 - Tombol `Disconnect` untuk menghentikan proses `scrcpy.exe` aktif.
 - Panel log untuk stdout/stderr scrcpy dan ADB.


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
 - Klik `Health` dan pastikan log menampilkan output `--connection-health`.
 - Klik `Device Status` dan pastikan log menampilkan JSON status device.
 - Klik `Connect` dan pastikan mirror scrcpy terbuka.
 - Klik `Disconnect` dan pastikan proses mirror berhenti.


## Catatan implementasi

Launcher v1 sengaja belum memiliki tray, installer, profile editor visual, atau
file transfer UI. Fitur-fitur itu akan ditambahkan bertahap setelah dashboard
dasar dan process manager stabil.
