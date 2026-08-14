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

Fitur v4:

 - Tombol dan tray menu `Companion bridge`.
 - Daftar file dari `/sdcard/Download/VR Mobile Companion/Outbox`.
 - `Receive selected` untuk menarik file tanpa mengetik path Android.
 - Daftar notifikasi aktif dengan penanda `[Reply]` jika quick reply tersedia.
 - Open notification action dan quick reply melalui authorized ADB shell.
 - Polling tiga detik dan Windows tray notification untuk event Android baru.
 - Bridge process terpisah dari mirror dan utility command.

Fitur v5:

 - Tombol `File Manager` membuka browser file Android untuk device yang dipilih.
 - Navigasi aman dibatasi ke `/sdcard` dan seluruh subfolder yang dapat diakses
   oleh ADB shell, termasuk `Android/data` dan `Android/obb` jika firmware
   device mengizinkan.
 - Address bar, tombol `Go`, `Up`, `Refresh`, dan double-click folder.
 - Kolom nama, tipe, ukuran, dan waktu modifikasi.
 - Download file atau folder dari Android ke Windows.
 - Upload file Windows ke folder Android yang sedang dibuka.
 - Create folder dan rename tanpa overwrite item existing.
 - Delete file/folder dengan dialog konfirmasi permanen.
 - Nama dan path dikodekan aman sebelum dikirim ke Android shell. Mutasi path
   di luar `/sdcard` ditolak oleh launcher.
 - File Manager memakai koneksi ADB yang sudah terotorisasi dan tidak meminta
   broad storage permission pada Companion APK.

Fitur v6:

 - Dashboard, Companion bridge, dan File Manager memakai visual dark modern
   yang konsisten.
 - Palet utama memakai graphite netral bergaya Codex: background `#151515`,
   surface `#1C1C1C`, elevated `#262626`, border `#3A3A3A`, teks `#ECECEC`,
   serta hijau lembut `#3F8F62` untuk aksi positif.
 - Tombol memiliki hierarki visual berdasarkan aksi utama, aksi positif,
   netral, atau berbahaya.
 - Ikon Fluent ditambahkan pada aksi Dashboard, Companion bridge, profile,
   transfer file, dan File Manager.
 - Logo `VR` berwarna indigo–teal dipakai pada window caption dan system tray.
 - Empty-state informatif ditampilkan saat daftar device atau Activity masih
   kosong, dengan background glow indigo–teal yang halus.
 - Spacing, tipografi, ukuran kontrol, heading panel, dan caption Windows
   diperbarui agar lebih mudah dipindai.
 - Dukungan DPI awareness menjaga tampilan tetap tajam dan ukuran window
   tetap proporsional pada display dengan Windows scaling.

Fitur v7:

 - Tombol `Internet` menjadi opsi koneksi keempat yang terpisah dari USB,
   Wi-Fi, dan Tailscale.
 - Window Internet Connect menyediakan signaling endpoint, QR pairing,
   fallback device code 10 digit, status, Test Link, dan Forget Phone.
 - Pairing invitation berlaku lima menit dan hanya dapat diklaim sekali.
 - Trusted link token disimpan menggunakan Windows DPAPI pada profil user.
 - Desktop melakukan polling pairing dan presence tanpa memblokir UI.
 - QR dibuat lokal oleh launcher; kode/token tidak dikirim ke layanan pembuat
   QR eksternal.
 - Signaling service v1 berada di folder `signaling` dan memiliki test
   end-to-end.
 - Media/control tunnel WebRTC/QUIC belum diaktifkan pada v7. USB, Wi-Fi, dan
   Tailscale tetap menjadi transport mirror produksi selama fase tersebut.

Fitur v8:

 - Shortcut langsung pada window mirror: `F1` WhatsApp, `F2` WhatsApp 2
   (XSpace/clone user 999), `F3` Facebook, `F4` Instagram, `F5` YouTube,
   dan `F10` untuk mengunci sekaligus mematikan layar.
- `Space` atau `Enter` membangunkan layar yang tertidur selama remote aktif.
- Jika Space ditekan saat frame remote hampir seluruhnya hitam, VR Mobile
  memeriksa dialog autentikasi Android. Bila tombol `Gunakan PIN`, `Use PIN`,
  atau padanan device credential ditemukan, tombol tersebut dipilih berdasarkan
  struktur UI—bukan koordinat tetap. PIN tetap harus dimasukkan pengguna dan
  tampilan `FLAG_SECURE` tetap tidak direkam.
