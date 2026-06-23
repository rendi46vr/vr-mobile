package com.vrmobile.companion;

import android.app.Activity;
import android.app.NotificationManager;
import android.content.ComponentName;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.provider.Settings;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.text.DateFormat;
import java.util.Date;

public final class MainActivity extends Activity {
    private TextView notificationStatus;
    private TextView latestShare;
    private TextView latestNotification;

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
                "Share files to the desktop bridge and grant optional notification access.",
                15, R.color.vr_muted);
        subtitle.setPadding(0, dp(6), 0, dp(24));
        content.addView(subtitle);

        content.addView(sectionTitle("Desktop link"));
        TextView desktopStatus = text(
                "Pairing protocol: pending. This build has no Internet permission.",
                15, R.color.vr_muted);
        desktopStatus.setPadding(0, dp(4), 0, dp(20));
        content.addView(desktopStatus);

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

    private void openNotificationAccess() {
        Intent intent = new Intent(Settings.ACTION_NOTIFICATION_LISTENER_SETTINGS);
        startActivity(intent);
    }

    private void refreshStatus() {
        NotificationManager manager = getSystemService(NotificationManager.class);
        ComponentName component = new ComponentName(this,
                NotificationBridgeService.class);
        boolean granted = manager != null
                && manager.isNotificationListenerAccessGranted(component);
        notificationStatus.setText(granted
                ? "Notification access granted. Events remain local in v1."
                : "Notification access not granted.");

        CompanionPreferences.Snapshot snapshot = CompanionPreferences.read(this);
        latestShare.setText(formatSharedFile(snapshot));
        latestNotification.setText(formatNotification(snapshot));
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
