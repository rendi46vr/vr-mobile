package com.vrmobile.companion;

import java.util.Locale;

public final class FileNameSanitizer {
    private static final int MAX_FILE_NAME_LENGTH = 120;

    private FileNameSanitizer() {
        // Utility class
    }

    public static String sanitize(String candidate) {
        if (candidate == null) {
            return "shared-file";
        }

        String value = candidate.trim()
                .replaceAll("[\\p{Cntrl}\\\\/:*?\"<>|]", "_");
        while (value.startsWith(".")) {
            value = value.substring(1);
        }
        if (value.isEmpty()) {
            return "shared-file";
        }

        if (isReservedWindowsName(value)) {
            value = "_" + value;
        }

        if (value.length() > MAX_FILE_NAME_LENGTH) {
            value = truncatePreservingExtension(value, MAX_FILE_NAME_LENGTH);
        }
        return value;
    }

    private static boolean isReservedWindowsName(String value) {
        int dot = value.indexOf('.');
        String stem = dot == -1 ? value : value.substring(0, dot);
        stem = stem.toUpperCase(Locale.ROOT);
        if (stem.equals("CON") || stem.equals("PRN") || stem.equals("AUX")
                || stem.equals("NUL")) {
            return true;
        }
        return stem.matches("COM[1-9]") || stem.matches("LPT[1-9]");
    }

    private static String truncatePreservingExtension(String value, int maxLength) {
        int dot = value.lastIndexOf('.');
        if (dot <= 0 || value.length() - dot > 16) {
            return value.substring(0, maxLength);
        }

        String extension = value.substring(dot);
        int stemLength = maxLength - extension.length();
        return value.substring(0, stemLength) + extension;
    }
}
