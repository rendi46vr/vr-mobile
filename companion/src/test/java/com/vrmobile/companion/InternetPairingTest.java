package com.vrmobile.companion;

import org.junit.Test;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

public final class InternetPairingTest {
    @Test
    public void normalizesCode() {
        assertEquals("1234567890", InternetPairing.normalizeCode("12345-67890"));
        assertEquals("1234567890", InternetPairing.normalizeCode("123 456 7890"));
        assertNull(InternetPairing.normalizeCode("1234"));
        assertNull(InternetPairing.normalizeCode("12345A6789"));
    }

    @Test
    public void validatesSecureServerUrl() {
        assertTrue(InternetPairing.isServerUrlValid("https://connect.example.com"));
        assertFalse(InternetPairing.isServerUrlValid("http://localhost:8787"));
        assertFalse(InternetPairing.isServerUrlValid("http://example.com"));
        assertFalse(InternetPairing.isServerUrlValid("https://bad host"));
    }

    @Test
    public void allowsLocalHttpOnlyForDevelopmentBuilds() {
        assertTrue(InternetPairing.isServerUrlValid(
                "http://localhost:8787", true));
        assertTrue(InternetPairing.isServerUrlValid(
                "http://192.168.1.15:8787", true));
        assertTrue(InternetPairing.isServerUrlValid(
                "http://100.113.32.82:8787", true));
        assertFalse(InternetPairing.isServerUrlValid(
                "http://100.128.0.1:8787", true));
        assertFalse(InternetPairing.isServerUrlValid(
                "http://example.com", true));
    }

    @Test
    public void buildsEndpoint() {
        assertEquals("https://connect.example.com/v1/pair/claim",
                InternetPairing.endpoint("https://connect.example.com/",
                                         "/v1/pair/claim"));
    }
}
