/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <vendor/oplus/hardware/camera/signal/1.0/ICameraSignal.h>
#include <vendor/oplus/hardware/camera/signal/1.0/ICameraSignalListener.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

namespace oplus::camera::signal {

using HidlCameraState =
        ::vendor::oplus::hardware::camera::signal::V1_0::CameraState;

class CameraSignalService final
    : public ::vendor::oplus::hardware::camera::signal::V1_0::ICameraSignal {
public:
    CameraSignalService();
    ~CameraSignalService() override;

    ::android::hardware::Return<void> registerListener(
            const ::android::sp<::vendor::oplus::hardware::camera::signal::V1_0::
                                        ICameraSignalListener>& listener) override;
    ::android::hardware::Return<void> unregisterListener(
            const ::android::sp<::vendor::oplus::hardware::camera::signal::V1_0::
                                        ICameraSignalListener>& listener) override;
    ::android::hardware::Return<void> notifyCameraState(
            int32_t cameraId,
            HidlCameraState state,
            int64_t sessionId, int64_t sequence, int32_t configurationId) override;
    ::android::hardware::Return<void> notifyMotorEvent(int32_t scanCode,
                                                       int64_t eventTimeNanos) override;

private:
    struct CameraState {
        int32_t cameraId;
        HidlCameraState state;
        int64_t sessionId;
        int64_t sourceSequence;
        int64_t deliveredSequence;
        int32_t configurationId;
        std::chrono::steady_clock::time_point lastSeen;
        bool timedOut;
    };

    using Listener =
            ::vendor::oplus::hardware::camera::signal::V1_0::ICameraSignalListener;

    struct MotorEvent {
        int32_t scanCode;
        int64_t eventTimeNanos;
        std::chrono::steady_clock::time_point queuedAt;
    };

    std::vector<::android::sp<Listener>> copyListeners();
    void removeListener(const ::android::sp<Listener>& listener);
    void runWatchdog();
    void deliverState(const CameraState& state);
    static bool isSameListener(const ::android::sp<Listener>& left,
                               const ::android::sp<Listener>& right);

    std::mutex mLock;
    std::condition_variable mCondition;
    std::map<int32_t, CameraState> mStates;
    std::vector<::android::sp<Listener>> mListeners;
    std::vector<MotorEvent> mPendingMotorEvents;
    bool mStopping = false;
    std::thread mWatchdogThread;
};

}  // namespace oplus::camera::signal
