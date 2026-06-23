# Desktop Upgrade Execution Plan

Dokumen ini adalah rencana eksekusi lanjutan setelah fondasi CLI VR Mobile
selesai. Tujuannya adalah mengubah fork scrcpy ini dari tool command-line
menjadi aplikasi desktop Windows yang terasa siap dipakai user awam.


## Status fondasi saat ini

Fondasi yang sudah tersedia di core scrcpy fork:

 - `--connect-manager[=auto|usb|wifi]`
 - `--wireless-setup`
 - `--auto-reconnect[=seconds]`
 - `--profile=<name>` dan `--save-profile=<name>`
 - `--xiaomi-helper`
 - `--send-file=<path>`
 - `--clipboard-history`
 - `--device-status`
 - `--quick-action=<action>`
 - `--connection-health`

Fondasi ini sengaja dibuat sebagai command-line flow agar bisa dites sekarang,
dan nanti bisa dipanggil oleh UI desktop, tray menu, atau companion APK.


## Target besar

1. Buat desktop shell Windows untuk VR Mobile.
2. Tambahkan system tray dan background process manager.
3. Buat dashboard koneksi dan device status.
4. Buat profile manager visual.
5. Buat file transfer UI.
6. Buat clipboard history UI.
7. Buat Android companion APK untuk notification bridge.
8. Buat installer Windows.


## 1. Windows Desktop Shell

**Tujuan:** menyediakan aplikasi desktop sebagai pintu masuk utama, bukan
memaksa user menjalankan command.

**Rekomendasi teknis awal:**

 - Buat launcher terpisah dari core scrcpy.
 - Launcher memanggil `scrcpy.exe` dengan command yang sudah tersedia.
 - Prioritaskan Windows.
 - Hindari mengubah terlalu banyak core scrcpy untuk UI.

**Pilihan stack:**

 - `.NET WinUI/WPF`: cocok untuk Windows tray, installer, dan UI native.
 - `Tauri`: UI web ringan, tetap bisa panggil binary lokal.
 - `Electron`: paling cepat untuk UI, tetapi lebih berat.
 - Native C/Win32: paling ringan, tetapi UI lebih lambat dikembangkan.

**Rekomendasi:** mulai dari `.NET` atau `Tauri`, karena target awal Windows dan
fitur tray/settings lebih cepat dibuat di layer launcher.

**Output v1:**

 - Window dashboard sederhana.
 - Tombol connect/disconnect.
 - Log output scrcpy/ADB.
 - Process manager untuk menjalankan dan menghentikan `scrcpy.exe`.

**Status:** started. Target Windows launcher awal tersedia sebagai
`build/desktop/vr-mobile.exe`. Dokumentasi penggunaan ada di
[`doc/desktop-launcher.md`](desktop-launcher.md).
Launcher v2 memisahkan proses mirror dan utility command, menambahkan device
list, selected serial connect, dan panel device status yang lebih rapi.

**Test v1:**

 - App bisa membuka dashboard.
 - App bisa menjalankan `scrcpy.exe --connect-manager`.
 - App bisa menghentikan proses mirror.
 - Error command tampil di UI.

**Test saat ini:**

 - `test_launcher_commands`
 - Smoke test proses launcher start tanpa crash.
 - Smoke test manual: `Refresh Devices`/`Device Status` dapat berjalan saat
   mirror aktif.
 - Smoke test manual: tray icon muncul dan menu dapat memanggil command utama.
 - Smoke test manual: profile manager dapat save/run/delete profile.
 - Smoke test manual: file drop menjalankan queue transfer lewat `--send-file`.


## 2. Tray App Windows

**Tujuan:** VR Mobile tetap siap di background seperti aplikasi desktop biasa.

**Fitur v1:**

 - System tray icon.
 - Menu:
   - Open dashboard
   - Connect last device
   - Wireless setup
   - Device status
   - Disconnect
   - Exit
 - Deteksi proses scrcpy yang sedang berjalan.
 - Auto start with Windows.

**Command yang dipakai:**

 - `scrcpy --connect-manager`
 - `scrcpy --wireless-setup`
 - `scrcpy --connection-health`
 - `scrcpy --connect-manager --device-status`

**Test:**

 - Tray muncul setelah app start.
 - Menu connect menjalankan mirror.
 - Menu disconnect menghentikan mirror.
 - Exit menutup tray dan child process dengan bersih.

**Status:** v1 implemented in the native Windows launcher.

**Catatan:** tombol close window menyembunyikan dashboard ke tray. Gunakan menu
tray `Exit` untuk benar-benar menutup app.


## 3. Dashboard Connect Manager

**Tujuan:** mengganti mental model "buka CMD" menjadi "klik Connect".

**Fitur v1:**

 - Status device: USB, Wi-Fi, unauthorized, offline, no device.
 - Tombol Connect.
 - Tombol Wireless Setup.
 - Tombol Reconnect.
 - Tombol Connection Health.

**Integrasi:**

 - Jalankan command fondasi dan parse log sederhana.
 - Untuk tahap berikutnya, pertimbangkan output machine-readable seperti JSON
   dari core command agar UI tidak parsing log manusia.

**Test:**

 - USB connected tampil benar.
 - Wi-Fi connected tampil benar.
 - Unauthorized menampilkan instruksi user-facing.
 - No device tidak crash.


## 4. Device Profile Manager UI

**Tujuan:** user bisa mengatur opsi scrcpy per HP tanpa menulis command.

**Fitur v1:**

 - List profile dari folder config `profiles`.
 - Create profile.
 - Edit profile.
 - Delete profile.
 - Run profile.

