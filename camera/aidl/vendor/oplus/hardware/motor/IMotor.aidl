/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

package vendor.oplus.hardware.motor;

/** AIDL HAL that exclusively owns the pop-up camera motor nodes. */
@VintfStability
interface IMotor {
    const int DIRECTION_DOWN = 0;
    const int DIRECTION_UP = 1;

    const int START_NORMAL = 1;
    const int START_FORCE = 2;

    const int POSITION_UNKNOWN = -1;
    const int POSITION_UP = 0;
    const int POSITION_DOWN = 1;
    const int POSITION_MID = 2;

    /** Restore the per-device factory hall calibration once per HAL lifetime. */
    void initializeCalibration();

    /** Write direction first and then start the motor. */
    void move(int direction, int startMode);

    int getPosition();
    int getMoveState();
}
