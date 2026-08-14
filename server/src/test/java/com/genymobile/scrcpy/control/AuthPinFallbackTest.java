package com.genymobile.scrcpy.control;

import org.junit.Assert;
import org.junit.Test;

public class AuthPinFallbackTest {

    @Test
    public void testFindIndonesianPinButton() {
        String xml = "<hierarchy><node text=\"Gunakan PIN\" resource-id=\"com.android.systemui:id/button_use_credential\" "
                + "clickable=\"true\" bounds=\"[120,1800][1080,1980]\"></node></hierarchy>";
        Assert.assertArrayEquals(new int[]{120, 1800, 1080, 1980}, AuthPinFallback.findPinButtonBounds(xml));
    }

    @Test
    public void testIgnoresUnrelatedPinText() {
        String xml = "<hierarchy><node text=\"Masukkan PIN\" resource-id=\"pin_entry\" clickable=\"true\" "
                + "bounds=\"[100,100][500,200]\"></node></hierarchy>";
        Assert.assertNull(AuthPinFallback.findPinButtonBounds(xml));
    }

    @Test
    public void testFindEnglishCredentialResource() {
        String xml = "<hierarchy><node text=\"\" resource-id=\"android:id/use_credential\" clickable=\"true\" "
                + "bounds=\"[10,20][110,220]\"></node></hierarchy>";
        Assert.assertArrayEquals(new int[]{10, 20, 110, 220}, AuthPinFallback.findPinButtonBounds(xml));
    }
}
