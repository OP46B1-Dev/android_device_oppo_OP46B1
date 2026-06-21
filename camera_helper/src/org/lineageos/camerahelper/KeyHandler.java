/*
 * Copyright (C) 2019 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package org.lineageos.camerahelper;

import android.annotation.NonNull;
import android.app.AlertDialog;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager.NameNotFoundException;
import android.hardware.camera2.CameraAccessException;
import android.hardware.camera2.CameraManager;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.KeyEvent;
import android.view.WindowManager;

import com.android.internal.os.DeviceKeyHandler;

public class KeyHandler implements DeviceKeyHandler {
    private static final String TAG = KeyHandler.class.getSimpleName();

    // Camera motor event key codes
    private static final int MOTOR_EVENT_MANUAL_TO_DOWN = 184;
    private static final int MOTOR_EVENT_UP = 185;
    private static final int MOTOR_EVENT_UP_ABNORMAL = 186;
    private static final int MOTOR_EVENT_UP_NORMAL = 187;
    private static final int MOTOR_EVENT_DOWN = 188;
    private static final int MOTOR_EVENT_DOWN_ABNORMAL = 189;
    private static final int MOTOR_EVENT_DOWN_NORMAL = 190;

    // Camera ID
    private static final String FLASHLIGHT_CAMERA_ID = "0";

    private final Context mContext;

    // Tracks whether the flashlight is currently on, so we can tell whether a
    // motor raise was triggered by the flashlight (rather than a front camera
    // app). KeyHandler runs in a separate process from CameraMotorService, so
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

    public KeyHandler(Context context) {
        mContext = context;
        CameraManager cameraManager =
                (CameraManager) mContext.getSystemService(Context.CAMERA_SERVICE);
        if (cameraManager != null) {
            cameraManager.registerTorchCallback(mTorchCallback,
                    new Handler(Looper.getMainLooper()));
        }
    }

    public KeyEvent handleKeyEvent(KeyEvent event) {
        int scanCode = event.getScanCode();

        switch (scanCode) {
            case MOTOR_EVENT_MANUAL_TO_DOWN:
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    showCameraMotorPressWarning();
                }
                break;
            case MOTOR_EVENT_UP_ABNORMAL:
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    showCameraMotorCannotGoUpWarning();
                }
                break;
            case MOTOR_EVENT_DOWN_ABNORMAL:
                if (event.getAction() == KeyEvent.ACTION_DOWN) {
                    showCameraMotorCannotGoDownWarning();
                }
                break;
            default:
                return event;
        }

        return null;
    }

    private Context getPackageContext() {
        try {
            return mContext.createPackageContext("org.lineageos.camerahelper", 0);
        } catch (NameNotFoundException | SecurityException e) {
            Log.e(TAG, "Failed to create package context", e);
        }
        return null;
    }

    private void showCameraMotorCannotGoDownWarning() {
        // Close flashlight
        closeFlashlight();

        // Show the alert
        new Handler(Looper.getMainLooper()).post(() -> {
            Context packageContext = getPackageContext();
            if (packageContext != null) {
                AlertDialog alertDialog = new AlertDialog.Builder(packageContext)
                        .setTitle(R.string.warning)
                        .setMessage(R.string.motor_cannot_go_down_message)
                        .setPositiveButton(R.string.retry, (dialog, which) -> {
                            // Close the camera
                            CameraMotorController.setMotorDirection(
                                    CameraMotorController.DIRECTION_DOWN);
                            CameraMotorController.setMotorEnabled();
                        })
                        .create();
                alertDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT);
                alertDialog.setCanceledOnTouchOutside(false);
                alertDialog.show();
            }
        });
    }

    private void showCameraMotorCannotGoUpWarning() {
        // Remember whether the flashlight was on before we close it. Only if the
        // raise was triggered by the flashlight do we reopen it on retry.
        boolean wasFlashlightOn = mIsFlashlightOn;

        // Close flashlight
        closeFlashlight();

        // Show the alert
        new Handler(Looper.getMainLooper()).post(() -> {
            Context packageContext = getPackageContext();
            if (packageContext != null) {
                AlertDialog alertDialog = new AlertDialog.Builder(packageContext)
                        .setTitle(R.string.warning)
                        .setMessage(R.string.motor_cannot_go_up_message)
                        .setNegativeButton(R.string.retry, (dialog, which) -> {
                            // Reopen the camera
                            CameraMotorController.setMotorDirection(
                                    CameraMotorController.DIRECTION_UP);
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
                            // Close the camera
                            CameraMotorController.setMotorDirection(
                                    CameraMotorController.DIRECTION_DOWN);
                            CameraMotorController.setMotorEnabled();

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
            }
        });
    }

    private void showCameraMotorPressWarning() {
        // Close flashlight
        closeFlashlight();

        // Go back to home to close all camera apps first
        Intent intent = new Intent(Intent.ACTION_MAIN);
        intent.addCategory(Intent.CATEGORY_HOME);
        intent.setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        mContext.startActivity(intent);

        // Show the alert
        new Handler(Looper.getMainLooper()).post(() -> {
            Context packageContext = getPackageContext();
            if (packageContext != null) {
                AlertDialog alertDialog = new AlertDialog.Builder(packageContext)
                        .setTitle(R.string.warning)
                        .setMessage(R.string.motor_press_message)
                        .setPositiveButton(android.R.string.ok, null)
                        .create();
                alertDialog.getWindow().setType(WindowManager.LayoutParams.TYPE_SYSTEM_ALERT);
                alertDialog.setCanceledOnTouchOutside(false);
                alertDialog.show();
            }
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
