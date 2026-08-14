package com.vrmobile.companion;

import android.app.Activity;
import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.PixelFormat;
import android.hardware.display.DisplayManager;
import android.hardware.display.VirtualDisplay;
import android.media.Image;
import android.media.ImageReader;
import android.media.projection.MediaProjection;
import android.media.projection.MediaProjectionManager;
import android.os.Build;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.IBinder;
import android.util.DisplayMetrics;
import android.view.WindowManager;

import java.io.ByteArrayOutputStream;
import java.nio.ByteBuffer;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicBoolean;

public final class StandardRemoteService extends Service {
    public static final String ACTION_START =
            "com.vrmobile.companion.action.START_STANDARD_REMOTE";
    public static final String ACTION_STOP =
            "com.vrmobile.companion.action.STOP_STANDARD_REMOTE";
    public static final String EXTRA_RESULT_CODE = "result_code";
    public static final String EXTRA_RESULT_DATA = "result_data";

    private static final String CHANNEL_ID = "standard_remote";
    private static final int NOTIFICATION_ID = 4203;
    private static final long FRAME_INTERVAL_MS = 450;
    private static final int MAX_FRAME_WIDTH = 720;
    private static final int JPEG_QUALITY = 55;

    private static volatile boolean running;

    private final AtomicBoolean uploadPending = new AtomicBoolean();
    private final ExecutorService networkExecutor =
            Executors.newSingleThreadExecutor();
    private MediaProjection projection;
    private VirtualDisplay virtualDisplay;
    private ImageReader imageReader;
    private HandlerThread captureThread;
    private long lastFrameAt;

    public static boolean isRunning() {
        return running;
    }

