package com.vrmobile.companion;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

public final class FileNameSanitizerTest {
    @Test
    public void keepsNormalFileName() {
        assertEquals("photo 2026.png", FileNameSanitizer.sanitize("photo 2026.png"));
    }

    @Test
    public void replacesUnsafeCharacters() {
        assertEquals("report_2026_.pdf", FileNameSanitizer.sanitize("report:2026?.pdf"));
    }

    @Test
    public void replacesEmptyName() {
        assertEquals("shared-file", FileNameSanitizer.sanitize("..."));
    }

    @Test
    public void avoidsWindowsReservedName() {
        assertEquals("_CON.txt", FileNameSanitizer.sanitize("CON.txt"));
    }

    @Test
    public void truncatesLongNameAndKeepsExtension() {
        String value = FileNameSanitizer.sanitize("a".repeat(160) + ".docx");
        assertEquals(120, value.length());
        assertTrue(value.endsWith(".docx"));
    }
}
