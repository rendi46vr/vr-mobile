package com.vrmobile.companion;

public final class BridgeJson {
    private BridgeJson() {
        // Utility class
    }

    public static String quote(String value) {
        if (value == null) {
            return "null";
        }

        StringBuilder result = new StringBuilder(value.length() + 16);
        result.append('"');
        for (int i = 0; i < value.length(); ++i) {
            char valueChar = value.charAt(i);
            switch (valueChar) {
                case '"':
                    result.append("\\\"");
                    break;
                case '\\':
                    result.append("\\\\");
                    break;
                case '\b':
                    result.append("\\b");
                    break;
                case '\f':
                    result.append("\\f");
                    break;
                case '\n':
                    result.append("\\n");
                    break;
                case '\r':
                    result.append("\\r");
                    break;
                case '\t':
                    result.append("\\t");
                    break;
                default:
                    if (valueChar < 0x20) {
                        appendUnicodeEscape(result, valueChar);
                    } else {
                        result.append(valueChar);
                    }
                    break;
            }
        }
        return result.append('"').toString();
    }

    private static void appendUnicodeEscape(StringBuilder result, char value) {
        final char[] digits = "0123456789abcdef".toCharArray();
        result.append("\\u")
                .append(digits[(value >> 12) & 0x0f])
                .append(digits[(value >> 8) & 0x0f])
                .append(digits[(value >> 4) & 0x0f])
                .append(digits[value & 0x0f]);
    }
}
