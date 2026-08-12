/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <vendor/oplus/hardware/camera/signal/1.0/types.h>

#include <cstdint>

namespace oplus::camera::shim {

struct CameraSnapshot {
    int32_t cameraId;
    ::vendor::oplus::hardware::camera::signal::V1_0::CameraState state;
    int64_t sessionId;
    int64_t sequence;
    int32_t configurationId;
};

class SignalDispatcher final {
public:
    static SignalDispatcher& getInstance();

    // Capture threads only take a short lock and replace the pending snapshot.
    void submit(const CameraSnapshot& snapshot);

private:
    SignalDispatcher();
    ~SignalDispatcher() = delete;

    SignalDispatcher(const SignalDispatcher&) = delete;
    SignalDispatcher& operator=(const SignalDispatcher&) = delete;

    void run();

    class Impl;
    Impl* mImpl;
};

}  // namespace oplus::camera::shim
