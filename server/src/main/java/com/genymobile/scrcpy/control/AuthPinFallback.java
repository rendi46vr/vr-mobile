package com.genymobile.scrcpy.control;

import com.genymobile.scrcpy.util.Ln;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

final class AuthPinFallback {

    private static final String UI_DUMP_PATH = "/data/local/tmp/vr-mobile-auth-window.xml";
    private static final String BOUNDS_CACHE_PATH = "/data/local/tmp/vr-mobile-auth-pin-bounds";
    private static final int MAX_XML_SIZE = 2 * 1024 * 1024;
    private static final Pattern NODE_PATTERN = Pattern.compile("<node\\s+[^>]*>");
    private static final Pattern BOUNDS_PATTERN = Pattern.compile("\\[(\\d+),(\\d+)]\\[(\\d+),(\\d+)]");

    private AuthPinFallback() {
        // not instantiable
    }

    static int[] findPinButtonBounds(String xml) {
        Matcher nodes = NODE_PATTERN.matcher(xml);
        while (nodes.find()) {
            String node = nodes.group();
            String text = normalize(attribute(node, "text") + " " + attribute(node, "content-desc"));
            String resourceId = normalize(attribute(node, "resource-id"));
            boolean clickable = "true".equals(attribute(node, "clickable"));
            boolean credentialResource = resourceId.contains("use_credential")
                    || resourceId.contains("use_pin")
                    || resourceId.contains("device_credential");
            if ((!clickable && !credentialResource) || (!isPinFallbackLabel(text) && !credentialResource)) {
                continue;
            }

            Matcher bounds = BOUNDS_PATTERN.matcher(attribute(node, "bounds"));
            if (!bounds.matches()) {
                continue;
            }
            int left = Integer.parseInt(bounds.group(1));
            int top = Integer.parseInt(bounds.group(2));
            int right = Integer.parseInt(bounds.group(3));
            int bottom = Integer.parseInt(bounds.group(4));
            if (right > left && bottom > top) {
                return new int[]{left, top, right, bottom};
            }
        }
        return null;
    }

    static int[] findPinButtonOnDevice() {
        File dump = new File(UI_DUMP_PATH);
        dump.delete();
        try {
            Process process = new ProcessBuilder("/system/bin/uiautomator", "dump", "--compressed", UI_DUMP_PATH)
                    .redirectErrorStream(true)
                    .start();
            drain(process.getInputStream());
            if (process.waitFor() != 0 || !dump.isFile()) {
                return null;
            }
            String xml = readFile(dump);
            return xml == null ? null : findPinButtonBounds(xml);
        } catch (IOException e) {
            Ln.d("Authentication UI inspection is unavailable: " + e.getMessage());
            return null;
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
            return null;
        } finally {
            dump.delete();
        }
    }

    static int[] loadCachedBounds(int rotation) {
        File cache = new File(BOUNDS_CACHE_PATH);
        try {
            String value = readFile(cache);
            if (value == null) {
                return null;
            }
            String[] parts = value.trim().split(",");
            if (parts.length != 5 || Integer.parseInt(parts[0]) != rotation) {
                return null;
            }
            int[] bounds = new int[]{
                    Integer.parseInt(parts[1]),
                    Integer.parseInt(parts[2]),
                    Integer.parseInt(parts[3]),
                    Integer.parseInt(parts[4]),
            };
            return bounds[2] > bounds[0] && bounds[3] > bounds[1] ? bounds : null;
        } catch (IOException | NumberFormatException e) {
            return null;
        }
    }

    static void saveCachedBounds(int rotation, int[] bounds) {
        String value = rotation + "," + bounds[0] + "," + bounds[1] + "," + bounds[2] + "," + bounds[3] + "\n";
        try (FileOutputStream output = new FileOutputStream(BOUNDS_CACHE_PATH)) {
            output.write(value.getBytes(StandardCharsets.US_ASCII));
        } catch (IOException e) {
            Ln.d("Could not cache authentication PIN position: " + e.getMessage());
        }
    }

    private static boolean isPinFallbackLabel(String value) {
        return value.contains("gunakan pin")
                || value.contains("pakai pin")
                || value.contains("gunakan kata sandi")
                || value.contains("gunakan password")
                || value.contains("gunakan pola")
                || value.contains("use pin")
                || value.contains("use password")
                || value.contains("use pattern")
                || value.contains("use device credential")
                || value.contains("enter pin instead");
    }

    private static String attribute(String node, String name) {
        Pattern pattern = Pattern.compile("(?:^|\\s)" + Pattern.quote(name) + "=\"([^\"]*)\"");
        Matcher matcher = pattern.matcher(node);
        return matcher.find() ? xmlUnescape(matcher.group(1)) : "";
    }

    private static String normalize(String value) {
        return value.toLowerCase(Locale.ROOT).trim();
    }

    private static String xmlUnescape(String value) {
        return value.replace("&quot;", "\"")
                .replace("&apos;", "'")
                .replace("&lt;", "<")
                .replace("&gt;", ">")
                .replace("&amp;", "&");
    }

    private static void drain(InputStream input) throws IOException {
        byte[] buffer = new byte[1024];
        while (input.read(buffer) != -1) {
            // discard command output
        }
    }

    private static String readFile(File file) throws IOException {
        if (file.length() <= 0 || file.length() > MAX_XML_SIZE) {
            return null;
        }
        try (InputStream input = new FileInputStream(file); ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) {
                if (output.size() + read > MAX_XML_SIZE) {
                    return null;
                }
                output.write(buffer, 0, read);
            }
            return new String(output.toByteArray(), StandardCharsets.UTF_8);
        }
    }
}
