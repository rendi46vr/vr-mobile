package com.vrmobile.companion;

import java.net.URI;
import java.net.URISyntaxException;
import java.util.Locale;

public final class InternetPairing {
    public static final int CODE_DIGITS = 10;

    private InternetPairing() {
        // Utility class
    }

    public static String normalizeCode(String input) {
        if (input == null) {
            return null;
        }
        StringBuilder result = new StringBuilder(CODE_DIGITS);
        for (int i = 0; i < input.length(); ++i) {
            char value = input.charAt(i);
            if (value == ' ' || value == '-') {
                continue;
            }
            if (value < '0' || value > '9' || result.length() >= CODE_DIGITS) {
                return null;
            }
            result.append(value);
        }
        return result.length() == CODE_DIGITS ? result.toString() : null;
    }

    public static boolean isServerUrlValid(String value) {
        return isServerUrlValid(value, false);
    }

    public static boolean isServerUrlValid(
            String value, boolean allowLocalHttp) {
        if (value == null || value.isEmpty() || value.length() >= 512
                || containsUnsafeCharacter(value)) {
            return false;
        }
        try {
            URI uri = new URI(value);
            String scheme = uri.getScheme();
            String host = uri.getHost();
            if (scheme == null || host == null || uri.getUserInfo() != null
                    || uri.getQuery() != null || uri.getFragment() != null) {
                return false;
            }
            if ("https".equals(scheme.toLowerCase(Locale.ROOT))) {
                return true;
            }
            return allowLocalHttp
                    && "http".equals(scheme.toLowerCase(Locale.ROOT))
                    && isLocalDevelopmentHost(host);
        } catch (URISyntaxException ignored) {
            return false;
        }
    }

    public static String endpoint(String serverUrl, String path) {
        String base = serverUrl;
        while (base.endsWith("/")) {
            base = base.substring(0, base.length() - 1);
        }
        return base + path;
    }

    private static boolean containsUnsafeCharacter(String value) {
        for (int i = 0; i < value.length(); ++i) {
            char item = value.charAt(i);
            if (Character.isWhitespace(item) || item == '"' || item == '\\') {
                return true;
            }
        }
        return false;
    }

    private static boolean isLocalDevelopmentHost(String host) {
        String lower = host.toLowerCase(Locale.ROOT);
        if ("localhost".equals(lower) || "::1".equals(lower)) {
            return true;
        }
        String[] parts = lower.split("\\.", -1);
        if (parts.length != 4) {
            return false;
        }
        int[] octets = new int[4];
        for (int index = 0; index < parts.length; ++index) {
            try {
                octets[index] = Integer.parseInt(parts[index]);
            } catch (NumberFormatException ignored) {
                return false;
            }
            if (octets[index] < 0 || octets[index] > 255) {
                return false;
            }
        }
        return octets[0] == 10
                || octets[0] == 127
                || octets[0] == 192 && octets[1] == 168
                || octets[0] == 172 && octets[1] >= 16
                    && octets[1] <= 31
                || octets[0] == 100 && octets[1] >= 64
                    && octets[1] <= 127;
    }
}
