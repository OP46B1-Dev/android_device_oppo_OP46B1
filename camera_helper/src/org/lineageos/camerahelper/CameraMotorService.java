/*
 * Copyright (C) 2019 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper;

import android.annotation.NonNull;
import android.app.Service;
import android.content.Intent;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.IBinder;
import android.os.Message;
import android.os.SystemClock;
import android.os.SystemProperties;
import android.util.Log;

public class CameraMotorService extends Service implements Handler.Callback {
    private static final boolean DEBUG = true;
    private static final String TAG = "CameraMotorService";

    static {
        System.loadLibrary("camerapropwait");
    }

    private static native void nativeStartPropWatch(Runnable callback);

    private static native void nativeStopPropWatch();

    public static final int CAMERA_EVENT_DELAY_TIME = 100; // ms

    public static final String FLASHLIGHT_CAMERA_ID = "0";

    public static final int MSG_CAMERA_CLOSED = 1000;
    public static final int MSG_CAMERA_OPEN = 1001;
    public static final int MSG_MOTOR_DECISION = 1002;
    public static final int MSG_HEARTBEAT_WATCHDOG = 1003;

    public static final int MOTOR_DECISION_TIMEOUT_MS = 2000; // ms

    public static final int HEARTBEAT_WATCHDOG_INTERVAL_MS = 30000; // ms

    private static final String PROP_FRONT_ON =
            "vendor.camera_hal_wrapper.front_on";
    private static final String PROP_REAR_FLASH_ON =
            "vendor.camera_hal_wrapper.rear_flash_on";
    private static final String PROP_HEARTBEAT =
            "vendor.camera_hal_wrapper.heartbeat";

    private Handler mHandler = new Handler(this);

    private CameraManager mCameraManager;

    private long mClosedEvent;
    private long mOpenEvent;

    private boolean mIsFlashlightOn = false;
    private boolean mIsFrontCameraOn = false;
    private boolean mIsRearFlashOn = false;

    private long mMotorDecisionEvent;

    private String mLastHeartbeat = "";

    private CameraManager.TorchCallback mTorchCallback =
            new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(@NonNull String cameraId, boolean enabled) {
                    super.onTorchModeChanged(cameraId, enabled);

                    if (cameraId.equals(FLASHLIGHT_CAMERA_ID)) {
                        mHandler.post(() -> {
                            mIsFlashlightOn = enabled;
                            if (DEBUG) Log.d(TAG, "Flashlight status: " + enabled);
                            MotorControl();
                        });
                    }
                }

                @Override
                public void onTorchModeUnavailable(@NonNull String cameraId) {
                    super.onTorchModeUnavailable(cameraId);

                    if (cameraId.equals(FLASHLIGHT_CAMERA_ID)) {
                        mHandler.post(() -> {
                            mIsFlashlightOn = false;
                            if (DEBUG) Log.d(TAG, "Flashlight unavailable");
                            MotorControl();
                        });
                    }
                }
            };

    private final Runnable mPropertyChangeCallback = () ->
            mHandler.post(this::handlePropertyChange);

    private void handlePropertyChange() {
        boolean newFront = SystemProperties.getBoolean(PROP_FRONT_ON, false);
        boolean newRearFlash = SystemProperties.getBoolean(PROP_REAR_FLASH_ON, false);
        if (newFront != mIsFrontCameraOn || newRearFlash != mIsRearFlashOn) {
            if (DEBUG) Log.d(TAG, "Wrapper signals: front=" + newFront
                    + " rear_flash=" + newRearFlash);
            mIsFrontCameraOn = newFront;
            mIsRearFlashOn = newRearFlash;
            MotorControl();
        }
    }

    @Override
    public void onCreate() {
        mCameraManager = getSystemService(CameraManager.class);
        mCameraManager.registerTorchCallback(mTorchCallback, null);

        nativeStartPropWatch(mPropertyChangeCallback);

        mIsFrontCameraOn = SystemProperties.getBoolean(PROP_FRONT_ON, false);
        mIsRearFlashOn = SystemProperties.getBoolean(PROP_REAR_FLASH_ON, false);

        mLastHeartbeat = SystemProperties.get(PROP_HEARTBEAT, "");
        mHandler.sendEmptyMessageDelayed(MSG_HEARTBEAT_WATCHDOG,
                HEARTBEAT_WATCHDOG_INTERVAL_MS);
    }

    private void MotorControl() {
        mMotorDecisionEvent = SystemClock.elapsedRealtime();
        if (mHandler.hasMessages(MSG_MOTOR_DECISION)) {
            mHandler.removeMessages(MSG_MOTOR_DECISION);
        }
        mHandler.sendEmptyMessageDelayed(MSG_MOTOR_DECISION, MOTOR_DECISION_TIMEOUT_MS);

        boolean anyCameraActive = mIsFlashlightOn || mIsFrontCameraOn || mIsRearFlashOn;
        if (anyCameraActive) {
            mOpenEvent = SystemClock.elapsedRealtime();
            if (SystemClock.elapsedRealtime() - mClosedEvent < CAMERA_EVENT_DELAY_TIME
                    && mHandler.hasMessages(MSG_CAMERA_CLOSED)) {
                mHandler.removeMessages(MSG_CAMERA_CLOSED);
            }
            mHandler.sendEmptyMessageDelayed(MSG_CAMERA_OPEN,
                    CAMERA_EVENT_DELAY_TIME);
        } else {
            mClosedEvent = SystemClock.elapsedRealtime();
            if (SystemClock.elapsedRealtime() - mOpenEvent < CAMERA_EVENT_DELAY_TIME
                    && mHandler.hasMessages(MSG_CAMERA_OPEN)) {
                mHandler.removeMessages(MSG_CAMERA_OPEN);
            }
            mHandler.sendEmptyMessageDelayed(MSG_CAMERA_CLOSED,
                    CAMERA_EVENT_DELAY_TIME);
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (DEBUG) Log.d(TAG, "Starting service");
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (DEBUG) Log.d(TAG, "Destroying service");
        if (mCameraManager != null) {
            mCameraManager.unregisterTorchCallback(mTorchCallback);
        }
        mHandler.removeMessages(MSG_HEARTBEAT_WATCHDOG);
        mHandler.removeMessages(MSG_MOTOR_DECISION);
        mHandler.removeMessages(MSG_CAMERA_OPEN);
        mHandler.removeMessages(MSG_CAMERA_CLOSED);
        nativeStopPropWatch();
        super.onDestroy();
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public boolean handleMessage(Message msg) {
        switch (msg.what) {
            case MSG_MOTOR_DECISION:
                if (SystemClock.elapsedRealtime() - mMotorDecisionEvent
                        < MOTOR_DECISION_TIMEOUT_MS) {
                    mHandler.sendEmptyMessageDelayed(MSG_MOTOR_DECISION,
                            MOTOR_DECISION_TIMEOUT_MS
                                    - (SystemClock.elapsedRealtime() - mMotorDecisionEvent));
                } else {
                    boolean anyCameraActive =
                            mIsFlashlightOn || mIsFrontCameraOn || mIsRearFlashOn;
                    if (anyCameraActive) {
                        CameraMotorController.setMotorDirection(
                                CameraMotorController.DIRECTION_UP);
                    } else {
                        CameraMotorController.setMotorDirection(
                                CameraMotorController.DIRECTION_DOWN);
                    }
                    CameraMotorController.setMotorEnabled();
                }
                break;
            case MSG_CAMERA_CLOSED:
                CameraMotorController.setMotorDirection(CameraMotorController.DIRECTION_DOWN);
                CameraMotorController.setMotorEnabled();
                break;
            case MSG_CAMERA_OPEN:
                CameraMotorController.setMotorDirection(CameraMotorController.DIRECTION_UP);
                CameraMotorController.setMotorEnabled();
                break;
            case MSG_HEARTBEAT_WATCHDOG:
                handleHeartbeatWatchdog();
                break;
        }
        return true;
    }

    private void handleHeartbeatWatchdog() {
        String current = SystemProperties.get(PROP_HEARTBEAT, "");
        if (!current.isEmpty() && current.equals(mLastHeartbeat)) {
            Log.w(TAG, "Wrapper heartbeat stale (" + current
                    + "), resetting camera state");
            boolean changed = false;
            if (mIsFrontCameraOn) {
                mIsFrontCameraOn = false;
                changed = true;
            }
            if (mIsRearFlashOn) {
                mIsRearFlashOn = false;
                changed = true;
            }
            if (changed) {
                MotorControl();
            }
        }
        mLastHeartbeat = current;
        mHandler.sendEmptyMessageDelayed(MSG_HEARTBEAT_WATCHDOG,
                HEARTBEAT_WATCHDOG_INTERVAL_MS);
    }
}
