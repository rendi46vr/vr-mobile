package com.vrmobile.companion;

import android.app.Notification;
import android.os.Bundle;
import android.service.notification.NotificationListenerService;
import android.service.notification.StatusBarNotification;

public final class NotificationBridgeService extends NotificationListenerService {
    private static final int MAX_TEXT_LENGTH = 4096;

    @Override
    public void onNotificationPosted(StatusBarNotification notification) {
        Notification value = notification.getNotification();
        Bundle extras = value.extras;
        String title = limitedText(extras.getCharSequence(Notification.EXTRA_TITLE));
        String text = limitedText(extras.getCharSequence(Notification.EXTRA_TEXT));
        CompanionPreferences.saveNotification(
                this, notification.getPackageName(), title, text,
                notification.getPostTime());
        NotificationRepository.put(notification, title, text);
    }

    @Override
    public void onNotificationRemoved(StatusBarNotification notification) {
        NotificationRepository.remove(notification.getKey());
    }

    @Override
    public void onListenerConnected() {
        StatusBarNotification[] notifications = getActiveNotifications();
        if (notifications == null) {
            return;
        }
        for (StatusBarNotification notification : notifications) {
            Notification value = notification.getNotification();
            Bundle extras = value.extras;
            NotificationRepository.put(
                    notification,
                    limitedText(extras.getCharSequence(Notification.EXTRA_TITLE)),
                    limitedText(extras.getCharSequence(Notification.EXTRA_TEXT)));
        }
    }

    private static String limitedText(CharSequence value) {
        if (value == null) {
            return null;
        }
        String text = value.toString();
        return text.length() <= MAX_TEXT_LENGTH
                ? text : text.substring(0, MAX_TEXT_LENGTH);
    }
}
