/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.OP46B1"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "HighTouchPollingRate.h"
#include "TouchscreenGesture.h"

using ::aidl::vendor::lineage::touch::HighTouchPollingRate;
using ::aidl::vendor::lineage::touch::TouchscreenGesture;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    LOG(INFO) << "Touch HAL service is starting.";

    std::shared_ptr<HighTouchPollingRate> htpr = ndk::SharedRefBase::make<HighTouchPollingRate>();
    std::shared_ptr<TouchscreenGesture> tg = ndk::SharedRefBase::make<TouchscreenGesture>();

    const std::string htprInstance = std::string(HighTouchPollingRate::descriptor) + "/default";
    const binder_status_t htprStatus =
            AServiceManager_addService(htpr->asBinder().get(), htprInstance.c_str());
    CHECK_EQ(htprStatus, STATUS_OK) << "Failed to register service " << htprInstance << " "
                                    << htprStatus;

    const std::string tgInstance = std::string(TouchscreenGesture::descriptor) + "/default";
    const binder_status_t tgStatus =
            AServiceManager_addService(tg->asBinder().get(), tgInstance.c_str());
    CHECK_EQ(tgStatus, STATUS_OK) << "Failed to register service " << tgInstance << " "
                                  << tgStatus;

    LOG(INFO) << "Touch HAL service is ready.";
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
