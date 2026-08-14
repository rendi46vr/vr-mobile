package com.vrmobile.companion;

import android.Manifest;
import android.app.Activity;
import android.app.NotificationManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Bundle;
import android.provider.Settings;
import android.text.InputType;
import android.view.View;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import com.google.mlkit.vision.barcode.common.Barcode;
import com.google.mlkit.vision.codescanner.GmsBarcodeScanner;
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions;
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning;

import java.text.DateFormat;
import java.util.Date;

public final class MainActivity extends Activity {
    private static final int SCREEN_CAPTURE_REQUEST = 7301;
    private TextView notificationStatus;
    private TextView desktopStatus;
    private Button bridgeButton;
    private TextView latestShare;
    private TextView latestNotification;
    private TextView internetStatus;
    private EditText serverUrlEdit;
    private EditText pairingCodeEdit;
    private Button pairButton;
    private Button forgetButton;
    private CheckBox autoReconnectCheck;
    private TextView standardRemoteStatus;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContentView());
    }

    @Override
    protected void onResume() {
        super.onResume();
        refreshStatus();
    }

    private View createContentView() {
        ScrollView scrollView = new ScrollView(this);
        scrollView.setFillViewport(true);
        scrollView.setBackgroundColor(getColor(R.color.vr_surface));

        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(dp(24), dp(28), dp(24), dp(28));
        scrollView.addView(content, new ScrollView.LayoutParams(
                ScrollView.LayoutParams.MATCH_PARENT,
                ScrollView.LayoutParams.WRAP_CONTENT));

        TextView title = text("VR Mobile Companion", 28, R.color.vr_text);
        content.addView(title);

        TextView subtitle = text(
                "Share files to Windows and connect optional Android notifications.",
                15, R.color.vr_muted);
        subtitle.setPadding(0, dp(6), 0, dp(24));
        content.addView(subtitle);

        content.addView(sectionTitle("Desktop link"));
        desktopStatus = text("Checking desktop bridge...", 15, R.color.vr_muted);
        desktopStatus.setPadding(0, dp(4), 0, dp(20));
        content.addView(desktopStatus);

        bridgeButton = button("Enable desktop bridge");
        bridgeButton.setOnClickListener(view -> toggleDesktopBridge());
        content.addView(bridgeButton);

        content.addView(sectionTitle("Internet Connect"));
        TextView internetDescription = text(
                "Fourth connection option for remote access outside USB, Wi-Fi, "
                        + "or Tailscale. Pairing and reconnect beta uses QR or "
                        + "a device code.",
                14, R.color.vr_muted);
        internetDescription.setPadding(0, dp(4), 0, dp(12));
        content.addView(internetDescription);

        serverUrlEdit = editText("Signaling server (https://…)");
        serverUrlEdit.setInputType(InputType.TYPE_CLASS_TEXT
                | InputType.TYPE_TEXT_VARIATION_URI);
        serverUrlEdit.setText(InternetLinkPreferences.getServerUrl(this));
        content.addView(serverUrlEdit);

        pairingCodeEdit = editText("10-digit device code");
        pairingCodeEdit.setInputType(InputType.TYPE_CLASS_NUMBER);
        pairingCodeEdit.setLetterSpacing(0.12f);
        content.addView(pairingCodeEdit);

        Button scanButton = button("Scan pairing QR");
        scanButton.setOnClickListener(view -> scanPairingQr());
        content.addView(scanButton);

        pairButton = button("Pair this phone");
        pairButton.setOnClickListener(view -> pairInternetDevice());
        content.addView(pairButton);

        autoReconnectCheck = new CheckBox(this);
        autoReconnectCheck.setText("Reconnect automatically when internet returns");
        autoReconnectCheck.setTextColor(getColor(R.color.vr_text));
        autoReconnectCheck.setChecked(
                InternetLinkPreferences.isAutoReconnect(this));
        autoReconnectCheck.setOnCheckedChangeListener((buttonView, checked) ->
                InternetLinkPreferences.setAutoReconnect(this, checked));
        autoReconnectCheck.setPadding(0, 0, 0, dp(8));
        content.addView(autoReconnectCheck);

        internetStatus = text("Not paired.", 14, R.color.vr_muted);
        internetStatus.setPadding(0, 0, 0, dp(8));
        content.addView(internetStatus);

        forgetButton = button("Forget trusted laptop");
        forgetButton.setOnClickListener(view -> forgetTrustedDesktop());
        content.addView(forgetButton);

        content.addView(sectionTitle("Standard Remote (no ADB beta)"));
        TextView remoteDescription = text(
                "Share a low-frame-rate screen preview through the trusted "
                        + "Internet link. Android confirmation is required "
                        + "for every sharing session.",
                14, R.color.vr_muted);
        remoteDescription.setPadding(0, dp(4), 0, dp(12));
        content.addView(remoteDescription);

        Button startRemoteButton = button("Start screen sharing");
        startRemoteButton.setOnClickListener(
                view -> startStandardRemote());
        content.addView(startRemoteButton);

        Button stopRemoteButton = button("Stop screen sharing");
        stopRemoteButton.setOnClickListener(
                view -> stopStandardRemote());
        content.addView(stopRemoteButton);

        standardRemoteStatus = text(
                "Screen sharing is stopped.", 14, R.color.vr_muted);
        standardRemoteStatus.setPadding(0, 0, 0, dp(16));
        content.addView(standardRemoteStatus);

        content.addView(sectionTitle("Shared file outbox"));
        TextView outboxPath = text("Shared storage/"
                                   + CompanionPreferences.OUTBOX_RELATIVE_PATH,
                                   14, R.color.vr_muted);
        outboxPath.setTextIsSelectable(true);
        outboxPath.setPadding(0, dp(4), 0, dp(8));
        content.addView(outboxPath);

        latestShare = text("No file has been shared yet.", 15, R.color.vr_text);
        latestShare.setPadding(0, 0, 0, dp(20));
        content.addView(latestShare);

        content.addView(sectionTitle("Notification bridge"));
        notificationStatus = text("Checking notification access...", 15,
                                  R.color.vr_text);
        notificationStatus.setPadding(0, dp(4), 0, dp(8));
        content.addView(notificationStatus);

        Button notificationButton = button("Open notification access");
        notificationButton.setOnClickListener(view -> openNotificationAccess());
        content.addView(notificationButton);

        latestNotification = text("No notification has been captured.", 14,
                                  R.color.vr_muted);
        latestNotification.setPadding(0, dp(12), 0, dp(20));
        content.addView(latestNotification);

        Button refreshButton = button("Refresh status");
        refreshButton.setOnClickListener(view -> refreshStatus());
        content.addView(refreshButton);

        return scrollView;
    }

    private TextView sectionTitle(String value) {
        TextView view = text(value, 18, R.color.vr_primary_dark);
        view.setPadding(0, dp(4), 0, 0);
        return view;
    }

    private TextView text(String value, int textSize, int colorResource) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(textSize);
        view.setTextColor(getColor(colorResource));
        view.setLineSpacing(0, 1.12f);
        return view;
    }

    private Button button(String label) {
        Button button = new Button(this);
        button.setText(label);
        button.setTextColor(Color.WHITE);
        button.setBackgroundTintList(getColorStateList(R.color.vr_primary));
        button.setAllCaps(false);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(48));
        params.setMargins(0, 0, 0, dp(8));
        button.setLayoutParams(params);
        return button;
    }

    private EditText editText(String hint) {
        EditText editText = new EditText(this);
        editText.setHint(hint);
        editText.setTextColor(getColor(R.color.vr_text));
        editText.setHintTextColor(getColor(R.color.vr_muted));
        editText.setSingleLine(true);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dp(52));
        params.setMargins(0, 0, 0, dp(8));
        editText.setLayoutParams(params);
        return editText;
    }

    private void openNotificationAccess() {
        Intent intent = new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS);
        startActivity(intent);
    }

    private void refreshStatus() {
        boolean bridgeEnabled = CompanionPreferences.isBridgeEnabled(this);
        desktopStatus.setText(bridgeEnabled
                ? "Desktop bridge enabled for authorized ADB computers."
                : "Desktop bridge disabled. No data is available to Windows.");
        bridgeButton.setText(bridgeEnabled
                ? "Disable desktop bridge" : "Enable desktop bridge");

        NotificationManager manager = getSystemService(NotificationManager.class);
        ComponentName component = new ComponentName(this,
                NotificationBridgeService.class);
        boolean granted = manager != null
                && manager.isNotificationListenerAccessGranted(component);
        notificationStatus.setText(granted
                ? "Notification access granted. Windows access follows the Desktop bridge switch."
                : "Notification access not granted.");

        CompanionPreferences.Snapshot snapshot = CompanionPreferences.read(this);
        latestShare.setText(formatSharedFile(snapshot));
        latestNotification.setText(formatNotification(snapshot));
        refreshInternetStatus();
        refreshStandardRemoteStatus();
    }

    @Override
    protected void onActivityResult(
            int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != SCREEN_CAPTURE_REQUEST) {
            return;
        }
        if (resultCode != RESULT_OK || data == null) {
            standardRemoteStatus.setText("Screen sharing was not approved.");
            return;
        }
        Intent service = new Intent(this, StandardRemoteService.class);
        service.setAction(StandardRemoteService.ACTION_START);
        service.putExtra(StandardRemoteService.EXTRA_RESULT_CODE, resultCode);
        service.putExtra(StandardRemoteService.EXTRA_RESULT_DATA, data);
        startForegroundService(service);
        standardRemoteStatus.setText(
                "Starting preview for the trusted laptop…");
    }

    private void scanPairingQr() {
        GmsBarcodeScannerOptions options =
                new GmsBarcodeScannerOptions.Builder()
                        .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
                        .build();
        GmsBarcodeScanner scanner = GmsBarcodeScanning.getClient(this, options);
        internetStatus.setText("Point the camera at the QR shown on the laptop.");
        scanner.startScan()
                .addOnSuccessListener(barcode -> {
                    String code = InternetPairing.normalizeCode(
                            barcode.getRawValue());
                    if (code == null) {
                        internetStatus.setText(
                                "That QR is not a VR Mobile pairing code.");
                        return;
                    }
                    pairingCodeEdit.setText(code);
                    internetStatus.setText(
                            "QR accepted. Confirm the signaling server, then pair.");
                })
                .addOnCanceledListener(() ->
                        internetStatus.setText("QR scan canceled."))
                .addOnFailureListener(error ->
                        internetStatus.setText(
                                "QR scanner unavailable: "
                                + safeMessage(error)));
    }

    private void pairInternetDevice() {
        String serverUrl = serverUrlEdit.getText().toString().trim();
        String code = InternetPairing.normalizeCode(
                pairingCodeEdit.getText().toString());
        if (!isServerUrlValid(serverUrl)) {
            internetStatus.setText(
                    isDebuggable()
                            ? "Enter an HTTPS server, or a local/Tailscale "
                                + "HTTP test server."
                            : "Enter a valid HTTPS signaling server first.");
            return;
        }
        if (code == null) {
            internetStatus.setText("Enter or scan a valid 10-digit code.");
            return;
        }

        InternetLinkPreferences.setServerUrl(this, serverUrl);
        String deviceId =
                InternetLinkPreferences.getOrCreateDeviceId(this);
        String deviceName = Build.MANUFACTURER + " " + Build.MODEL;
        pairButton.setEnabled(false);
        internetStatus.setText("Pairing securely with the laptop…");
        new Thread(() -> {
            try {
                InternetPairingClient.ClaimResult result =
                        InternetPairingClient.claim(
                                serverUrl, code, deviceId, deviceName);
                boolean saved =
                        InternetLinkPreferences.saveTrustedDesktop(
                                this, result.getDesktopId(),
                                result.getDesktopName(),
                                result.getLinkToken());
                runOnUiThread(() -> {
                    pairButton.setEnabled(true);
                    if (!saved) {
                        internetStatus.setText(
                                "Paired, but the secure token could not be saved.");
                        return;
                    }
                    pairingCodeEdit.setText("");
                    refreshInternetStatus();
                });
            } catch (Exception error) {
                runOnUiThread(() -> {
                    pairButton.setEnabled(true);
                    internetStatus.setText(
                            "Pairing failed: " + safeMessage(error));
                });
            }
        }, "vr-internet-pair").start();
    }

    private void refreshInternetStatus() {
        InternetLinkPreferences.TrustedDesktop trusted =
                InternetLinkPreferences.getTrustedDesktop(this);
        forgetButton.setEnabled(trusted != null);
        if (trusted == null) {
            internetStatus.setText(
                    "Not paired. Scan QR or enter the laptop device code.");
            return;
        }

        internetStatus.setText("Trusted laptop: " + trusted.getName()
                + "\nSecure key stored in Android Keystore.");
        if (!InternetLinkPreferences.isAutoReconnect(this)) {
            return;
        }
        String serverUrl = InternetLinkPreferences.getServerUrl(this);
        String deviceId = InternetLinkPreferences.getOrCreateDeviceId(this);
        if (!isServerUrlValid(serverUrl)) {
            return;
        }
        new Thread(() -> {
            try {
                InternetPairingClient.PresenceResult result =
                        InternetPairingClient.presence(
                                serverUrl, deviceId, trusted.getToken());
                runOnUiThread(() -> internetStatus.setText(
                        result.isPeerOnline()
                                ? "Internet signaling ready — "
                                    + trusted.getName() + " is online."
                                : "Paired with " + trusted.getName()
                                    + ". Waiting for the laptop to come online."));
            } catch (Exception error) {
                runOnUiThread(() -> internetStatus.setText(
                        "Paired with " + trusted.getName()
                                + ". Reconnect will retry when internet returns."));
            }
        }, "vr-internet-presence").start();
    }

    private void forgetTrustedDesktop() {
        InternetLinkPreferences.forgetTrustedDesktop(this);
        refreshInternetStatus();
    }

    private void startStandardRemote() {
        InternetLinkPreferences.TrustedDesktop trusted =
                InternetLinkPreferences.getTrustedDesktop(this);
        String serverUrl = InternetLinkPreferences.getServerUrl(this);
        if (trusted == null || !isServerUrlValid(serverUrl)) {
            standardRemoteStatus.setText(
                    "Pair a trusted laptop before sharing the screen.");
            return;
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS)
                    != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {
                Manifest.permission.POST_NOTIFICATIONS,
            }, 7302);
        }
        MediaProjectionManager manager =
                (MediaProjectionManager) getSystemService(
                        Context.MEDIA_PROJECTION_SERVICE);
        startActivityForResult(
                manager.createScreenCaptureIntent(), SCREEN_CAPTURE_REQUEST);
    }

    private void stopStandardRemote() {
        Intent service = new Intent(this, StandardRemoteService.class);
        service.setAction(StandardRemoteService.ACTION_STOP);
        stopService(service);
        refreshStandardRemoteStatus();
    }

    private void refreshStandardRemoteStatus() {
        standardRemoteStatus.setText(StandardRemoteService.isRunning()
                ? "Screen preview is running. Open Preview on the laptop."
                : "Screen sharing is stopped.");
    }

    private String safeMessage(Throwable error) {
        String message = error.getMessage();
        return message == null || message.isEmpty()
                ? error.getClass().getSimpleName() : message;
    }

    private boolean isServerUrlValid(String value) {
        return InternetPairing.isServerUrlValid(value, isDebuggable());
    }

    private boolean isDebuggable() {
        return (getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE)
                != 0;
    }

    private void toggleDesktopBridge() {
        boolean enabled = !CompanionPreferences.isBridgeEnabled(this);
        CompanionPreferences.setBridgeEnabled(this, enabled);
        refreshStatus();
    }

    private String formatSharedFile(CompanionPreferences.Snapshot snapshot) {
        if (snapshot.getLastSharedFile() == null) {
            return "No file has been shared yet.";
        }
        return snapshot.getLastSharedFile() + "\n"
                + formatTimestamp(snapshot.getLastSharedAt());
    }

    private String formatNotification(CompanionPreferences.Snapshot snapshot) {
        if (snapshot.getLastNotificationPackage() == null) {
            return "No notification has been captured.";
        }

        StringBuilder result = new StringBuilder();
        result.append(snapshot.getLastNotificationPackage());
        if (snapshot.getLastNotificationTitle() != null
                && !snapshot.getLastNotificationTitle().isEmpty()) {
            result.append("\n").append(snapshot.getLastNotificationTitle());
        }
        if (snapshot.getLastNotificationText() != null
                && !snapshot.getLastNotificationText().isEmpty()) {
            result.append("\n").append(snapshot.getLastNotificationText());
        }
        result.append("\n").append(formatTimestamp(snapshot.getLastNotificationAt()));
        return result.toString();
    }

    private String formatTimestamp(long timestamp) {
        if (timestamp <= 0) {
            return "Time unavailable";
        }
        return DateFormat.getDateTimeInstance(
                DateFormat.MEDIUM, DateFormat.SHORT).format(new Date(timestamp));
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
