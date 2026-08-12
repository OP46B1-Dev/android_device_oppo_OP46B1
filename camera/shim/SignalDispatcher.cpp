/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraShim"

#include "SignalDispatcher.h"

#include <android/log.h>
#include <vendor/oplus/hardware/camera/signal/1.0/ICameraSignal.h>

#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace oplus::camera::shim {

using ::android::hardware::Return;
using ::android::sp;
using ::vendor::oplus::hardware::camera::signal::V1_0::ICameraSignal;
using namespace std::chrono_literals;

class SignalDispatcher::Impl {
public:
    std::mutex lock;
    std::condition_variable condition;
    std::map<int32_t, CameraSnapshot> pending;
    std::map<int32_t, CameraSnapshot> latest;
    sp<ICameraSignal> service;
    bool stopping = false;
    std::thread worker;
};

SignalDispatcher& SignalDispatcher::getInstance() {
    // camera.qcom is process-lifetime code. Keeping the dispatcher alive avoids
    // joining a worker from an unpredictable ELF destructor order at shutdown.
    static SignalDispatcher* const instance = new SignalDispatcher;
    return *instance;
}

SignalDispatcher::SignalDispatcher() : mImpl(new Impl) {
    mImpl->worker = std::thread([this] { run(); });
}

void SignalDispatcher::submit(const CameraSnapshot& snapshot) {
    {
        std::lock_guard<std::mutex> guard(mImpl->lock);
        const auto latest = mImpl->latest.find(snapshot.cameraId);
        if (latest != mImpl->latest.end() && (latest->second.sessionId > snapshot.sessionId ||
                                              (latest->second.sessionId == snapshot.sessionId &&
                                               latest->second.sequence > snapshot.sequence))) {
            return;
        }
        if (latest == mImpl->latest.end() || latest->second.sessionId < snapshot.sessionId ||
            latest->second.sequence < snapshot.sequence) {
            mImpl->latest[snapshot.cameraId] = snapshot;
        }

        const auto pending = mImpl->pending.find(snapshot.cameraId);
        if (pending == mImpl->pending.end() || pending->second.sessionId < snapshot.sessionId ||
            (pending->second.sessionId == snapshot.sessionId &&
             pending->second.sequence <= snapshot.sequence)) {
            mImpl->pending[snapshot.cameraId] = snapshot;
        }
    }
    mImpl->condition.notify_one();
}

void SignalDispatcher::run() {
    for (;;) {
        CameraSnapshot snapshot{};
        {
            std::unique_lock<std::mutex> lock(mImpl->lock);
            mImpl->condition.wait_for(
                    lock, 5s, [this] { return mImpl->stopping || !mImpl->pending.empty(); });
            if (mImpl->stopping) {
                return;
            }
            if (mImpl->pending.empty()) {
                // Replaying the last snapshots makes broker restarts recover
                // even when the camera mode itself has not changed.
                mImpl->pending = mImpl->latest;
            }
            if (mImpl->pending.empty()) {
                continue;
            }
            auto it = mImpl->pending.begin();
            snapshot = it->second;
            mImpl->pending.erase(it);
        }

        if (mImpl->service == nullptr) {
            mImpl->service = ICameraSignal::tryGetService();
        }

        if (mImpl->service == nullptr) {
            submit(snapshot);
            std::unique_lock<std::mutex> lock(mImpl->lock);
            mImpl->condition.wait_for(lock, 250ms, [this] { return mImpl->stopping; });
            continue;
        }

        Return<void> status = mImpl->service->notifyCameraState(
                snapshot.cameraId, snapshot.state, snapshot.sessionId, snapshot.sequence,
                snapshot.configurationId);
        if (!status.isOk()) {
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "camera signal transaction failed: %s",
                                status.description().c_str());
            mImpl->service.clear();
            submit(snapshot);
        }
    }
}

}  // namespace oplus::camera::shim
