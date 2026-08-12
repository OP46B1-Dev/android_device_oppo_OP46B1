/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/oplus/hardware/motor/BnMotor.h>
#include <android-base/thread_annotations.h>

#include <cstdint>
#include <mutex>

namespace aidl::vendor::oplus::hardware::motor {

class Motor final : public BnMotor {
public:
    ndk::ScopedAStatus initializeCalibration() override;
    ndk::ScopedAStatus move(int32_t direction, int32_t startMode) override;
    ndk::ScopedAStatus getPosition(int32_t* _aidl_return) override;
    ndk::ScopedAStatus getMoveState(int32_t* _aidl_return) override;

private:
    ndk::ScopedAStatus initializeCalibrationLocked() REQUIRES(mLock);

    std::mutex mLock;
    bool mCalibrationInitialized GUARDED_BY(mLock) = false;
};

}  // namespace aidl::vendor::oplus::hardware::motor
