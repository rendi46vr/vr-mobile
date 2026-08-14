# VR Mobile Companion App

## Tujuan

VR Mobile Companion adalah APK Android khusus untuk fitur yang tidak dapat
diselesaikan oleh video/control channel scrcpy saja. Companion tidak mengganti
scrcpy-server. Keduanya memiliki peran berbeda:

 - `scrcpy-server` berjalan sementara sebagai proses shell untuk mirroring.
 - Companion adalah aplikasi Android yang di-install permanen dan hanya bekerja
   setelah user memberi izin yang relevan.

Target utamanya adalah transfer file Android ke PC, notification bridge,
pairing desktop, dan integrasi Android content URI.


## Status v3

Package: `com.vrmobile.companion`

Version: `0.3.0`

Minimum Android: Android 10 (API 29)

Fitur yang sudah dibuat:

 - Activity utama untuk melihat status companion.
 - Target Android Share untuk satu atau beberapa file.
 - Salin file yang dibagikan ke outbox publik melalui MediaStore.
 - Sanitasi nama file agar aman untuk Android dan Windows.
 - Outbox di `/sdcard/Download/VR Mobile Companion/Outbox`.
 - Tombol untuk membuka pengaturan Notification Access.
 - `NotificationListenerService` yang menyimpan event aktif beserta action.
 - Desktop bridge opt-in melalui ADB shell tanpa permission Internet.
 - Snapshot Outbox dan notifikasi untuk launcher Windows.
 - Panel Companion Windows dengan refresh otomatis setiap tiga detik.
 - Windows tray notification untuk event Android baru.
 - Open notification dan quick reply jika action Android mendukung
   `RemoteInput`.
 - Pilih file Outbox dari panel Windows tanpa mengetik path Android.
 - Unit test sanitasi nama file.
 - V3 meminta permission Internet hanya untuk opsi Internet Connect yang
   diaktifkan user. ADB bridge tetap dapat digunakan tanpa pairing internet.

Fitur Internet Connect v3:

 - Internet Connect tampil sebagai opsi keempat selain USB, Wi-Fi, dan
   Tailscale.
 - Scan QR memakai Google Code Scanner tanpa permission kamera permanen.
 - Fallback input device code 10 digit.
 - Signaling endpoint harus HTTPS; HTTP hanya diizinkan pada debug localhost.
 - Pairing code berlaku lima menit dan hanya dapat diklaim sekali.
 - Trusted desktop ID dan nama disimpan di private preferences.
 - Link token dienkripsi AES-256-GCM dengan key non-exportable dari Android
   Keystore.
 - Auto-reconnect melakukan presence check ketika Companion aktif dan internet
   kembali tersedia.
 - Forget trusted laptop menghapus metadata serta key/token lokal.
 - Transport WebRTC/QUIC untuk video/control masih fase berikutnya; fitur
   bridge ADB existing tidak berubah.

Yang belum dibuat:

 - Native drag dari item Android langsung ke Windows Explorer.
 - File browser khusus di companion untuk memilih file tanpa Android Share.
 - Filter allowlist/denylist notifikasi.
 - Riwayat notifikasi persisten setelah notifikasi Android ditutup.


## Alur Transfer File v2

1. Di File Manager Android, tekan lama file.
2. Pilih `Share`.
3. Pilih `VR Mobile Companion`.
4. Companion menyalin stream dari content URI ke:
   `/sdcard/Download/VR Mobile Companion/Outbox`.
5. Pada launcher desktop, pilih device lalu buka `Companion`.
6. Pilih file pada daftar Outbox dan klik `Receive selected`.
7. Launcher menjalankan `adb pull` dan menampilkan dialog Save As.

User tidak perlu memindahkan file secara manual ke folder Outbox. Aksi
`Share > VR Mobile Companion` memberi temporary content URI, lalu companion
yang membuat salinan stabil di Outbox.

Alur ini sengaja memakai Android Share. Window mirror hanya mengetahui piksel
dan koordinat sentuhan, sehingga tidak dapat mengetahui content URI item yang
sedang diseret dari aplikasi File Manager.


## Komponen Android

### MainActivity

