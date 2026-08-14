/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.livedisplay-service.OP46B1"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "AntiFlicker.h"
#include "DisplayModes.h"
#include "SunlightEnhancement.h"

using ::aidl::vendor::lineage::livedisplay::AntiFlicker;
using ::aidl::vendor::lineage::livedisplay::DisplayModes;
using ::aidl::vendor::lineage::livedisplay::SunlightEnhancement;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    LOG(INFO) << "LiveDisplay HAL service is starting.";

    std::shared_ptr<AntiFlicker> af = ndk::SharedRefBase::make<AntiFlicker>();
    std::shared_ptr<DisplayModes> dm = ndk::SharedRefBase::make<DisplayModes>();
    std::shared_ptr<SunlightEnhancement> se = ndk::SharedRefBase::make<SunlightEnhancement>();

    const std::string afInstance = std::string(AntiFlicker::descriptor) + "/default";
    const binder_status_t afStatus =
            AServiceManager_addService(af->asBinder().get(), afInstance.c_str());
    CHECK_EQ(afStatus, STATUS_OK) << "Failed to register service " << afInstance << " "
                                  << afStatus;

    const std::string dmInstance = std::string(DisplayModes::descriptor) + "/default";
    const binder_status_t dmStatus =
            AServiceManager_addService(dm->asBinder().get(), dmInstance.c_str());
    CHECK_EQ(dmStatus, STATUS_OK) << "Failed to register service " << dmInstance << " "
                                  << dmStatus;

    const std::string seInstance = std::string(SunlightEnhancement::descriptor) + "/default";
    const binder_status_t seStatus =
            AServiceManager_addService(se->asBinder().get(), seInstance.c_str());
    CHECK_EQ(seStatus, STATUS_OK) << "Failed to register service " << seInstance << " "
                                  << seStatus;

    LOG(INFO) << "LiveDisplay HAL service is ready.";
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
