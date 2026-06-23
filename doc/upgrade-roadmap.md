# Upgrade Roadmap

Dokumen ini mencatat arah awal upgrade untuk fork scrcpy ini. Fokus utamanya
adalah membuat pengalaman koneksi Android terasa seperti aplikasi desktop biasa:
jelas statusnya, minim perintah manual, dan ramah untuk user yang tidak ingin
membuka CMD hanya untuk mirror HP.

Roadmap ini dimulai sebagai dokumen arah upgrade. Beberapa fondasi v1 sekarang
sudah tersedia sebagai command-line flow agar bisa diuji sebelum UI native,
tray, dan companion APK dibangun.

Rencana eksekusi desktop/tray/APK yang lebih detail dicatat di
[Desktop Upgrade Execution Plan](desktop-upgrade-plan.md).


## Prinsip upgrade

 - Tetap memakai kemampuan scrcpy dan ADB yang sudah ada sebagai fondasi.
 - UI menjadi launcher visual untuk fitur yang sebelumnya dominan lewat command
   line.
 - Error teknis diterjemahkan ke instruksi yang bisa langsung dilakukan user.
 - State device terakhir disimpan agar flow berikutnya lebih cepat.
 - Fitur yang membutuhkan APK pendamping Android dicatat terpisah dari fitur
   yang bisa dibangun di sisi desktop saja.


## 1. One-click Connect Manager

**Status:** v1 foundation implemented as `--connect-manager[=auto|usb|wifi]`,
plus Tailscale connect via `--tailscale[=addr]`. The native one-click UI can
call these same flows.

**Masalah user:** user awam tidak tahu apakah HP sudah terbaca, masih
unauthorized, offline, atau harus memilih mode USB/Wi-Fi.

**Perilaku utama:**

 - Deteksi device via USB otomatis.
 - Tombol `Connect via USB`.
 - Tombol `Switch to Wi-Fi Mode`.
 - Simpan IP device terakhir.
 - Auto connect ketika HP dan PC ada di jaringan Wi-Fi yang sama.
 - Tampilkan status: `USB connected`, `Wi-Fi connected`, `ADB unauthorized`,
   dan `Device offline`.

**Integrasi scrcpy/ADB:** gunakan hasil `adb devices -l`, tipe serial USB atau
TCP/IP, `--select-usb`, `--select-tcpip`, dan `--tcpip` sebagai fondasi awal.

**Catatan implementasi awal:** fitur ini menjadi prioritas pertama karena
menjadi pintu masuk semua fitur lain. Mulai dari service kecil yang membaca
state ADB dan menyimpan device terakhir, lalu hubungkan ke UI connect manager.

**Implementasi v1:**

 - `--connect-manager` memilih USB, Wi-Fi, atau last Wi-Fi device secara
   otomatis.
 - `--connect-manager=usb` membatasi koneksi ke USB.
 - `--connect-manager=wifi` memakai device Wi-Fi yang sudah tersambung, atau
   mengaktifkan TCP/IP dari satu device USB.
 - `--tailscale=<addr>` menghubungkan ADB TCP/IP lewat IP Tailscale atau
   MagicDNS hostname dan menyimpan alamat terakhir.
 - `--tailscale` tanpa alamat memakai last saved Tailscale address.
 - Status utama sudah diterjemahkan di log: USB connected, Wi-Fi connected, ADB
   unauthorized, Device offline, no device, dan multiple devices.
 - Test unit ditambahkan untuk decision logic dan parser CLI.


## 2. Wireless Setup Wizard

**Status:** v1 foundation implemented as `--wireless-setup`. The native wizard
page can call this same flow later.

**Masalah user:** setup wireless scrcpy masih terasa teknikal karena user harus
tahu urutan `adb tcpip`, cari IP, `adb connect`, lalu menjalankan scrcpy.

