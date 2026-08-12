/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraMotorHal"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/ProcessState.h>

#include <cstdlib>
#include <memory>
#include <string>

#include "Motor.h"

using aidl::vendor::oplus::hardware::motor::Motor;

int main() {
    // This device still ships vndservicemanager, so the vendor libbinder
    // default would otherwise publish the stable AIDL HAL on /dev/vndbinder.
    android::ProcessState::initWithDriver("/dev/binder");
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    std::shared_ptr<Motor> motor = ndk::SharedRefBase::make<Motor>();
    ndk::ScopedAStatus calibrationStatus = motor->initializeCalibration();
    if (!calibrationStatus.isOk()) {
        LOG(WARNING) << "Initial motor calibration failed; a later call will retry: "
                     << calibrationStatus.getDescription();
    }

    const std::string instance = std::string(Motor::descriptor) + "/default";
    const binder_status_t status =
            AServiceManager_addService(motor->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "Failed to register " << instance << ": " << status;
        return EXIT_FAILURE;
    }

    LOG(INFO) << "Registered " << instance;
    ABinderProcess_joinThreadPool();

    LOG(ERROR) << "Binder thread pool unexpectedly exited";
    return EXIT_FAILURE;
}