**Field awal:**

 - Connection mode: auto, USB, Wi-Fi.
 - Max FPS.
 - Max size/resolution.
 - Video codec: H.264, H.265, AV1.
 - Audio on/off.
 - Turn screen off.
 - Start app/package.

**Command yang dipakai:**

 - `scrcpy --save-profile=<name> ...`
 - `scrcpy --profile=<name>`

**Test:**

 - Profile tersimpan.
 - Profile bisa dijalankan.
 - Opsi manual override bekerja.

**Status:** v1 implemented in the launcher. Profile manager membaca dan menulis
file `.profile` di folder config `profiles`, lalu menjalankan profile dengan
`scrcpy --profile=<name>`.


## 5. File Transfer UI

**Tujuan:** user bisa drag file ke app untuk kirim ke HP.

**Fitur v1:**

 - Area `Drop files here`.
 - Multiple file queue.
 - Status per file: pending, sending, done, failed.
 - APK install confirmation.

**Command yang dipakai:**

 - `scrcpy --connect-manager --send-file=<path>`

**Status:** v1 implemented in the launcher. Drag & drop file masuk queue dan
dikirim satu per satu lewat `--send-file`. Jika ada device dipilih, launcher
memakai `--serial=<serial> --send-file=<path>`; jika tidak, launcher memakai
`--connect-manager --send-file=<path>`.

**Fitur lanjutan:**

 - File browser HP.
 - Pull file dari HP ke PC. **Status:** implemented v1 melalui input path Android
   dan dialog Save As di launcher.
 - Delete/rename file dari UI.

**Command tambahan yang perlu dibuat nanti:**

 - `--list-phone-files=<path>`
 - `--pull-file=<remote-path> --pull-target=<local-path>` **implemented**

Drag langsung dari UI File Manager Android ke Explorer tetap memerlukan
companion APK untuk menangkap dan meneruskan content URI Android.


## 6. Clipboard Sync Plus UI

**Tujuan:** clipboard terasa seperti fitur aplikasi, bukan shortcut tersembunyi.

**Fitur v1:**

 - Tampilkan `clipboard-history.txt`.
 - Search/filter history.
 - Copy item ke clipboard PC.
 - Clear history.

**Fondasi saat ini:**

 - `scrcpy --clipboard-history` menyimpan teks clipboard dari HP ke history.

**Fitur lanjutan:**

 - Paste selected text to phone.
 - Copy current phone clipboard on demand.

**Catatan teknis:** tombol manual perlu jalur control-channel saat sesi scrcpy
berjalan, atau command utility baru yang menjalankan server control minimal.


## 7. Notification Bridge

**Tujuan:** notifikasi HP tampil di Windows.

**Catatan penting:** fitur ini tidak bisa selesai hanya dari scrcpy core. Perlu
APK Android pendamping.

**Android companion APK v1:**

 - `NotificationListenerService`.
 - Permission request screen.
 - Filter app.
 - Kirim event notifikasi ke desktop.

**Desktop v1:**

 - Local listener dari companion APK.
 - Tampilkan Windows toast.
 - Klik toast membuka dashboard/mirror.

**Fondasi core saat ini:**

 - `--quick-action=notification-panel`
 - `--quick-action=settings-panel`
 - `--quick-action=collapse-panels`

**Test:**

 - APK menerima notifikasi Android.
 - Desktop menerima payload.
 - Windows toast muncul.
 - Filter app bekerja.


## 8. Installer Windows

**Tujuan:** user bisa install dan menjalankan VR Mobile tanpa setup manual.

**Isi installer:**

 - Desktop launcher.
 - `scrcpy.exe`.
 - `scrcpy-server`.
 - Icon dan shortcut Start Menu.
 - Optional autostart.
 - Uninstaller.

**Preflight check:**

 - ADB tersedia.
 - Driver Android/USB debugging guidance.
 - Permission warning untuk Xiaomi/HyperOS.


## Urutan eksekusi yang disarankan

1. Tambahkan output JSON untuk command status/health di core scrcpy.
   **Status:** started via `--output-format=json` for `--connection-health` and
   `--device-status`.
2. Buat desktop launcher minimal.
   **Status:** started via native Windows target `vr-mobile.exe`; launcher v2
   sudah punya dashboard device list.
3. Tambahkan process manager connect/disconnect.
   **Status:** started. Mirror process dan utility process sudah dipisah agar
   status/health tetap bisa berjalan ketika mirror aktif.
4. Tambahkan tray menu.
   **Status:** implemented in launcher v3.
5. Buat dashboard connect/status.
   **Status:** implemented in launcher v2/v3.
6. Buat profile manager UI.
   **Status:** implemented in launcher v3.
7. Buat file transfer UI.
   **Status:** implemented in launcher v3 for drag & drop send queue.
8. Buat clipboard history UI.
9. Buat Android companion APK.
10. Buat notification bridge desktop.
11. Buat installer Windows.


## Status fitur yang belum selesai

Belum selesai di desktop layer:

 - Clipboard history UI.
 - Quick actions UI.
 - Installer Windows.

Belum selesai karena butuh komponen tambahan:

 - Notification Bridge membutuhkan companion APK Android dengan
   `NotificationListenerService`.
 - Quick reply notification membutuhkan riset permission dan kompatibilitas
   aplikasi.
 - File browser HP membutuhkan command tambahan untuk list/pull remote file.


## Definition of Done

Setiap fitur dianggap selesai jika:

 - Ada command/UI yang bisa dipakai user.
 - Ada test unit atau smoke test manual yang jelas.
 - Error umum diterjemahkan ke instruksi user-facing.
 - Dokumentasi di `doc/connection.md` atau roadmap diperbarui.
 - Tidak merusak flow scrcpy existing.