Menampilkan:

 - Status pairing desktop.
 - Lokasi outbox.
 - File terakhir yang dibagikan.
 - Status Notification Access.
 - Notifikasi terakhir yang diterima secara lokal.

### ShareReceiverActivity

Menerima `ACTION_SEND` dan `ACTION_SEND_MULTIPLE`. URI dibaca hanya karena user
memulai aksi Share dan memberikan temporary URI permission. Companion tidak
meminta akses ke seluruh penyimpanan.

### OutboxWriter

Menyalin stream ke koleksi MediaStore Downloads dengan `IS_PENDING`, sehingga
file yang belum selesai tidak terlihat sebagai file lengkap. Nama file
disanitasi sebelum disimpan.

### NotificationBridgeService

Service hanya aktif setelah user memberi Notification Access dari Settings.
Service menyimpan metadata notifikasi aktif dan action yang masih valid di
memori proses. Launcher dapat membacanya hanya jika switch Desktop bridge
diaktifkan.

Quick reply hanya ditampilkan untuk action yang menyediakan free-form
`RemoteInput`. Balasan dikirim melalui `PendingIntent` milik aplikasi sumber,
sehingga kompatibilitas tetap ditentukan oleh aplikasi seperti WhatsApp atau
Telegram.


## Protokol Desktop v2

V2 memakai exported `ContentProvider` yang dilindungi permission sistem
`android.permission.DUMP` dan pemeriksaan UID shell. Launcher menjalankan:

```text
adb -s SERIAL shell content call \
  --uri content://com.vrmobile.companion.bridge \
  --method snapshot
```

Payload JSON dikodekan dengan Base64 URL-safe agar aman melewati output shell.
Bridge ADB tidak membuka socket Wi-Fi dan tidak memakai permission Internet.
APK v3 memiliki permission tersebut hanya untuk modul Internet Connect.

Metode yang tersedia:

 - `snapshot`: daftar Outbox dan notifikasi aktif.
 - `notification_open`: jalankan content intent notifikasi.
 - `notification_reply`: kirim teks melalui action `RemoteInput`.

ADB authorization berfungsi sebagai trust boundary v2. Pairing token terpisah
baru diperlukan jika kelak transport tidak lagi dibatasi oleh ADB shell.

Rancangan socket untuk fase lanjutan:

1. Companion membuka local socket Android dengan nama khusus VR Mobile.
2. Desktop memakai `adb forward tcp:PORT localabstract:SOCKET_NAME`.
3. Handshake membuat pairing token acak per desktop/device.
4. Setiap frame memakai version, type, request ID, payload length, dan payload.
5. Payload terstruktur memakai JSON UTF-8 dengan batas ukuran yang tegas.
6. File tetap ditransfer melalui `adb pull`; socket hanya membawa metadata dan
   status queue.

Message awal yang dibutuhkan:

 - `hello`: versi companion dan capability.
 - `pair_request` / `pair_confirm`: pairing user-approved.
 - `outbox_list`: stable file ID, display name, MIME type, size, timestamp.
 - `outbox_ack`: tandai item sudah diterima PC.
 - `notification_posted` / `notification_removed`.
 - `notification_action`: buka app atau quick reply jika action mendukung.


## Batas Native Drag Android ke Windows

Drag langsung dari File Manager Xiaomi atau aplikasi Android lain tidak dapat
diimplementasikan hanya dari scrcpy. Window mirror menerima piksel dan
koordinat input, bukan content URI, nama file, atau status item yang dipilih.

Alur stabil saat ini:

1. Pilih file pada File Manager Android.
2. Gunakan `Share > VR Mobile Companion`.
3. Buka panel Companion Windows.
4. Pilih file Outbox dan klik `Receive selected` atau klik dua kali.

Native drag dari panel Outbox Windows ke Explorer masih memerlukan cache lokal
dan implementasi Windows `IDataObject`/`IDropSource`. Drag yang benar-benar
berawal dari layar mirror memerlukan file browser milik companion agar kedua
sisi memiliki stable file ID yang sama.

Tahap implementasinya:

1. Companion menerima content URI melalui Share atau drop target overlay.
2. Companion menambahkan item ke outbox dan mengirim metadata ke desktop.
3. Desktop menarik file ke cache sementara menggunakan `adb pull`.
4. Window scrcpy/launcher membuat Windows `IDataObject` dan `IDropSource` dari
   file cache tersebut.
