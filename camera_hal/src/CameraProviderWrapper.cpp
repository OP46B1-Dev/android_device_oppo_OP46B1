/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraHalWrapper.h"

#include <android-base/logging.h>

#include <cstdlib>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

CameraProviderWrapper::CameraProviderWrapper(sp<ICameraProvider> underlying)
    : mUnderlying(underlying) {
    CHECK(underlying != nullptr);
}

Return<Status> CameraProviderWrapper::setCallback(
        const sp<::android::hardware::camera::provider::V2_4::
                ICameraProviderCallback>& callback) {
    return mUnderlying->setCallback(callback);
}

Return<void> CameraProviderWrapper::getVendorTags(getVendorTags_cb _hidl_cb) {
    return mUnderlying->getVendorTags(_hidl_cb);
}

Return<void> CameraProviderWrapper::getCameraIdList(
        getCameraIdList_cb _hidl_cb) {
    return mUnderlying->getCameraIdList(_hidl_cb);
}

Return<void> CameraProviderWrapper::isSetTorchModeSupported(
        isSetTorchModeSupported_cb _hidl_cb) {
    return mUnderlying->isSetTorchModeSupported(_hidl_cb);
}

Return<void> CameraProviderWrapper::getCameraDeviceInterface_V1_x(
        const hidl_string& cameraDeviceName,
        getCameraDeviceInterface_V1_x_cb _hidl_cb) {
    return mUnderlying->getCameraDeviceInterface_V1_x(cameraDeviceName,
                                                       _hidl_cb);
}

Return<void> CameraProviderWrapper::getCameraDeviceInterface_V3_x(
        const hidl_string& cameraDeviceName,
        getCameraDeviceInterface_V3_x_cb _hidl_cb) {
    const char* name = cameraDeviceName.c_str();
    const char* lastSep = std::strrchr(name, '/');
    int cameraId = std::atoi(lastSep ? lastSep + 1 : name);
    bool isFront = (cameraId == 1);

    return mUnderlying->getCameraDeviceInterface_V3_x(
            cameraDeviceName,
            [isFront, _hidl_cb](
                    Status status,
                    sp<ICameraDevice> device) {
                if (status == Status::OK && device != nullptr) {
                    sp<ICameraDevice> wrapped =
                            new CameraDeviceWrapper(device, isFront);
                    _hidl_cb(status, wrapped);
                } else {
                    _hidl_cb(status, device);
                }
            });
}

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor
