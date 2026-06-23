package com.vrmobile.companion;

import android.annotation.SuppressLint;
import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.provider.MediaStore;

public final class OutboxRepository {
    private static final int MAX_ENTRIES = 128;
    private static final String RELATIVE_PATH =
            CompanionPreferences.OUTBOX_RELATIVE_PATH + "/";
    @SuppressLint("SdCardPath")
    private static final String DEVICE_PATH =
            "/sdcard/" + CompanionPreferences.OUTBOX_RELATIVE_PATH + "/";

    private OutboxRepository() {
        // Utility class
    }

    public static void appendJson(Context context, StringBuilder json) {
        ContentResolver resolver = context.getContentResolver();
        String[] projection = {
            MediaStore.MediaColumns._ID,
            MediaStore.MediaColumns.DISPLAY_NAME,
            MediaStore.MediaColumns.MIME_TYPE,
            MediaStore.MediaColumns.SIZE,
            MediaStore.MediaColumns.DATE_MODIFIED,
        };
        String selection = MediaStore.MediaColumns.RELATIVE_PATH + "=?";
        String order = MediaStore.MediaColumns.DATE_MODIFIED + " DESC";

        json.append('[');
        try (Cursor cursor = resolver.query(
                MediaStore.Downloads.EXTERNAL_CONTENT_URI, projection,
                selection, new String[]{RELATIVE_PATH}, order)) {
            int count = 0;
            while (cursor != null && cursor.moveToNext() && count < MAX_ENTRIES) {
                if (count > 0) {
                    json.append(',');
                }
                String name = cursor.getString(1);
                json.append('{')
                        .append("\"id\":").append(cursor.getLong(0))
                        .append(",\"name\":").append(BridgeJson.quote(name))
                        .append(",\"mime\":")
                        .append(BridgeJson.quote(cursor.getString(2)))
                        .append(",\"size\":").append(cursor.getLong(3))
                        .append(",\"modified\":").append(cursor.getLong(4))
                        .append(",\"path\":")
                        .append(BridgeJson.quote(DEVICE_PATH + name))
                        .append('}');
                ++count;
            }
        }
        json.append(']');
    }
}
