/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "CameraHalWrapper.h"

#include <android-base/logging.h>

#include <system/camera_metadata.h>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

namespace {

constexpr uint8_t kAeModeOnAutoFlash   = ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH;    // 2
constexpr uint8_t kAeModeOnAlwaysFlash = ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH; // 3

constexpr uint8_t kFlashModeSingle = ANDROID_FLASH_MODE_SINGLE;  // 2
constexpr uint8_t kFlashModeTorch  = ANDROID_FLASH_MODE_TORCH;   // 3

// Reads a single u8 metadata entry; returns false if absent or malformed.
bool readU8Entry(const camera_metadata_t* meta, uint32_t tag, uint8_t* out) {
    camera_metadata_ro_entry_t entry;
    if (find_camera_metadata_ro_entry(meta, tag, &entry) != 0) {
        return false;
    }
    if (entry.count != 1) {
        return false;
    }
    *out = entry.data.u8[0];
    return true;
}

bool requestIndicatesRearFlash(const camera_metadata_t* meta) {
    if (meta == nullptr) {
        return false;
    }

    uint8_t aeMode;
    if (readU8Entry(meta, ANDROID_CONTROL_AE_MODE, &aeMode)) {
        if (aeMode == kAeModeOnAutoFlash || aeMode == kAeModeOnAlwaysFlash) {
            return true;
        }
    }

    uint8_t flashMode;
    if (readU8Entry(meta, ANDROID_FLASH_MODE, &flashMode)) {
        if (flashMode == kFlashModeSingle || flashMode == kFlashModeTorch) {
            return true;
        }
    }

    return false;
}

const camera_metadata_t* validateMetadata(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        return nullptr;
    }
    const camera_metadata_t* meta =
            reinterpret_cast<const camera_metadata_t*>(data);
    size_t expected = size;
    int rc = validate_camera_metadata_structure(meta, &expected);
    if (rc != 0) {
        return nullptr;
    }
    return meta;
}

}  // namespace

CameraDeviceSessionWrapper::CameraDeviceSessionWrapper(
        sp<ICameraDeviceSession> underlying, bool isFront)
    : mUnderlying(underlying),
      mIsFront(isFront),
      mLastFrontSignalEmitted(false),
      mLastRearFlashSignalEmitted(false) {
    CHECK(underlying != nullptr);
    if (mIsFront) {
        CameraSignalEmitter::getInstance().setFrontCameraOn(true);
        mLastFrontSignalEmitted = true;
    }
}

CameraDeviceSessionWrapper::~CameraDeviceSessionWrapper() {
    std::lock_guard<std::mutex> lock(mInspectLock);
    cleanupSignalsLocked();
}

Return<void> CameraDeviceSessionWrapper::constructDefaultRequestSettings(
        RequestTemplate type,
        constructDefaultRequestSettings_cb _hidl_cb) {
    return mUnderlying->constructDefaultRequestSettings(type, _hidl_cb);
}

Return<void> CameraDeviceSessionWrapper::configureStreams(
        const StreamConfiguration& requestedConfiguration,
        configureStreams_cb _hidl_cb) {
    return mUnderlying->configureStreams(requestedConfiguration, _hidl_cb);
}

Return<void> CameraDeviceSessionWrapper::processCaptureRequest(
        const hidl_vec<CaptureRequest>& requests,
        const hidl_vec<BufferCache>& cachesToRemove,
        processCaptureRequest_cb _hidl_cb) {
    {
        std::lock_guard<std::mutex> lock(mInspectLock);
        for (size_t i = 0; i < requests.size(); i++) {
            inspectRequestLocked(requests[i]);
        }
    }

    return mUnderlying->processCaptureRequest(requests, cachesToRemove,
                                              _hidl_cb);
}

Return<void> CameraDeviceSessionWrapper::getCaptureRequestMetadataQueue(
        getCaptureRequestMetadataQueue_cb _hidl_cb) {
    _hidl_cb(::android::hardware::MQDescriptorSync<uint8_t>{});
    return ::android::hardware::Void();
}

Return<void> CameraDeviceSessionWrapper::getCaptureResultMetadataQueue(
        getCaptureResultMetadataQueue_cb _hidl_cb) {
    return mUnderlying->getCaptureResultMetadataQueue(_hidl_cb);
}

Return<Status> CameraDeviceSessionWrapper::flush() {
    return mUnderlying->flush();
}

Return<void> CameraDeviceSessionWrapper::close() {
    {
        std::lock_guard<std::mutex> lock(mInspectLock);
        cleanupSignalsLocked();
    }
    return mUnderlying->close();
}

void CameraDeviceSessionWrapper::cleanupSignalsLocked() {
    if (mIsFront && mLastFrontSignalEmitted) {
        CameraSignalEmitter::getInstance().setFrontCameraOn(false);
        mLastFrontSignalEmitted = false;
    }
    if (!mIsFront && mLastRearFlashSignalEmitted) {
        CameraSignalEmitter::getInstance().setRearFlashOn(false);
        mLastRearFlashSignalEmitted = false;
    }
}

void CameraDeviceSessionWrapper::inspectRequestLocked(
        const CaptureRequest& request) {
    if (request.fmqSettingsSize != 0) {
        return;
    }
    if (request.settings.size() == 0) {
        return;
    }

    const camera_metadata_t* meta = validateMetadata(
            request.settings.data(), request.settings.size());
    if (meta == nullptr) {
        return;
    }

    if (mIsFront) {
        return;
    }

    bool flashOn = requestIndicatesRearFlash(meta);
    if (flashOn != mLastRearFlashSignalEmitted) {
        CameraSignalEmitter::getInstance().setRearFlashOn(flashOn);
        mLastRearFlashSignalEmitted = flashOn;
    }
}

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor
