/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraSignal"

#include "CameraSignalService.h"

#include <android/log.h>
#include <hidl/HidlTransportSupport.h>
#include <hwbinder/IPCThreadState.h>
#include <private/android_filesystem_config.h>

#include <algorithm>
#include <chrono>

namespace oplus::camera::signal {

using ::android::hardware::IPCThreadState;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::interfacesEqual;
using ::android::sp;
using HidlCameraState =
        ::vendor::oplus::hardware::camera::signal::V1_0::CameraState;

namespace {

using namespace std::chrono_literals;

constexpr auto kWatchdogInterval = 5s;
constexpr auto kCameraStateTimeout = 15s;
constexpr auto kMotorEventTimeout = 2s;
constexpr size_t kMaxPendingMotorEvents = 32;
constexpr int32_t kFirstMotorScanCode = 183;
constexpr int32_t kLastMotorScanCode = 190;

bool requireCallingUid(uid_t expectedUid, const char* operation) {
    const uid_t callingUid = IPCThreadState::self()->getCallingUid();
    if (callingUid == expectedUid) {
        return true;
    }

    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "rejecting %s from uid %u", operation,
                        callingUid);
    return false;
}

}  // namespace

CameraSignalService::CameraSignalService() : mWatchdogThread([this] { runWatchdog(); }) {}

CameraSignalService::~CameraSignalService() {
    {
        std::lock_guard<std::mutex> guard(mLock);
        mStopping = true;
    }
    mCondition.notify_all();
    if (mWatchdogThread.joinable()) {
        mWatchdogThread.join();
    }
}

bool CameraSignalService::isSameListener(const sp<Listener>& left,
                                         const sp<Listener>& right) {
    return interfacesEqual(left, right);
}

std::vector<sp<CameraSignalService::Listener>> CameraSignalService::copyListeners() {
    std::lock_guard<std::mutex> guard(mLock);
    return mListeners;
}

void CameraSignalService::removeListener(const sp<Listener>& listener) {
    std::lock_guard<std::mutex> guard(mLock);
    mListeners.erase(std::remove_if(mListeners.begin(), mListeners.end(),
                                    [&listener](const auto& candidate) {
                                        return isSameListener(candidate, listener);
                                    }),
                     mListeners.end());
}

Return<void> CameraSignalService::registerListener(const sp<Listener>& listener) {
    if (!requireCallingUid(AID_SYSTEM, "registerListener")) {
        return Void();
    }
    if (listener == nullptr) {
        return Void();
    }

    std::vector<CameraState> replay;
    std::vector<MotorEvent> pendingMotorEvents;
    {
        std::lock_guard<std::mutex> guard(mLock);
        // There is one policy owner. Replacing it also drops a stale proxy if
        // the persistent helper process was restarted.
        mListeners.clear();
        mListeners.push_back(listener);
        for (const auto& [cameraId, state] : mStates) {
            (void)cameraId;
            replay.push_back(state);
        }
        const auto oldestAllowed = std::chrono::steady_clock::now() - kMotorEventTimeout;
        for (const MotorEvent& event : mPendingMotorEvents) {
            if (event.queuedAt >= oldestAllowed) {
                pendingMotorEvents.push_back(event);
            }
        }
        mPendingMotorEvents.clear();
    }

    for (const CameraState& state : replay) {
        const HidlCameraState replayState =
                state.timedOut ? HidlCameraState::IDLE : state.state;
        Return<void> status =
                listener->onCameraStateChanged(state.cameraId, replayState, state.sessionId,
                                               state.deliveredSequence, state.configurationId);
        if (!status.isOk()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "listener replay failed: %s",
                                status.description().c_str());
            removeListener(listener);
            return Void();
        }
    }
    for (const MotorEvent& event : pendingMotorEvents) {
        Return<void> status = listener->onMotorEvent(event.scanCode, event.eventTimeNanos);
        if (!status.isOk()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "motor event replay failed: %s",
                                status.description().c_str());
            removeListener(listener);
            return Void();
        }
    }
    return Void();
}

Return<void> CameraSignalService::unregisterListener(const sp<Listener>& listener) {
    if (!requireCallingUid(AID_SYSTEM, "unregisterListener")) {
        return Void();
    }
    if (listener != nullptr) {
        removeListener(listener);
    }
    return Void();
}

Return<void> CameraSignalService::notifyCameraState(int32_t cameraId, HidlCameraState state,
                                                     int64_t sessionId, int64_t sequence,
                                                     int32_t configurationId) {
    if (!requireCallingUid(AID_CAMERASERVER, "notifyCameraState")) {
        return Void();
    }
    if (cameraId < 0 || static_cast<int32_t>(state) < static_cast<int32_t>(HidlCameraState::IDLE) ||
        static_cast<int32_t>(state) > static_cast<int32_t>(HidlCameraState::REAR_VIDEO) ||
        sessionId <= 0 || sequence <= 0 ||
        configurationId < 0) {
        return Void();
    }

    CameraState delivery{};
    {
        std::lock_guard<std::mutex> guard(mLock);
        const auto now = std::chrono::steady_clock::now();
        const auto existing = mStates.find(cameraId);
        if (existing != mStates.end()) {
            CameraState& current = existing->second;
            if (current.sessionId > sessionId ||
                (current.sessionId == sessionId && current.sourceSequence > sequence)) {
                return Void();
            }
            if (current.sessionId == sessionId && current.sourceSequence == sequence) {
                current.lastSeen = now;
                if (!current.timedOut) {
                    return Void();
                }
                current.timedOut = false;
                current.state = state;
                current.configurationId = configurationId;
                current.deliveredSequence =
                        std::max(current.deliveredSequence + 1, sequence);
                delivery = current;
            } else {
                const int64_t deliveredSequence = current.sessionId == sessionId
                                                          ? std::max(current.deliveredSequence + 1,
                                                                     sequence)
                                                          : sequence;
                current = CameraState{cameraId, state, sessionId, sequence, deliveredSequence,
                                      configurationId, now, false};
                delivery = current;
            }
        } else {
            delivery = CameraState{cameraId, state, sessionId, sequence, sequence, configurationId,
                                   now, false};
            mStates.emplace(cameraId, delivery);
        }
    }

    deliverState(delivery);
    return Void();
}

void CameraSignalService::deliverState(const CameraState& state) {
    const HidlCameraState deliveredState =
            state.timedOut ? HidlCameraState::IDLE : state.state;
    for (const auto& listener : copyListeners()) {
        const Return<void> status = listener->onCameraStateChanged(
                state.cameraId, deliveredState, state.sessionId, state.deliveredSequence,
                state.configurationId);
        if (!status.isOk()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "camera callback failed: %s",
                                status.description().c_str());
            removeListener(listener);
        }
    }
}

void CameraSignalService::runWatchdog() {
    for (;;) {
        std::vector<CameraState> timedOutStates;
        {
            std::unique_lock<std::mutex> lock(mLock);
            if (mCondition.wait_for(lock, kWatchdogInterval, [this] { return mStopping; })) {
                return;
            }

            const auto now = std::chrono::steady_clock::now();
            for (auto& [cameraId, state] : mStates) {
                (void)cameraId;
                if (state.state == HidlCameraState::IDLE || state.timedOut ||
                    now - state.lastSeen < kCameraStateTimeout) {
                    continue;
                }
                state.timedOut = true;
                ++state.deliveredSequence;
                timedOutStates.push_back(state);
            }
        }

        for (const CameraState& state : timedOutStates) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG,
                                "camera %d heartbeat timed out; publishing idle", state.cameraId);
            deliverState(state);
        }
    }
}

Return<void> CameraSignalService::notifyMotorEvent(int32_t scanCode, int64_t eventTimeNanos) {
    if (!requireCallingUid(AID_SYSTEM, "notifyMotorEvent")) {
        return Void();
    }
    if (scanCode < kFirstMotorScanCode || scanCode > kLastMotorScanCode || eventTimeNanos < 0) {
        return Void();
    }

    std::vector<sp<Listener>> listeners;
    {
        std::lock_guard<std::mutex> guard(mLock);
        if (!mListeners.empty()) {
            listeners = mListeners;
        } else {
            const auto now = std::chrono::steady_clock::now();
            mPendingMotorEvents.erase(
                    std::remove_if(mPendingMotorEvents.begin(), mPendingMotorEvents.end(),
                                   [now](const MotorEvent& event) {
                                       return now - event.queuedAt > kMotorEventTimeout;
                                   }),
                    mPendingMotorEvents.end());
            if (mPendingMotorEvents.size() == kMaxPendingMotorEvents) {
                mPendingMotorEvents.erase(mPendingMotorEvents.begin());
            }
            mPendingMotorEvents.push_back(MotorEvent{scanCode, eventTimeNanos, now});
            return Void();
        }
    }

    for (const auto& listener : listeners) {
        const Return<void> status = listener->onMotorEvent(scanCode, eventTimeNanos);
        if (!status.isOk()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "motor callback failed: %s",
                                status.description().c_str());
            removeListener(listener);
        }
    }
    return Void();
}

}  // namespace oplus::camera::signal