    @Override
    public void onCreate() {
        super.onCreate();
        createNotificationChannel();
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent == null || ACTION_STOP.equals(intent.getAction())) {
            stopSelf();
            return START_NOT_STICKY;
        }
        startForeground(NOTIFICATION_ID, notification(
                "Preparing secure screen preview…"));
        if (!ACTION_START.equals(intent.getAction()) || projection != null) {
            return START_NOT_STICKY;
        }
        int resultCode = intent.getIntExtra(
                EXTRA_RESULT_CODE, Activity.RESULT_CANCELED);
        Intent resultData = getResultData(intent);
        InternetLinkPreferences.TrustedDesktop trusted =
                InternetLinkPreferences.getTrustedDesktop(this);
        String serverUrl = InternetLinkPreferences.getServerUrl(this);
        if (resultCode != Activity.RESULT_OK || resultData == null
                || trusted == null
                || !InternetPairing.isServerUrlValid(serverUrl, true)) {
            stopSelf();
            return START_NOT_STICKY;
        }
        startProjection(resultCode, resultData);
        running = true;
        updateNotification("Sharing screen with the trusted laptop");
        return START_NOT_STICKY;
    }

    @Override
    public void onDestroy() {
        running = false;
        if (virtualDisplay != null) {
            virtualDisplay.release();
            virtualDisplay = null;
        }
        if (imageReader != null) {
            imageReader.close();
            imageReader = null;
        }
        if (projection != null) {
            projection.stop();
            projection = null;
        }
        if (captureThread != null) {
            captureThread.quitSafely();
            captureThread = null;
        }
        networkExecutor.shutdownNow();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    private void startProjection(int resultCode, Intent resultData) {
        MediaProjectionManager manager =
                getSystemService(MediaProjectionManager.class);
        projection = manager.getMediaProjection(resultCode, resultData);
        projection.registerCallback(new MediaProjection.Callback() {
            @Override
            public void onStop() {
                stopSelf();
            }
        }, new Handler(getMainLooper()));

        WindowManager windowManager = getSystemService(WindowManager.class);
        DisplayMetrics metrics = new DisplayMetrics();
        windowManager.getDefaultDisplay().getRealMetrics(metrics);
        int width = metrics.widthPixels;
        int height = metrics.heightPixels;
        imageReader = ImageReader.newInstance(
                width, height, PixelFormat.RGBA_8888, 2);
        captureThread = new HandlerThread("vr-standard-capture");
        captureThread.start();
        Handler captureHandler = new Handler(captureThread.getLooper());
        imageReader.setOnImageAvailableListener(
                reader -> onImageAvailable(reader), captureHandler);
        virtualDisplay = projection.createVirtualDisplay(
                "VR Mobile Standard Remote", width, height,
                metrics.densityDpi,
                DisplayManager.VIRTUAL_DISPLAY_FLAG_AUTO_MIRROR,
                imageReader.getSurface(), null, captureHandler);
    }

    private void onImageAvailable(ImageReader reader) {
        try (Image image = reader.acquireLatestImage()) {
            long now = System.currentTimeMillis();
            if (image == null || now - lastFrameAt < FRAME_INTERVAL_MS
                    || !uploadPending.compareAndSet(false, true)) {
                return;
            }
            lastFrameAt = now;
            byte[] jpeg = encodeFrame(image);
            if (jpeg == null) {
                uploadPending.set(false);
                return;
            }
            networkExecutor.execute(() -> upload(jpeg));
        } catch (Exception ignored) {
            uploadPending.set(false);
        }
    }

    private byte[] encodeFrame(Image image) {
        Image.Plane plane = image.getPlanes()[0];
        ByteBuffer buffer = plane.getBuffer();
        int pixelStride = plane.getPixelStride();
        int rowStride = plane.getRowStride();
        int width = image.getWidth();
        int height = image.getHeight();
        int paddedWidth = width + (rowStride - pixelStride * width)
                / pixelStride;
        Bitmap padded = Bitmap.createBitmap(
                paddedWidth, height, Bitmap.Config.ARGB_8888);
        padded.copyPixelsFromBuffer(buffer);
        Bitmap cropped = Bitmap.createBitmap(padded, 0, 0, width, height);
        if (cropped != padded) {
            padded.recycle();
        }
        Bitmap output = cropped;
        if (width > MAX_FRAME_WIDTH) {
            int scaledHeight = Math.max(1,
                    height * MAX_FRAME_WIDTH / width);
            output = Bitmap.createScaledBitmap(
                    cropped, MAX_FRAME_WIDTH, scaledHeight, true);
            cropped.recycle();
        }
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        boolean compressed = output.compress(
                Bitmap.CompressFormat.JPEG, JPEG_QUALITY, bytes);
        output.recycle();
        return compressed ? bytes.toByteArray() : null;
    }

    private void upload(byte[] jpeg) {
        try {
            InternetLinkPreferences.TrustedDesktop trusted =
                    InternetLinkPreferences.getTrustedDesktop(this);
            if (trusted == null) {
                stopSelf();
                return;
            }
            StandardRemoteClient.uploadFrame(
                    InternetLinkPreferences.getServerUrl(this),
                    InternetLinkPreferences.getOrCreateDeviceId(this),
                    trusted.getToken(), jpeg);
        } catch (Exception ignored) {
            updateNotification("Preview reconnecting to signaling server…");
        } finally {
            uploadPending.set(false);
        }
    }

    private Intent getResultData(Intent intent) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            return intent.getParcelableExtra(EXTRA_RESULT_DATA, Intent.class);
        }
        return intent.getParcelableExtra(EXTRA_RESULT_DATA);
    }

    private void createNotificationChannel() {
        NotificationManager manager =
                getSystemService(NotificationManager.class);
        manager.createNotificationChannel(new NotificationChannel(
                CHANNEL_ID, "Standard Remote",
                NotificationManager.IMPORTANCE_LOW));
    }

    private Notification notification(String text) {
        Intent open = new Intent(this, MainActivity.class);
        PendingIntent pending = PendingIntent.getActivity(
                this, 0, open,
                PendingIntent.FLAG_UPDATE_CURRENT
                        | PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL_ID)
                .setSmallIcon(R.drawable.ic_vr_mobile)
                .setContentTitle("VR Mobile screen sharing")
                .setContentText(text)
                .setContentIntent(pending)
                .setOngoing(true)
                .build();
    }

    private void updateNotification(String text) {
        NotificationManager manager =
                getSystemService(NotificationManager.class);
        manager.notify(NOTIFICATION_ID, notification(text));
    }
}
