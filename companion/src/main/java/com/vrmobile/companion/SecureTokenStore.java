package com.vrmobile.companion;

import android.content.Context;
import android.content.SharedPreferences;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.nio.charset.StandardCharsets;
import java.security.KeyStore;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

final class SecureTokenStore {
    private static final String KEYSTORE = "AndroidKeyStore";
    private static final String KEY_ALIAS = "vr_mobile_internet_link";
    private static final String PREFS = "vr_mobile_internet_secret";
    private static final String KEY_CIPHER_TEXT = "cipher_text";
    private static final String KEY_IV = "iv";

    private SecureTokenStore() {
        // Utility class
    }

    static boolean save(Context context, String token) {
        try {
            SecretKey key = getOrCreateKey();
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key);
            byte[] encrypted = cipher.doFinal(
                    token.getBytes(StandardCharsets.UTF_8));
            preferences(context).edit()
                    .putString(KEY_CIPHER_TEXT, Base64.encodeToString(
                            encrypted, Base64.NO_WRAP))
                    .putString(KEY_IV, Base64.encodeToString(
                            cipher.getIV(), Base64.NO_WRAP))
                    .apply();
            return true;
        } catch (Exception exception) {
            return false;
        }
    }

    static String load(Context context) {
        SharedPreferences prefs = preferences(context);
        String encryptedValue = prefs.getString(KEY_CIPHER_TEXT, null);
        String ivValue = prefs.getString(KEY_IV, null);
        if (encryptedValue == null || ivValue == null) {
            return null;
        }
        try {
            KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
            keyStore.load(null);
            SecretKey key = (SecretKey) keyStore.getKey(KEY_ALIAS, null);
            if (key == null) {
                return null;
            }
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(
                    128, Base64.decode(ivValue, Base64.NO_WRAP)));
            byte[] clear = cipher.doFinal(
                    Base64.decode(encryptedValue, Base64.NO_WRAP));
            return new String(clear, StandardCharsets.UTF_8);
        } catch (Exception exception) {
            return null;
        }
    }

    static void clear(Context context) {
        preferences(context).edit().clear().apply();
        try {
            KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
            keyStore.load(null);
            keyStore.deleteEntry(KEY_ALIAS);
        } catch (Exception ignored) {
            // Preference removal still invalidates the local link.
        }
    }

    private static SecretKey getOrCreateKey() throws Exception {
        KeyStore keyStore = KeyStore.getInstance(KEYSTORE);
        keyStore.load(null);
        SecretKey existing = (SecretKey) keyStore.getKey(KEY_ALIAS, null);
        if (existing != null) {
            return existing;
        }
        KeyGenerator generator = KeyGenerator.getInstance(
                KeyProperties.KEY_ALGORITHM_AES, KEYSTORE);
        generator.init(new KeyGenParameterSpec.Builder(
                KEY_ALIAS,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build());
        return generator.generateKey();
    }

    private static SharedPreferences preferences(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
}
