/*
 * Copyright (c) 2019 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper;

import android.annotation.NonNull;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;
import android.util.Log;
import android.view.WindowManager;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class FallSensor implements SensorEventListener {
    private static final boolean DEBUG = true;
    private static final String TAG = "FallSensor";

    // Camera ID
    private static final String FLASHLIGHT_CAMERA_ID = "0";

    private ExecutorService mExecutorService;
    private SensorManager mSensorManager;
    private Sensor mSensor;
    private Context mContext;

    // Tracks whether the flashlight is currently on, so we can tell whether the
    // camera raise was triggered by the flashlight (rather than a front camera
    // app). FallSensor runs in a separate process from CameraMotorService, so
    // it maintains its own torch state.
    private boolean mIsFlashlightOn = false;

    private final CameraManager.TorchCallback mTorchCallback =
            new CameraManager.TorchCallback() {
                @Override
                public void onTorchModeChanged(@NonNull String cameraId, boolean enabled) {
                    super.onTorchModeChanged(cameraId, enabled);
                    if (FLASHLIGHT_CAMERA_ID.equals(cameraId)) {
                        mIsFlashlightOn = enabled;
                    }
                }

                @Override
                public void onTorchModeUnavailable(@NonNull String cameraId) {
                    super.onTorchModeUnavailable(cameraId);
                    if (FLASHLIGHT_CAMERA_ID.equals(cameraId)) {
                        mIsFlashlightOn = false;
                    }
                }
            };

    public FallSensor(Context context) {
        mContext = context;
        mSensorManager = mContext.getSystemService(SensorManager.class);
        mExecutorService = Executors.newSingleThreadExecutor();

        CameraManager cameraManager =
                (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
        if (cameraManager != null) {
            cameraManager.registerTorchCallback(mTorchCallback,
                    new Handler(Looper.getMainLooper()));
        }

        for (Sensor sensor : mSensorManager.getSensorList(Sensor.TYPE_ALL)) {
            if (DEBUG) Log.d(TAG, "Sensor type: " + sensor.getStringType());
            if (TextUtils.equals(sensor.getStringType(), "free_fall_detect")) {
                if (DEBUG) Log.d(TAG, "Found fall sensor");
                mSensor = sensor;
                break;
            }
        }
    }

    @Override
    public void onSensorChanged(SensorEvent event) {
        if (event.values[0] <= 0) {
            return;
        }

        Log.d(TAG, "Fall detected, ensuring front camera is closed");

        // We shouldn't really bother doing anything if motor is already closed
        if (CameraMotorController.getMotorPosition().equals(CameraMotorController.POSITION_DOWN)) {
            return;
        }

        // Close the camera
        CameraMotorController.setMotorDirection(CameraMotorController.DIRECTION_DOWN);
        CameraMotorController.setMotorEnabled();

        // Remember whether the flashlight was on before we close it. Only if the
        // raise was triggered by the flashlight do we reopen it on retry.
        boolean wasFlashlightOn = mIsFlashlightOn;

        // Close flashlight
        closeFlashlight();

        // Show alert dialog informing user that we closed the camera
        new Handler(Looper.getMainLooper()).post(() -> {
            AlertDialog alertDialog = new AlertDialog.Builder(mContext)
                    .setTitle(R.string.free_fall_detected_title)
                    .setMessage(R.string.free_fall_detected_message)
                    .setNegativeButton(R.string.raise_the_camera, (dialog, which) -> {
                        // Reopen the camera
                        CameraMotorController.setMotorDirection(CameraMotorController.DIRECTION_UP);
                        CameraMotorController.setMotorEnabled();

                        // Reopen the flashlight only if the raise was
                        // triggered by it, so that a front-camera app
                        // raising the motor does not unexpectedly turn the
                        // flashlight on.
                        if (wasFlashlightOn) {
                            openFlashlight();
                        }
                    })
                    .setPositiveButton(R.string.close, (dialog, which) -> {
                        // Go back to home screen
                        Intent intent = new Intent(Intent.ACTION_MAIN);
                        intent.addCategory(Intent.CATEGORY_HOME);
                        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                        mContext.startActivity(intent);
                    })
                    .create();
            alertDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT);
            alertDialog.setCanceledOnTouchOutside(false);
            alertDialog.show();
        });
    }

    @Override
    public void onAccuracyChanged(Sensor sensor, int accuracy) {
        /* Empty */
    }

    void enable() {
        if (DEBUG) Log.d(TAG, "Enabling");
        mExecutorService.submit(() -> {
            mSensorManager.registerListener(this, mSensor, SensorManager.SENSOR_DELAY_NORMAL);
        });
    }

    void disable() {
        if (DEBUG) Log.d(TAG, "Disabling");
        mExecutorService.submit(() -> {
            mSensorManager.unregisterListener(this, mSensor);
        });
    }

    private void closeFlashlight() {
        CameraManager cameraManager = (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
        try {
            cameraManager.setTorchMode(FLASHLIGHT_CAMERA_ID, false);
        } catch (CameraAccessException e) {
            Log.e(TAG, "Unable to turn off flashlight", e);
        }
    }

    private void openFlashlight() {
        CameraManager cameraManager = (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
        try {
            cameraManager.setTorchMode(FLASHLIGHT_CAMERA_ID, true);
        } catch (CameraAccessException e) {
            Log.e(TAG, "Unable to turn on flashlight", e);
        }
    }
}