5. Explorer menerima drag sebagai file lokal biasa.
6. Cache dihapus setelah drop selesai atau melewati batas waktu.

Implementasi overlay Android harus opt-in. Permission Accessibility atau
`SYSTEM_ALERT_WINDOW` tidak boleh ditambahkan sebelum manfaat dan risikonya
jelas. Alur Share tetap menjadi fallback yang lebih aman.


## Notification Bridge

### Fase 1: local capture

Sudah tersedia. Notifikasi aktif diregistrasikan setelah user memberi izin.

### Fase 2: desktop stream

Sudah tersedia melalui polling ADB tiga detik dan Windows tray notification.

Peningkatan berikutnya:

 - Filter package allowlist/denylist.
 - Redaksi konten sensitif dan lock-screen visibility.
 - Stream hanya melalui ADB-forwarded authenticated socket.
 - Windows toast dengan app name, title, text, dan icon bila tersedia.

### Fase 3: actions

 - Panel dapat membuka app melalui content intent.
 - Quick reply tersedia untuk `RemoteInput` yang didukung notification action.
 - Klik Windows tray notification membuka panel Companion.


## Security dan Privacy

 - Bridge ADB tetap tidak mengirim data melalui internet. Permission Internet
   v3 dipakai hanya saat user mengatur dan memakai Internet Connect.
 - Notification Access selalu opt-in dari Settings Android.
 - Tidak ada broad storage permission; file hanya dibaca dari URI yang
   dibagikan user.
 - Desktop transport v2 hanya menerima caller ADB shell yang terotorisasi.
 - Payload, nama file, ukuran, jumlah queue, dan panjang teks harus dibatasi.
 - Isi notifikasi tidak boleh ditulis ke log produksi.
 - Quick reply harus menampilkan tujuan dan meminta konfirmasi user.


## Build

Gunakan JDK 17 dari Android Studio dan Android SDK 36:

```powershell
$env:JAVA_HOME = 'C:\Program Files\Android\Android Studio\jbr'
$env:ANDROID_HOME = "$env:LOCALAPPDATA\Android\Sdk"
$env:ANDROID_SDK_ROOT = $env:ANDROID_HOME
.\gradlew.bat :companion:testDebugUnitTest :companion:assembleDebug
```

APK debug dihasilkan di:

```text
companion/build/outputs/apk/debug/companion-debug.apk
```


## Install dan Smoke Test

```powershell
adb -s DEVICE_SERIAL install -r `
  companion/build/outputs/apk/debug/companion-debug.apk
adb -s DEVICE_SERIAL shell am start -n `
  com.vrmobile.companion/.MainActivity
```

Pada Xiaomi/HyperOS, jika muncul `INSTALL_FAILED_USER_RESTRICTED`, buka layar HP
dan aktifkan `Developer options > Install via USB`, lalu terima prompt instalasi
yang muncul. `USB debugging (Security settings)` mungkin juga perlu aktif.

Checklist manual:

 - App terbuka dan lokasi outbox tampil.
 - Share satu file dari File Manager ke VR Mobile Companion.
 - File muncul di `Download/VR Mobile Companion/Outbox`.
 - Share beberapa file dan pastikan semua tersalin.
 - Beri Notification Access dan pastikan status app berubah menjadi granted.
 - Aktifkan `Desktop bridge` di companion.
 - Refresh device pada launcher dan klik `Companion`.
 - Pastikan daftar Outbox dan notifikasi muncul.
 - Pilih file lalu klik `Receive selected`.
 - Pilih notifikasi bertanda `[Reply]`, isi teks, lalu klik `Send reply`.


## Definition of Done Companion v3

 - Pairing desktop/device harus user-approved dan tersimpan aman.
 - Native drag dari daftar Outbox Windows ke Explorer.
 - File browser companion dengan stable file selection.
 - Pull file menggunakan stable ID, bukan nama file mentah.
 - Notification filter dan Windows toast berfungsi.
 - Reconnect tidak menggandakan event atau transfer.
 - Unit test, Android lint, dan smoke test device lulus.
