/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraSignal"

#include <android/log.h>
#include <hidl/HidlTransportSupport.h>

#include <cstdlib>

#include "CameraSignalService.h"

using ::android::OK;
using ::android::sp;
using ::android::hardware::configureRpcThreadpool;
using ::android::hardware::joinRpcThreadpool;
using oplus::camera::signal::CameraSignalService;

int main() {
    configureRpcThreadpool(4, true);

    sp<CameraSignalService> service = new CameraSignalService();
    const auto status = service->registerAsService();
    if (status != OK) {
        __android_log_print(ANDROID_LOG_FATAL, LOG_TAG, "failed to register default: %d", status);
        return EXIT_FAILURE;
    }

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "registered default");
    joinRpcThreadpool();
    return EXIT_FAILURE;
}