**Perilaku utama:**

 - Halaman step-by-step: colok HP via USB, klik enable wireless mode, cabut
   kabel, lalu reconnect via IP.
 - App menjalankan command ADB otomatis.
 - Wizard memberi feedback di tiap step.

**Integrasi scrcpy/ADB:** scrcpy sudah mendukung `--tcpip` tanpa argumen untuk
mencari IP device dan mengaktifkan TCP/IP mode jika perlu. ADB command relevan:
`adb tcpip 5555`, `adb connect DEVICE_IP:5555`, dan `adb disconnect`.

**Catatan implementasi awal:** wizard sebaiknya memakai flow yang sama dengan
connect manager agar status dan retry tidak dibuat dua kali.

**Implementasi v1:**

 - `--wireless-setup` menjalankan step USB check, enable/reuse TCP/IP, reconnect
   via Wi-Fi, lalu simpan last Wi-Fi device.
 - Wizard keluar setelah setup selesai, sehingga user bisa cabut USB dan lanjut
   memakai `--connect-manager`.
 - Test parser CLI ditambahkan, dan flow real device sudah diuji ke Xiaomi
   Android 16.


## 3. Auto Reconnect

**Status:** v1 foundation implemented as `--auto-reconnect[=seconds]`.

**Masalah user:** koneksi wireless bisa putus karena Wi-Fi, device sleep, atau
ADB disconnect. App tidak boleh langsung membuat user mengulang dari awal.

**Perilaku utama:**

 - Auto retry setiap beberapa detik.
 - Notifikasi kecil: `Device disconnected, reconnecting...`.
 - Tombol `Reconnect`.
 - Simpan device terakhir untuk dipakai ulang saat retry.

**Integrasi scrcpy/ADB:** gunakan serial/IP terakhir, `adb connect`, dan opsi
scrcpy yang sama dengan sesi sebelumnya. Deteksi disconnect dari lifecycle
server/client scrcpy dan status ADB.

**Catatan implementasi awal:** v1 menjalankan ulang sesi hanya ketika scrcpy
keluar dengan status disconnected. Normal close atau time limit tidak memicu
retry.


## 4. Device Profiles

**Status:** v1 foundation implemented as `--profile=<name>` and
`--save-profile=<name>`.

**Masalah user:** setiap HP sering butuh setting yang berbeda, tetapi opsi
kualitas scrcpy masih tersebar di command line.

**Perilaku utama:**

 - Simpan profil per device.
 - Contoh profil: mode koneksi, max FPS, resolution, codec, audio, dan keep
   screen off.
 - Saat device yang sama terdeteksi, app menawarkan profil terakhir.

**Integrasi scrcpy/ADB:** map field profil ke opsi scrcpy seperti
`--max-fps`, `--max-size`, `--video-codec`, `--no-audio`, dan
`--turn-screen-off`.

**Catatan implementasi awal:** v1 menyimpan satu argumen CLI per baris di file
profil lokal. Ini sengaja memetakan profil ke opsi scrcpy existing, misalnya
`--connect-manager=wifi`, `--max-fps=60`, `--max-size=1920`,
`--video-codec=h265`, `--no-audio`, dan `--turn-screen-off`. UI pengelola
profil masih fase berikutnya.


## 5. Xiaomi Helper Mode

**Status:** v1 foundation implemented as `--xiaomi-helper`.

**Masalah user:** pada beberapa device Xiaomi, input keyboard/mouse gagal jika
`USB debugging (Security Settings)` belum aktif. User sering mengira app yang
error.

**Perilaku utama:**

 - Deteksi device Xiaomi.
 - Tampilkan instruksi khusus: aktifkan Developer Options, USB Debugging, USB
   Debugging Security Settings, lalu reboot jika perlu.
 - Tombol `Test Control` untuk mengecek mouse/keyboard sudah bisa dipakai.

**Integrasi scrcpy/ADB:** ambil manufacturer/model dari `adb devices -l` atau
`adb shell getprop ro.product.manufacturer`. Test control dapat memakai input
event scrcpy atau command ADB ringan yang tidak merusak state user.

**Catatan implementasi awal:** v1 mendeteksi manufacturer/brand Xiaomi, Redmi,
atau POCO lewat `getprop`, lalu menampilkan checklist dan command test control.
Helper ini bukan error fatal.


## 6. Tray App di Windows

**Status:** pending desktop shell/UI phase; command foundations are ready.

**Masalah user:** user ingin app tetap siap di background seperti Phone Link,
tanpa membuka window utama terus-menerus.

**Perilaku utama:**

 - Icon di system tray.
 - Menu klik kanan: connect last device, disconnect, record screen, file
   transfer, settings.
 - Opsi auto start with Windows.

**Integrasi scrcpy/ADB:** menu tray memanggil action yang sama dengan connect
manager, recorder, dan fitur transfer file. Recording tetap memakai dukungan
recording scrcpy yang sudah ada.

**Catatan implementasi awal:** belum dibuat penuh di core pass ini karena scrcpy
saat ini masih native CLI/SDL, belum punya desktop shell dengan tray lifecycle.
Command v1 seperti `--connect-manager`, `--profile`, `--send-file`,
`--quick-action`, dan recording scrcpy existing sudah disiapkan agar menu tray
nanti memanggil flow yang sama.


## 7. File Transfer Drag & Drop

**Status:** v2 implemented for desktop-to-phone drag and reverse transfer via
`--pull-file=<remote-path> --pull-target=<local-path>`.

**Masalah user:** transfer file lewat command line tidak natural untuk user
desktop.

**Perilaku utama:**

 - Area `Drop files here to send to phone`.
 - Drag file dari Windows ke app.
 - Kirim ke folder `Download/VR Phone Mirror`.
 - Install APK otomatis ketika file `.apk`.
 - File browser sederhana untuk copy file dari HP ke PC.

**Integrasi scrcpy/ADB:** scrcpy sudah punya file push. Untuk APK gunakan
`adb install`. Untuk browsing/copy balik gunakan command ADB seperti
`adb shell ls`, `adb pull`, dan path yang aman.

**Catatan implementasi awal:** v1 sudah bisa push file ke
`/sdcard/Download/VR Phone Mirror/` dan install APK dengan `adb install -r`.
V2 menambahkan tombol `Receive` di launcher untuk menarik file dari path Android
ke lokasi pilihan di PC melalui `adb pull`. Drag langsung dari item di File
Manager Android keluar dari window mirror masih membutuhkan companion APK agar
URI file yang dipilih dapat dikirim ke desktop; scrcpy core hanya menerima
video dan koordinat input.


## 8. Clipboard Sync Plus

**Status:** v1 foundation implemented as `--clipboard-history`.

**Masalah user:** copy-paste dua arah sudah ada, tetapi belum terasa seperti
fitur desktop yang jelas dan mudah dikontrol.

**Perilaku utama:**

 - Riwayat clipboard teks.
 - Tombol `Paste to phone`.
 - Tombol `Copy from phone`.
 - Sync teks otomatis.

**Integrasi scrcpy/ADB:** gunakan dukungan copy-paste scrcpy yang sudah ada di
control channel. Untuk file/gambar, catat sebagai fitur lanjutan yang mungkin
membutuhkan companion APK Android.

**Catatan implementasi awal:** scrcpy sudah punya clipboard autosync dan opsi
`--no-clipboard-autosync`. V1 menambahkan riwayat teks opt-in untuk clipboard
yang datang dari device ke PC. Tombol manual `Paste to phone` dan `Copy from
phone` masih perlu UI state, tetapi fondasi data history sudah ada.


## 9. Notification Bridge

**Status:** pending companion APK phase.

**Masalah user:** user ingin melihat notifikasi HP di Windows tanpa selalu
membuka layar mirror.

**Perilaku utama:**

 - Notifikasi HP muncul di Windows.
 - Klik notifikasi membuka app terkait di mirror.
 - Quick reply untuk WhatsApp/Telegram/SMS jika memungkinkan.
 - Filter aplikasi mana yang boleh tampil.

**Integrasi scrcpy/ADB:** fitur ini tidak cukup hanya dengan scrcpy core. Perlu
APK pendamping Android yang punya akses Notification Listener.

**Catatan implementasi awal:** belum dibuat di core pass ini karena membutuhkan
APK pendamping dengan akses Notification Listener dan protokol komunikasi baru.
Core scrcpy sekarang punya quick action `notification-panel`, `settings-panel`,
dan `collapse-panels`, tetapi bridge notifikasi Windows yang benar harus datang
dari APK pendamping.


## 10. Battery & Device Status Panel

**Status:** v1 command foundation implemented as `--device-status`.

**Masalah user:** user butuh tahu kondisi device tanpa membuka pengaturan HP
atau menjalankan command ADB.

**Perilaku utama:**

 - Tampilkan nama device, Android version, battery percent, charging status,
   Wi-Fi IP, screen resolution, storage free, dan ADB status.

**Integrasi scrcpy/ADB:** gunakan `adb shell getprop`, `adb shell dumpsys
battery`, `adb shell wm size`, `adb shell df`, dan query IP dari route/interface
device.

**Catatan implementasi awal:** v1 mencetak serial, model, Android version,
Wi-Fi IP, screen size, battery dump, dan storage info via ADB. Cache/refresh
UI masuk fase dashboard.


## 11. Quick Actions

**Status:** v1 command foundation implemented as `--quick-action=<action>`.

**Masalah user:** banyak kemampuan scrcpy berguna tetapi tersembunyi di shortcut
atau command line.

**Perilaku utama:**

 - Tombol cepat: lock phone, wake screen, turn screen off while mirroring,
   rotate screen, screenshot, start recording, open camera mirror, open
   specific app.

**Integrasi scrcpy/ADB:** pakai fitur scrcpy existing seperti screen off,
recording, camera mirroring, virtual display, dan start app. Untuk action device
pakai control message scrcpy atau command ADB yang sesuai.

**Catatan implementasi awal:** v1 mendukung `lock`, `wake`, `screen-off`,
`screen-on`, `rotate`, dan `screenshot` lewat ADB. Action lain seperti recording,
camera mirror, dan open app sudah punya opsi scrcpy existing dan perlu UI
launcher.


## 12. Connection Health / Error Translator

**Status:** v1 foundation implemented as `--connection-health` and shared
connect-manager hints.

**Masalah user:** pesan seperti `device unauthorized` atau `failed to connect`
tidak memberi instruksi praktis.

**Perilaku utama:**

 - Terjemahkan error teknis menjadi pesan yang bisa dilakukan user.
 - Contoh: `HP belum memberi izin debugging. Cek layar HP, pilih Allow USB
   debugging, lalu klik Retry.`
 - Contoh: `Wi-Fi device tidak ditemukan. Pastikan HP dan PC ada di jaringan
   yang sama.`

**Integrasi scrcpy/ADB:** mapping dari state ADB (`unauthorized`, `offline`,
missing device), error `adb connect`, dan error startup scrcpy.

**Catatan implementasi awal:** v1 menerjemahkan state ADB utama
(`unauthorized`, `offline`, no device, multiple devices) ke instruksi praktis.
Tahap berikutnya perlu menyatukan semua error ADB connect/startup ke tabel
translator yang lebih lengkap.


## Urutan implementasi awal yang disarankan

1. One-click Connect Manager.
2. Wireless Setup Wizard.
3. Connection Health / Error Translator.
4. Auto Reconnect.
5. Device Profiles.
6. Xiaomi Helper Mode.

Enam fitur pertama ini membentuk fondasi koneksi. Setelah koneksi stabil dan
mudah dipahami, fitur produktivitas seperti tray app, transfer file, clipboard,
notifikasi, status panel, dan quick actions bisa ditambahkan bertahap.
