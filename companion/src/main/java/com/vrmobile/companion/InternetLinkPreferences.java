package com.vrmobile.companion;

import android.content.Context;
import android.content.SharedPreferences;

import java.util.UUID;

public final class InternetLinkPreferences {
    private static final String PREFS = "vr_mobile_internet_link";
    private static final String KEY_SERVER_URL = "server_url";
    private static final String KEY_DEVICE_ID = "device_id";
    private static final String KEY_DESKTOP_ID = "desktop_id";
    private static final String KEY_DESKTOP_NAME = "desktop_name";
    private static final String KEY_AUTO_RECONNECT = "auto_reconnect";

    private InternetLinkPreferences() {
        // Utility class
    }

    public static String getServerUrl(Context context) {
        return preferences(context).getString(KEY_SERVER_URL, "");
    }

    public static void setServerUrl(Context context, String value) {
        preferences(context).edit().putString(KEY_SERVER_URL, value).apply();
    }

    public static String getOrCreateDeviceId(Context context) {
        SharedPreferences prefs = preferences(context);
        String existing = prefs.getString(KEY_DEVICE_ID, null);
        if (existing != null) {
            return existing;
        }
        String generated = UUID.randomUUID().toString();
        prefs.edit().putString(KEY_DEVICE_ID, generated).apply();
        return generated;
    }

    public static boolean isAutoReconnect(Context context) {
        return preferences(context).getBoolean(KEY_AUTO_RECONNECT, true);
    }

    public static void setAutoReconnect(Context context, boolean enabled) {
        preferences(context).edit()
                .putBoolean(KEY_AUTO_RECONNECT, enabled)
                .apply();
    }

    public static boolean saveTrustedDesktop(
            Context context, String desktopId, String desktopName,
            String linkToken) {
        if (!SecureTokenStore.save(context, linkToken)) {
            return false;
        }
        preferences(context).edit()
                .putString(KEY_DESKTOP_ID, desktopId)
                .putString(KEY_DESKTOP_NAME, desktopName)
                .apply();
        return true;
    }

    public static TrustedDesktop getTrustedDesktop(Context context) {
        SharedPreferences prefs = preferences(context);
        String id = prefs.getString(KEY_DESKTOP_ID, null);
        String name = prefs.getString(KEY_DESKTOP_NAME, null);
        String token = SecureTokenStore.load(context);
        return id == null || name == null || token == null
                ? null : new TrustedDesktop(id, name, token);
    }

    public static void forgetTrustedDesktop(Context context) {
        preferences(context).edit()
                .remove(KEY_DESKTOP_ID)
                .remove(KEY_DESKTOP_NAME)
                .apply();
        SecureTokenStore.clear(context);
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    public static final class TrustedDesktop {
        private final String id;
        private final String name;
        private final String token;

        private TrustedDesktop(String id, String name, String token) {
            this.id = id;
            this.name = name;
            this.token = token;
        }

        public String getId() {
            return id;
        }

        public String getName() {
            return name;
        }

        public String getToken() {
            return token;
        }
    }
}
