package com.vrmobile.companion;

import android.content.Context;
import android.content.SharedPreferences;

public final class CompanionPreferences {
    public static final String OUTBOX_RELATIVE_PATH =
            "Download/VR Mobile Companion/Outbox";

    private static final String PREFS_NAME = "vr_mobile_companion";
    private static final String KEY_BRIDGE_ENABLED = "bridge_enabled";
    private static final String KEY_LAST_SHARED_FILE = "last_shared_file";
    private static final String KEY_LAST_SHARED_AT = "last_shared_at";
    private static final String KEY_LAST_NOTIFICATION_PACKAGE = "last_notification_package";
    private static final String KEY_LAST_NOTIFICATION_TITLE = "last_notification_title";
    private static final String KEY_LAST_NOTIFICATION_TEXT = "last_notification_text";
    private static final String KEY_LAST_NOTIFICATION_AT = "last_notification_at";

    private CompanionPreferences() {
        // Utility class
    }

    public static void saveSharedFile(Context context, String name, long timestamp) {
        preferences(context).edit()
                .putString(KEY_LAST_SHARED_FILE, name)
                .putLong(KEY_LAST_SHARED_AT, timestamp)
                .apply();
    }

    public static void saveNotification(Context context, String packageName,
                                        String title, String text, long timestamp) {
        preferences(context).edit()
                .putString(KEY_LAST_NOTIFICATION_PACKAGE, packageName)
                .putString(KEY_LAST_NOTIFICATION_TITLE, title)
                .putString(KEY_LAST_NOTIFICATION_TEXT, text)
                .putLong(KEY_LAST_NOTIFICATION_AT, timestamp)
                .apply();
    }

    public static boolean isBridgeEnabled(Context context) {
        return preferences(context).getBoolean(KEY_BRIDGE_ENABLED, false);
    }

    public static void setBridgeEnabled(Context context, boolean enabled) {
        preferences(context).edit().putBoolean(KEY_BRIDGE_ENABLED, enabled).apply();
    }

    public static Snapshot read(Context context) {
        SharedPreferences prefs = preferences(context);
        return new Snapshot(
                prefs.getString(KEY_LAST_SHARED_FILE, null),
                prefs.getLong(KEY_LAST_SHARED_AT, 0),
                prefs.getString(KEY_LAST_NOTIFICATION_PACKAGE, null),
                prefs.getString(KEY_LAST_NOTIFICATION_TITLE, null),
                prefs.getString(KEY_LAST_NOTIFICATION_TEXT, null),
                prefs.getLong(KEY_LAST_NOTIFICATION_AT, 0));
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE);
    }

    public static final class Snapshot {
        private final String lastSharedFile;
        private final long lastSharedAt;
        private final String lastNotificationPackage;
        private final String lastNotificationTitle;
        private final String lastNotificationText;
        private final long lastNotificationAt;

        private Snapshot(String lastSharedFile, long lastSharedAt,
                         String lastNotificationPackage, String lastNotificationTitle,
                         String lastNotificationText, long lastNotificationAt) {
            this.lastSharedFile = lastSharedFile;
            this.lastSharedAt = lastSharedAt;
            this.lastNotificationPackage = lastNotificationPackage;
            this.lastNotificationTitle = lastNotificationTitle;
            this.lastNotificationText = lastNotificationText;
            this.lastNotificationAt = lastNotificationAt;
        }

        public String getLastSharedFile() {
            return lastSharedFile;
        }

        public long getLastSharedAt() {
            return lastSharedAt;
        }

        public String getLastNotificationPackage() {
            return lastNotificationPackage;
        }

        public String getLastNotificationTitle() {
            return lastNotificationTitle;
        }

        public String getLastNotificationText() {
            return lastNotificationText;
        }

        public long getLastNotificationAt() {
            return lastNotificationAt;
        }
    }
}
