package com.vrmobile.companion;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

public final class StandardRemoteClient {
    private static final int TIMEOUT_MS = 12_000;

    private StandardRemoteClient() {
        // Utility class
    }

    public static void uploadFrame(
            String serverUrl, String deviceId, String token,
            byte[] jpeg) throws Exception {
        URL url = new URL(InternetPairing.endpoint(
                serverUrl, "/v1/relay/frame"));
        HttpURLConnection connection =
                (HttpURLConnection) url.openConnection();
        connection.setConnectTimeout(TIMEOUT_MS);
        connection.setReadTimeout(TIMEOUT_MS);
        connection.setRequestMethod("POST");
        connection.setRequestProperty("Content-Type", "image/jpeg");
        connection.setRequestProperty("Authorization", "Bearer " + token);
        connection.setRequestProperty("X-VR-Peer-ID", deviceId);
        connection.setFixedLengthStreamingMode(jpeg.length);
        connection.setDoOutput(true);
        try (OutputStream output = connection.getOutputStream()) {
            output.write(jpeg);
        }
        int status = connection.getResponseCode();
        InputStream stream = status >= 400
                ? connection.getErrorStream() : connection.getInputStream();
        if (stream != null) {
            stream.close();
        }
        connection.disconnect();
        if (status < 200 || status >= 300) {
            throw new IllegalStateException(
                    "Frame relay returned HTTP " + status);
        }
    }
}
