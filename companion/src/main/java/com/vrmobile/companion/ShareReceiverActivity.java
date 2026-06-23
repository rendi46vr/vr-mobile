package com.vrmobile.companion;

import android.app.Activity;
import android.content.ClipData;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.Gravity;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class ShareReceiverActivity extends Activity {
    private final ExecutorService executor = Executors.newSingleThreadExecutor();
    private TextView status;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(createContentView());

        List<Uri> uris = collectSharedUris(getIntent());
        if (uris.isEmpty()) {
            showResult("No shared file was provided.", false);
            return;
        }

        status.setText(getResources().getQuantityString(
                R.plurals.share_preparing, uris.size(), uris.size()));
        executor.execute(() -> copySharedFiles(uris, getIntent().getType()));
    }

    @Override
    protected void onDestroy() {
        executor.shutdownNow();
        super.onDestroy();
    }

    private LinearLayout createContentView() {
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setGravity(Gravity.CENTER_HORIZONTAL);
        content.setPadding(dp(28), dp(28), dp(28), dp(28));

        ProgressBar progress = new ProgressBar(this);
        content.addView(progress);

        status = new TextView(this);
        status.setText(R.string.share_reading);
        status.setTextSize(16);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(18), 0, 0);
        content.addView(status, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT,
                LinearLayout.LayoutParams.WRAP_CONTENT));
        return content;
    }

    private void copySharedFiles(List<Uri> uris, String fallbackMimeType) {
        int copied = 0;
        String lastName = null;
        String error = null;
        for (Uri uri : uris) {
            try {
                OutboxWriter.Result result = OutboxWriter.copy(this, uri, fallbackMimeType);
                ++copied;
                lastName = result.getDisplayName();
            } catch (IOException | SecurityException e) {
                error = e.getMessage();
                break;
            }
        }

        if (lastName != null) {
            CompanionPreferences.saveSharedFile(this, lastName,
                                                System.currentTimeMillis());
        }

        int completed = copied;
        String failure = error;
        runOnUiThread(() -> {
            if (failure == null) {
                showResult(getResources().getQuantityString(
                        R.plurals.share_success, completed, completed), true);
            } else {
                showResult(getResources().getQuantityString(
                        R.plurals.share_partial, completed, completed, failure),
                        false);
            }
        });
    }

    private void showResult(String message, boolean success) {
        status.setText(message);
        Toast.makeText(this, message,
                       success ? Toast.LENGTH_SHORT : Toast.LENGTH_LONG).show();
        status.postDelayed(this::finish, success ? 900 : 2200);
    }

    private static List<Uri> collectSharedUris(Intent intent) {
        Set<Uri> result = new LinkedHashSet<>();
        String action = intent.getAction();
        if (Intent.ACTION_SEND.equals(action)) {
            Uri uri = getSingleStream(intent);
            if (uri != null) {
                result.add(uri);
            }
        } else if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
            result.addAll(getMultipleStreams(intent));
        }

        ClipData clipData = intent.getClipData();
        if (clipData != null) {
            for (int i = 0; i < clipData.getItemCount(); ++i) {
                Uri uri = clipData.getItemAt(i).getUri();
                if (uri != null) {
                    result.add(uri);
                }
            }
        }
        return new ArrayList<>(result);
    }

    @SuppressWarnings("deprecation")
    private static Uri getSingleStream(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class);
        }
        return intent.getParcelableExtra(Intent.EXTRA_STREAM);
    }

    @SuppressWarnings("deprecation")
    private static List<Uri> getMultipleStreams(Intent intent) {
        ArrayList<Uri> streams;
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            streams = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri.class);
        } else {
            streams = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
        }
        return streams == null ? new ArrayList<>() : streams;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
