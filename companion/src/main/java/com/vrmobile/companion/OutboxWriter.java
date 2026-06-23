package com.vrmobile.companion;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.os.Environment;
import android.provider.MediaStore;
import android.provider.OpenableColumns;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public final class OutboxWriter {
    private static final String OUTBOX_RELATIVE_PATH =
            Environment.DIRECTORY_DOWNLOADS + "/VR Mobile Companion/Outbox/";
    private static final int BUFFER_SIZE = 64 * 1024;

    private OutboxWriter() {
        // Utility class
    }

    public static Result copy(Context context, Uri source, String fallbackMimeType)
            throws IOException {
        ContentResolver resolver = context.getContentResolver();
        SourceMetadata metadata = readSourceMetadata(resolver, source);
        String displayName = FileNameSanitizer.sanitize(metadata.displayName);
        String mimeType = resolver.getType(source);
        if (mimeType == null) {
            mimeType = fallbackMimeType == null ? "application/octet-stream" : fallbackMimeType;
        }

        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
        values.put(MediaStore.MediaColumns.MIME_TYPE, mimeType);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, OUTBOX_RELATIVE_PATH);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);

        Uri target = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
        if (target == null) {
            throw new IOException("Could not create an outbox file");
        }

        long copied;
        try (InputStream input = resolver.openInputStream(source);
             OutputStream output = resolver.openOutputStream(target, "w")) {
            if (input == null || output == null) {
                throw new IOException("Could not open the shared file");
            }
            copied = copyStream(input, output);
        } catch (IOException | RuntimeException e) {
            resolver.delete(target, null, null);
            throw e;
        }

        ContentValues completed = new ContentValues();
        completed.put(MediaStore.MediaColumns.IS_PENDING, 0);
        resolver.update(target, completed, null, null);

        String storedName = readDisplayName(resolver, target, displayName);
        return new Result(storedName, copied, target);
    }

    private static SourceMetadata readSourceMetadata(ContentResolver resolver, Uri source) {
        String displayName = null;
        try (Cursor cursor = resolver.query(
                source, new String[]{OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (index >= 0) {
                    displayName = cursor.getString(index);
                }
            }
        } catch (RuntimeException ignored) {
            // Some providers expose a stream but do not support metadata queries.
        }
        if (displayName == null) {
            displayName = source.getLastPathSegment();
        }
        return new SourceMetadata(displayName);
    }

    private static String readDisplayName(ContentResolver resolver, Uri target,
                                          String fallback) {
        try (Cursor cursor = resolver.query(
                target, new String[]{MediaStore.MediaColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                int index = cursor.getColumnIndex(MediaStore.MediaColumns.DISPLAY_NAME);
                if (index >= 0) {
                    String value = cursor.getString(index);
                    if (value != null) {
                        return value;
                    }
                }
            }
        } catch (RuntimeException ignored) {
            // The fallback name is still valid for status output.
        }
        return fallback;
    }

    private static long copyStream(InputStream input, OutputStream output) throws IOException {
        byte[] buffer = new byte[BUFFER_SIZE];
        long total = 0;
        int count;
        while ((count = input.read(buffer)) != -1) {
            output.write(buffer, 0, count);
            total += count;
        }
        return total;
    }

    private static final class SourceMetadata {
        private final String displayName;

        private SourceMetadata(String displayName) {
            this.displayName = displayName;
        }
    }

    public static final class Result {
        private final String displayName;
        private final long size;
        private final Uri uri;

        private Result(String displayName, long size, Uri uri) {
            this.displayName = displayName;
            this.size = size;
            this.uri = uri;
        }

        public String getDisplayName() {
            return displayName;
        }

        public long getSize() {
            return size;
        }

        public Uri getUri() {
            return uri;
        }
    }
}
