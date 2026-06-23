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
import android.util.Log;

public class CameraMotorService extends Service implements Handler.Callback {
    private static final boolean DEBUG = true;
    private static final String TAG = "CameraMotorService";

    public static final int CAMERA_EVENT_DELAY_TIME = 100; // ms

    public static final String FLASHLIGHT_CAMERA_ID = "0";
    public static final String FRONT_CAMERA_ID = "1";

    public static final int MSG_CAMERA_CLOSED = 1000;
    public static final int MSG_CAMERA_OPEN = 1001;
    public static final int MSG_MOTOR_DECISION = 1002;

    public static final int MOTOR_DECISION_TIMEOUT_MS = 2000; // ms

    private Handler mHandler = new Handler(this);

    private long mClosedEvent;
    private long mOpenEvent;

    private boolean mIsFlashlightOn = false;
    private boolean mIsFontCameraOn = false;

    private long mMotorDecisionEvent;

    private CameraManager.TorchCallback mTorchCallback =
            new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(@NonNull String cameraId, boolean enabled) {
                    super.onTorchModeChanged(cameraId, enabled);

                    if (cameraId.equals(FLASHLIGHT_CAMERA_ID)) {
                        mIsFlashlightOn = enabled;
                        if (DEBUG) Log.d(TAG, "Flashlight status: " + enabled);
                        MotorControl();
                    }
                }

                @Override
                public void onTorchModeUnavailable(@NonNull String cameraId) {
                    super.onTorchModeUnavailable(cameraId);

                    if (cameraId.equals(FLASHLIGHT_CAMERA_ID)) {
                        mIsFlashlightOn = false;
                        if (DEBUG) Log.d(TAG, "Flashlight unavailable");
                        MotorControl();
                    }
                }
            };

    private CameraManager.AvailabilityCallback mAvailabilityCallback =
            new CameraManager.AvailabilityCallback() {
                @Override
                public void onCameraAvailable(@NonNull String cameraId) {
                    super.onCameraAvailable(cameraId);

                    if (cameraId.equals(FRONT_CAMERA_ID)) {
                        mIsFontCameraOn = false;
                        if (DEBUG) Log.d(TAG, "Front camera unavailable");
                        MotorControl();
                    }
                }

                @Override
                public void onCameraUnavailable(@NonNull String cameraId) {
                    super.onCameraUnavailable(cameraId);

                    if (cameraId.equals(FRONT_CAMERA_ID)) {
                        mIsFontCameraOn = true;
                        if (DEBUG) Log.d(TAG, "Front camera available");
                        MotorControl();
                    }
                }
            };

    private void MotorControl() {
        // Reset the motor decision countdown to MOTOR_DECISION_TIMEOUT_MS on every call.
        mMotorDecisionEvent = SystemClock.elapsedRealtime();
        if (mHandler.hasMessages(MSG_MOTOR_DECISION)) {
            mHandler.removeMessages(MSG_MOTOR_DECISION);
        }
        mHandler.sendEmptyMessageDelayed(MSG_MOTOR_DECISION, MOTOR_DECISION_TIMEOUT_MS);

        if (mIsFlashlightOn || mIsFontCameraOn) {
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
    public void onCreate() {
        CameraManager cameraManager = getSystemService(CameraManager.class);
        cameraManager.registerAvailabilityCallback(mAvailabilityCallback, null);
        cameraManager.registerTorchCallback(mTorchCallback, null);
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        if (DEBUG) Log.d(TAG, "Starting service");
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (DEBUG) Log.d(TAG, "Destroying service");
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
                    // Not elapsed yet (a MotorControl() call reset the countdown):
                    // reschedule for the remaining time.
                    mHandler.sendEmptyMessageDelayed(MSG_MOTOR_DECISION,
                            MOTOR_DECISION_TIMEOUT_MS
                                    - (SystemClock.elapsedRealtime() - mMotorDecisionEvent));
                } else {
                    // Countdown elapsed: re-check camera/flashlight status and
                    // dispatch the camera module raise/retract event.
                    if (mIsFlashlightOn || mIsFontCameraOn) {
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
        }
        return true;
    }
}
