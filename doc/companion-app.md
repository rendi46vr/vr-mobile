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


## Status v1

Package: `com.vrmobile.companion`

Minimum Android: Android 10 (API 29)

Fitur yang sudah dibuat:

 - Activity utama untuk melihat status companion.
 - Target Android Share untuk satu atau beberapa file.
 - Salin file yang dibagikan ke outbox publik melalui MediaStore.
 - Sanitasi nama file agar aman untuk Android dan Windows.
 - Outbox di `/sdcard/Download/VR Mobile Companion/Outbox`.
 - Tombol untuk membuka pengaturan Notification Access.
 - `NotificationListenerService` yang menyimpan event terakhir secara lokal.
 - Unit test sanitasi nama file.
 - Tidak meminta permission Internet.

Yang belum dibuat pada v1:

 - Pairing companion dengan desktop.
 - Pengiriman event notifikasi ke Windows.
 - Daftar outbox otomatis pada launcher.
 - Native drag dari item Android langsung ke Windows Explorer.
 - Quick reply notifikasi.


## Alur Transfer File v1

1. Di File Manager Android, tekan lama file.
2. Pilih `Share`.
3. Pilih `VR Mobile Companion`.
4. Companion menyalin stream dari content URI ke:
   `/sdcard/Download/VR Mobile Companion/Outbox`.
5. Pada launcher desktop, masukkan path outbox lengkap lalu klik `Receive`.
6. Launcher menjalankan `adb pull` dan menampilkan dialog Save As.

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
V1 menyimpan package, title, text, dan timestamp terakhir di private
SharedPreferences. Belum ada data yang dikirim ke PC atau jaringan.


## Protokol Desktop yang Perlu Dikembangkan

Fase berikutnya harus menggunakan transport lokal yang dibatasi ADB, bukan
listener Wi-Fi terbuka:

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


## Native Drag Android ke Windows

Native drag keluar dari window mirror memerlukan beberapa tahap:

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


## Notification Bridge Roadmap

### Fase 1: local capture

Sudah tersedia. Event terakhir disimpan lokal setelah user memberi izin.

### Fase 2: desktop stream

 - Filter package allowlist/denylist.
 - Redaksi konten sensitif dan lock-screen visibility.
 - Stream hanya melalui ADB-forwarded authenticated socket.
 - Windows toast dengan app name, title, text, dan icon bila tersedia.

### Fase 3: actions

 - Klik toast membuka app terkait di mirror.
 - Jalankan notification action Android yang masih valid.
 - Quick reply hanya untuk `RemoteInput` yang didukung notification action.


## Security dan Privacy

 - V1 tidak memiliki permission Internet.
 - Notification Access selalu opt-in dari Settings Android.
 - Tidak ada broad storage permission; file hanya dibaca dari URI yang
   dibagikan user.
 - Desktop transport harus melalui ADB forwarding dan pairing token.
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
 - Tarik file outbox dengan tombol `Receive` pada launcher desktop.


## Definition of Done Companion v2

 - Pairing desktop/device harus user-approved dan tersimpan aman.
 - Launcher dapat menampilkan outbox tanpa user mengetik path.
 - Pull file menggunakan stable ID, bukan nama file mentah.
 - Notification filter dan Windows toast berfungsi.
 - Reconnect tidak menggandakan event atau transfer.
 - Unit test, Android lint, dan smoke test device lulus.
