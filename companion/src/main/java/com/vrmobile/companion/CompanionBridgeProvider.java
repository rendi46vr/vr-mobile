package com.vrmobile.companion;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.BaseBundle;
import android.os.Binder;
import android.os.Bundle;
import android.os.Process;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.util.List;

public final class CompanionBridgeProvider extends ContentProvider {
    public static final String AUTHORITY = "com.vrmobile.companion.bridge";

    private static final int SHELL_UID = 2000;
    private static final String METHOD_SNAPSHOT = "snapshot";
    private static final String METHOD_OPEN = "notification_open";
    private static final String METHOD_REPLY = "notification_reply";

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Bundle call(String method, String argument, Bundle extras) {
        if (!isTrustedCaller()) {
            throw new SecurityException("VR Mobile bridge only accepts ADB shell");
        }
        if (!CompanionPreferences.isBridgeEnabled(getContext())) {
            return encodedResult("{\"version\":1,\"enabled\":false,"
                    + "\"error\":\"Enable Desktop bridge in the companion app\"}");
        }

        switch (method) {
            case METHOD_SNAPSHOT:
                return encodedResult(buildSnapshot());
            case METHOD_OPEN:
                return actionResult(NotificationRepository.open(
                        decodeExtra(extras, "key")));
            case METHOD_REPLY:
                String key = decodeExtra(extras, "key");
                String text = decodeExtra(extras, "text");
                int action = extras == null ? -1 : extras.getInt("action", -1);
                return actionResult(NotificationRepository.reply(
                        getContext(), key, action, text));
            default:
                throw new IllegalArgumentException("Unknown bridge method: " + method);
        }
    }

    private String buildSnapshot() {
        StringBuilder json = new StringBuilder(4096);
        json.append("{\"version\":1,\"enabled\":true,\"outbox\":");
        OutboxRepository.appendJson(getContext(), json);
        json.append(",\"notifications\":[");
        List<NotificationRepository.Entry> notifications =
                NotificationRepository.snapshot();
        for (int i = notifications.size() - 1; i >= 0; --i) {
            if (i < notifications.size() - 1) {
                json.append(',');
            }
            notifications.get(i).appendJson(json);
        }
        return json.append("]}").toString();
    }

    private boolean isTrustedCaller() {
        int uid = Binder.getCallingUid();
        return uid == SHELL_UID || uid == Process.myUid();
    }

    private static Bundle actionResult(boolean success) {
        return encodedResult("{\"ok\":" + success + "}");
    }

    private static Bundle encodedResult(String json) {
        Bundle result = new Bundle();
        String encoded = Base64.encodeToString(
                json.getBytes(StandardCharsets.UTF_8),
                Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
        result.putString("data", encoded);
        return result;
    }

    private static String decodeExtra(BaseBundle extras, String key) {
        if (extras == null) {
            return null;
        }
        String encoded = extras.getString(key);
        if (encoded == null) {
            return null;
        }
        try {
            byte[] value = Base64.decode(encoded,
                    Base64.URL_SAFE | Base64.NO_WRAP | Base64.NO_PADDING);
            return new String(value, StandardCharsets.UTF_8);
        } catch (IllegalArgumentException e) {
            return null;
        }
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection,
                        String[] selectionArgs, String sortOrder) {
        throw new UnsupportedOperationException("Use ContentProvider.call");
    }

    @Override
    public String getType(Uri uri) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        throw new UnsupportedOperationException("Use ContentProvider.call");
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        throw new UnsupportedOperationException("Use ContentProvider.call");
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection,
                      String[] selectionArgs) {
        throw new UnsupportedOperationException("Use ContentProvider.call");
    }
}
