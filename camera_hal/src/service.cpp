/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraHalWrapper.h"

#include <android-base/logging.h>
#include <android/hidl/manager/1.0/IServiceManager.h>
#include <hidl/HidlTransportSupport.h>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

extern "C" ICameraProvider* HIDL_FETCH_ICameraProvider(const char* name);

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor

int main() {
    using namespace vendor::oplus::camera_hal_wrapper;

    CameraSignalEmitter::getInstance();

    ICameraProvider* raw = HIDL_FETCH_ICameraProvider("legacy/0");
    if (raw == nullptr) {
        LOG(ERROR) << "HIDL_FETCH_ICameraProvider(\"legacy/0\") returned null";
        return 1;
    }
    sp<ICameraProvider> underlying = raw;
    LOG(INFO) << "Loaded underlying ICameraProvider via direct link";

    sp<CameraProviderWrapper> wrapper = new CameraProviderWrapper(underlying);

    android::hardware::configureRpcThreadpool(1, true /* callerWillJoin */);

    android::status_t status = wrapper->registerAsService("legacy/0");
    if (status != android::OK) {
        LOG(ERROR) << "registerAsService(\"legacy/0\") failed: " << status;
        return 1;
    }
    LOG(INFO) << "android.hardware.camera.provider@2.4-service_64.OP46B1 registered as legacy/0";

    CameraSignalEmitter::getInstance().startHeartbeat();

    android::hardware::joinRpcThreadpool();
    return 0;
}