- Pemeriksaan dialog dimulai di background saat frame aman berubah menjadi
  hitam. Posisi tombol yang telah diverifikasi disimpan per rotasi sehingga
  Space berikutnya dapat memilih `Gunakan PIN` dengan latensi minimal; jika
  tata letak belum pernah dikenali, pemindaian penuh menjadi fallback pertama.
- Shortcut global `Win+F` membuka pencarian aplikasi dari perangkat Android
  yang dipilih tanpa mengubah transport USB, Wi-Fi, atau Tailscale.
- Jika belum ada pilihan pada daftar device, `Win+F` otomatis memakai target
  Tailscale terakhir yang disimpan Connect Manager; dashboard dan Refresh tidak
  perlu dibuka lebih dahulu.
 - Pencarian aplikasi memakai popup dark, rounded, dan searchable bergaya
   Spotlight/Apple Find, sedikit transparan, dan dilengkapi tombol tutup.
 - Popup dimulai sebagai search bar ringkas. Daftar aplikasi baru mengembang
   saat pengguna mengklik atau mengetik, lalu menutup langsung setelah `Enter`.
 - Daftar aplikasi di-cache secara persisten per perangkat di LocalAppData,
   tampil tanpa menunggu, lalu diperbarui diam-diam di background.
 - Ikon launcher Android dirender menjadi PNG 96x96 dan disimpan per package
   pada `LocalAppData\VR Mobile\Cache\icons\<device>`. Finder memuat ikon lokal
   saat dibuka dan memperbaruinya terpisah di background.
 - Setelah aplikasi dipilih, fokus Windows kembali otomatis ke window mirror.
- Jika aplikasi dipilih dari Finder saat mirror tidak aktif, VR Mobile
  menjalankan Tailscale Connect otomatis, menunggu window scrcpy tersedia,
  lalu membawa remote ke depan tanpa menampilkan dashboard.
- Startup remote dan aplikasi digabung ke satu proses
  `scrcpy --tailscale --start-app=<package>`. Koneksi dan control channel remote
  dibangun terlebih dahulu, kemudian aplikasi dibuka melalui channel tersebut.
  Cara ini lebih cepat dan mencegah dua proses ADB saling berebut koneksi.
- Mode Tailscale memakai maksimum 20 FPS dan buffer video 70 ms untuk menjaga
  gerakan tetap cukup mulus sambil meredam jitter koneksi internet.
- Jika mirror sudah aktif, Finder memfokuskan window remote terlebih dahulu,
  lalu menjalankan perintah aplikasi pada device yang dipilih.
 - Finder berdiri sebagai top-level tool window independen; membuka atau
   mengetik di Finder tidak menaikkan dashboard VR Mobile ke depan.
 - Shortcut `Fn+M` membuka Tailscale Connect jika driver keyboard meneruskan
   tombol Fn ke Windows. `Ctrl+Alt+M` tersedia sebagai padanan universal.
 - Device Status menampilkan status display dan keyguard secara eksplisit,
   misalnya `ON / Awake` dan `OPEN / Unlocked`.


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
 - `test_launcher_json`
 - `test_file_manager_protocol`

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
 - Install companion APK, beri Notification Access, lalu aktifkan
   `Desktop bridge` pada app Android.
 - Klik `Refresh Devices`, pilih device, lalu klik `Companion`.
 - Pastikan file Outbox dan notifikasi aktif tampil pada panel terpisah.
 - Pilih file lalu klik `Receive selected`.
 - Pilih notifikasi bertanda `[Reply]`, masukkan teks, lalu klik `Send reply`.
 - Pilih satu device lalu klik `File Manager`.
 - Pastikan `/sdcard` tampil, buka `Download` dengan double-click, lalu coba
   tombol `Up` dan `Refresh`.
 - Upload satu file ke folder test, download kembali, kemudian bandingkan file.
 - Buat folder test, rename, lalu delete dan pastikan dialog konfirmasi muncul.
 - Pastikan address bar menolak `/data/data`, `/system`, path dengan `..`, dan
   path lain di luar `/sdcard`.
 - Klik `Disconnect` dan pastikan proses mirror berhenti.


## Catatan implementasi

Launcher saat ini belum memiliki installer, clipboard UI, quick actions UI,
filter notifikasi, atau native drag Outbox ke Explorer. File Manager desktop
sudah dapat browse dan mengelola shared storage melalui ADB, tetapi tidak dapat
membaca private app storage seperti `/data/data` tanpa root. Drag yang dimulai
dari File Manager Android bawaan tidak menyediakan content URI ke scrcpy;
gunakan `Share > VR Mobile Companion` sebagai jalur transfer Android ke Windows.
