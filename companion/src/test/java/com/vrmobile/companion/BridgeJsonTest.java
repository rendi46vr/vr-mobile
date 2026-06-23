package com.vrmobile.companion;

import org.junit.Test;

import static org.junit.Assert.assertEquals;

public final class BridgeJsonTest {
    @Test
    public void quotesTextForJson() {
        assertEquals("\"line\\n\\\"quoted\\\"\\\\path\"",
                     BridgeJson.quote("line\n\"quoted\"\\path"));
    }

    @Test
    public void writesNull() {
        assertEquals("null", BridgeJson.quote(null));
    }

    @Test
    public void escapesControlCharacters() {
        assertEquals("\"a\\u0001b\"", BridgeJson.quote("a\u0001b"));
    }
}
