/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraHalWrapper.h"

#include <android-base/logging.h>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

CameraDeviceWrapper::CameraDeviceWrapper(sp<ICameraDevice> underlying,
                                         bool isFront)
    : mUnderlying(underlying),
      mIsFront(isFront) {
    CHECK(underlying != nullptr);
}

Return<void> CameraDeviceWrapper::getResourceCost(
        getResourceCost_cb _hidl_cb) {
    return mUnderlying->getResourceCost(_hidl_cb);
}

Return<void> CameraDeviceWrapper::getCameraCharacteristics(
        getCameraCharacteristics_cb _hidl_cb) {
    return mUnderlying->getCameraCharacteristics(_hidl_cb);
}

Return<Status> CameraDeviceWrapper::setTorchMode(
        ::android::hardware::camera::common::V1_0::TorchMode mode) {
    return mUnderlying->setTorchMode(mode);
}

Return<void> CameraDeviceWrapper::open(const sp<ICameraDeviceCallback>& callback,
                                       open_cb _hidl_cb) {
    return mUnderlying->open(
            callback,
            [this, _hidl_cb](Status status,
                             sp<ICameraDeviceSession> session) {
                if (status == Status::OK && session != nullptr) {
                    sp<ICameraDeviceSession> wrapped =
                            new CameraDeviceSessionWrapper(session, mIsFront);
                    _hidl_cb(status, wrapped);
                } else {
                    _hidl_cb(status, session);
                }
            });
}

Return<void> CameraDeviceWrapper::dumpState(
        const ::android::hardware::hidl_handle& fd) {
    return mUnderlying->dumpState(fd);
}

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor
