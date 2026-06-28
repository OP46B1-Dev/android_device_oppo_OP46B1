/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraHalWrapper.h"

#include <android-base/logging.h>

#include <cutils/properties.h>
#include <time.h>

#include <chrono>
#include <string>
#include <thread>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

namespace {

int64_t boottimeMillis() {
    struct timespec ts;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

}  // namespace

CameraSignalEmitter& CameraSignalEmitter::getInstance() {
    static CameraSignalEmitter instance;
    return instance;
}

CameraSignalEmitter::CameraSignalEmitter()
    : mFrontCameraOn(false), mRearFlashOn(false) {
    property_set(kPropFrontCameraOn, "0");
    property_set(kPropRearFlashOn, "0");
    property_set(kPropHeartbeat, std::to_string(boottimeMillis()).c_str());
}

CameraSignalEmitter::~CameraSignalEmitter() {
    stopHeartbeat();
}

void CameraSignalEmitter::startHeartbeat() {
    if (mHeartbeatRunning.exchange(true)) {
        return;
    }
    mHeartbeatThread = std::thread([this] {
        while (mHeartbeatRunning.load()) {
            property_set(kPropHeartbeat,
                    std::to_string(boottimeMillis()).c_str());
            for (int i = 0; i < kHeartbeatIntervalSec * 10 &&
                            mHeartbeatRunning.load(); i++) {
                std::this_thread::sleep_for(
                        std::chrono::milliseconds(100));
            }
        }
    });
}

void CameraSignalEmitter::stopHeartbeat() {
    if (!mHeartbeatRunning.load()) {
        return;
    }
    mHeartbeatRunning.store(false);
    if (mHeartbeatThread.joinable()) {
        mHeartbeatThread.join();
    }
}

void CameraSignalEmitter::setFrontCameraOn(bool on) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mFrontCameraOn != on) {
            mFrontCameraOn = on;
            changed = true;
        }
    }
    if (changed) {
        if (property_set(kPropFrontCameraOn, on ? "1" : "0") != 0) {
            LOG(ERROR) << "property_set " << kPropFrontCameraOn << " failed";
        } else {
            LOG(INFO) << kPropFrontCameraOn << " -> " << (on ? 1 : 0);
        }
    }
}

void CameraSignalEmitter::setRearFlashOn(bool on) {
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mRearFlashOn != on) {
            mRearFlashOn = on;
            changed = true;
        }
    }
    if (changed) {
        if (property_set(kPropRearFlashOn, on ? "1" : "0") != 0) {
            LOG(ERROR) << "property_set " << kPropRearFlashOn << " failed";
        } else {
            LOG(INFO) << kPropRearFlashOn << " -> " << (on ? 1 : 0);
        }
    }
}

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor
