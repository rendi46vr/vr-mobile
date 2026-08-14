package com.vrmobile.companion;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

public final class InternetPairingClient {
    private static final int TIMEOUT_MS = 12_000;
    private static final int MAX_RESPONSE_BYTES = 65_536;

    private InternetPairingClient() {
        // Utility class
    }

    public static ClaimResult claim(
            String serverUrl, String code, String deviceId,
            String deviceName) throws Exception {
        JSONObject request = new JSONObject();
        request.put("code", code);
        request.put("deviceId", deviceId);
        request.put("deviceName", deviceName);
        JSONObject response = post(serverUrl, "/v1/pair/claim", request, null);
        return new ClaimResult(
                response.getString("desktopId"),
                response.getString("desktopName"),
                response.getString("linkToken"));
    }

    public static PresenceResult presence(
            String serverUrl, String deviceId, String token) throws Exception {
        JSONObject request = new JSONObject();
        request.put("role", "device");
        request.put("peerId", deviceId);
        JSONObject response = post(
                serverUrl, "/v1/presence", request, token);
        return new PresenceResult(
                response.optBoolean("peerOnline", false),
                response.optString("peerName", ""));
    }

    private static JSONObject post(
            String serverUrl, String path, JSONObject request,
            String bearerToken) throws Exception {
        URL url = new URL(InternetPairing.endpoint(serverUrl, path));
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);
        connection.setRequestMethod("POST");
        connection.setRequestProperty("Content-Type", "application/json");
        connection.setRequestProperty("Accept", "application/json");
        connection.setRequestProperty("User-Agent", "VR-Mobile-Companion/0.3");
        if (bearerToken != null) {
            connection.setRequestProperty(
                    "Authorization", "Bearer " + bearerToken);
        }
        connection.setDoOutput(true);
        byte[] body = request.toString().getBytes(StandardCharsets.UTF_8);
        connection.setFixedLengthStreamingMode(body.length);
        try (OutputStream output = connection.getOutputStream()) {
            output.write(body);
        }

        int status = connection.getResponseCode();
        InputStream stream = status >= 200 && status < 300
                ? connection.getInputStream() : connection.getErrorStream();
        String response = readLimited(stream);
        connection.disconnect();
        if (status < 200 || status >= 300) {
            String message = "Server returned HTTP " + status;
            try {
                message = new JSONObject(response).optString("error", message);
            } catch (Exception ignored) {
                // Keep the bounded generic error.
            }
            throw new IllegalStateException(message);
        }
        return new JSONObject(response);
    }

    private static String readLimited(InputStream input) throws Exception {
        if (input == null) {
            return "";
        }
        try (InputStream source = input;
                ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[4096];
            int total = 0;
            int read;
            while ((read = source.read(buffer)) != -1) {
                total += read;
                if (total > MAX_RESPONSE_BYTES) {
                    throw new IllegalStateException("Server response is too large");
                }
                output.write(buffer, 0, read);
            }
            return output.toString(StandardCharsets.UTF_8.name());
        }
    }

    public static final class ClaimResult {
        private final String desktopId;
        private final String desktopName;
        private final String linkToken;

        private ClaimResult(
                String desktopId, String desktopName, String linkToken) {
            this.desktopId = desktopId;
            this.desktopName = desktopName;
            this.linkToken = linkToken;
        }

        public String getDesktopId() {
            return desktopId;
        }

        public String getDesktopName() {
            return desktopName;
        }

        public String getLinkToken() {
            return linkToken;
        }
    }

    public static final class PresenceResult {
        private final boolean peerOnline;
        private final String peerName;

        private PresenceResult(boolean peerOnline, String peerName) {
            this.peerOnline = peerOnline;
            this.peerName = peerName;
        }

        public boolean isPeerOnline() {
            return peerOnline;
        }

        public String getPeerName() {
            return peerName;
        }
    }
}
