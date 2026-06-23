package com.vrmobile.companion;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.RemoteInput;
import android.content.Context;
import android.content.Intent;
import android.os.Bundle;
import android.service.notification.StatusBarNotification;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class NotificationRepository {
    private static final int MAX_ENTRIES = 64;
    private static final Map<String, Entry> ENTRIES = new LinkedHashMap<>();

    private NotificationRepository() {
        // Utility class
    }

    public static synchronized void put(StatusBarNotification notification,
                                        String title, String text) {
        String key = notification.getKey();
        ENTRIES.remove(key);
        ENTRIES.put(key, new Entry(notification, title, text));
        while (ENTRIES.size() > MAX_ENTRIES) {
            String oldest = ENTRIES.keySet().iterator().next();
            ENTRIES.remove(oldest);
        }
    }

    public static synchronized void remove(String key) {
        ENTRIES.remove(key);
    }

    public static synchronized List<Entry> snapshot() {
        return new ArrayList<>(ENTRIES.values());
    }

    public static synchronized boolean open(String key) {
        Entry entry = ENTRIES.get(key);
        if (entry == null || entry.contentIntent == null) {
            return false;
        }
        try {
            entry.contentIntent.send();
            return true;
        } catch (PendingIntent.CanceledException e) {
            return false;
        }
    }

    public static synchronized boolean reply(Context context, String key,
                                             int actionIndex, String text) {
        Entry entry = ENTRIES.get(key);
        if (entry == null || actionIndex < 0
                || actionIndex >= entry.actions.length || text == null) {
            return false;
        }

        Notification.Action action = entry.actions[actionIndex];
        RemoteInput[] inputs = action.getRemoteInputs();
        if (inputs == null || inputs.length == 0 || action.actionIntent == null) {
            return false;
        }

        Bundle results = new Bundle();
        for (RemoteInput input : inputs) {
            if (input.getAllowFreeFormInput()) {
                results.putCharSequence(input.getResultKey(), text);
            }
        }
        if (results.isEmpty()) {
            return false;
        }

        Intent intent = new Intent();
        RemoteInput.addResultsToIntent(inputs, intent, results);
        try {
            action.actionIntent.send(context, 0, intent);
            return true;
        } catch (PendingIntent.CanceledException e) {
            return false;
        }
    }

    public static final class Entry {
        private final String key;
        private final String packageName;
        private final String title;
        private final String text;
        private final long postTime;
        private final PendingIntent contentIntent;
        private final Notification.Action[] actions;

        private Entry(StatusBarNotification notification, String title,
                      String text) {
            Notification value = notification.getNotification();
            this.key = notification.getKey();
            this.packageName = notification.getPackageName();
            this.title = title;
            this.text = text;
            this.postTime = notification.getPostTime();
            this.contentIntent = value.contentIntent;
            this.actions = value.actions == null
                    ? new Notification.Action[0] : value.actions.clone();
        }

        public void appendJson(StringBuilder json) {
            json.append('{')
                    .append("\"key\":").append(BridgeJson.quote(key))
                    .append(",\"package\":").append(BridgeJson.quote(packageName))
                    .append(",\"title\":").append(BridgeJson.quote(title))
                    .append(",\"text\":").append(BridgeJson.quote(text))
                    .append(",\"post_time\":").append(postTime)
                    .append(",\"can_open\":").append(contentIntent != null)
                    .append(",\"reply_action\":").append(findReplyAction())
                    .append('}');
        }

        private int findReplyAction() {
            for (int i = 0; i < actions.length; ++i) {
                RemoteInput[] inputs = actions[i].getRemoteInputs();
                if (actions[i].actionIntent != null && inputs != null) {
                    for (RemoteInput input : inputs) {
                        if (input.getAllowFreeFormInput()) {
                            return i;
                        }
                    }
                }
            }
            return -1;
        }
    }
}
