# VR Mobile Signaling Service

Service ini menyediakan rendezvous untuk opsi keempat `Internet Connect`.
Service tidak mengganti USB, Wi-Fi, Tailscale, ADB, atau scrcpy-server.

Fitur v1:

 - Pairing invitation 10 digit dengan masa berlaku lima menit.
 - QR memakai kode invitation yang sama dan dibuat lokal di desktop.
 - Claim sekali pakai dari Companion Android.
 - Trusted link token 256-bit untuk presence/reconnect.
 - Presence laptop/HP dengan timeout 45 detik.
 - State trusted link dapat disimpan ke file JSON berpermission `0600`.
 - Tidak ada dependency npm.

Jalankan untuk development:

```powershell
$env:VR_SIGNAL_HOST = "127.0.0.1"
$env:VR_SIGNAL_PORT = "8787"
$env:VR_SIGNAL_STATE = "C:\vr-mobile-data\signaling-state.json"
node signaling/server.mjs
```

Production wajib berada di belakang reverse proxy HTTPS. Set endpoint Companion
dan desktop ke URL publik, misalnya `https://connect.example.com`. Jangan
membuka port HTTP development langsung ke internet.

Endpoint:

 - `GET /health`
 - `POST /v1/pair/create`
 - `POST /v1/pair/claim`
 - `GET /v1/pair/status`
 - `POST /v1/presence`

Test:

```powershell
cd signaling
node --test
```

Signaling v1 baru menangani pairing dan presence. Transport video, audio,
control, clipboard, dan file melalui WebRTC/QUIC serta TURN fallback adalah
fase berikutnya. Sampai transport tersebut selesai, gunakan USB, Wi-Fi, atau
Tailscale untuk sesi mirror aktual.
