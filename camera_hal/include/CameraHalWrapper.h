/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Camera HAL wrapper for OP46B1.
 *
 * Interposes between the cameraserver framework and the real vendor camera
 * provider (android.hardware.camera.provider@2.4 / device@3.2). The wrapper is
 * transparent for every HIDL method except ICameraDeviceSession::processCaptureRequest,
 * where it inspects each CaptureRequest's settings for:
 *
 *   - Front camera session is open  -> emits "front camera on" while the
 *     session is alive (cleared on close()/destructor); per-request metadata
 *     is not inspected for the front camera.
 *   - Rear camera with CONTROL_AE_MODE = ON_AUTO_FLASH or ON_ALWAYS_FLASH,
 *     or with ANDROID_FLASH_MODE = FLASH_MODE_TORCH / FLASH_MODE_SINGLE
 *                                                   -> emits "rear flash on"
 *
 * Signals are emitted via __system_property_set on
 * vendor.camera_hal_wrapper.front_on / .rear_flash_on, observed by
 * OPlusCameraHelper via a native prop-wait thread (see camera_helper/jni/).
 * The helper is downgraded to only consume torch state (via the existing
 * CameraManager.TorchCallback) plus the signals emitted here.
 */

#pragma once

#include <android-base/thread_annotations.h>
#include <android/hardware/camera/common/1.0/types.h>
#include <android/hardware/camera/device/3.2/ICameraDevice.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceCallback.h>
#include <android/hardware/camera/device/3.2/ICameraDeviceSession.h>
#include <android/hardware/camera/provider/2.4/ICameraProvider.h>
#include <hidl/HidlSupport.h>
#include <hidl/MQDescriptor.h>
#include <hidl/Status.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace vendor {
namespace oplus {
namespace camera_hal_wrapper {

using ::android::hardware::camera::common::V1_0::Status;
using ::android::hardware::camera::device::V3_2::BufferCache;
using ::android::hardware::camera::device::V3_2::CameraMetadata;
using ::android::hardware::camera::device::V3_2::CaptureRequest;
using ::android::hardware::camera::device::V3_2::HalStreamConfiguration;
using ::android::hardware::camera::device::V3_2::ICameraDevice;
using ::android::hardware::camera::device::V3_2::ICameraDeviceCallback;
using ::android::hardware::camera::device::V3_2::ICameraDeviceSession;
using ::android::hardware::camera::device::V3_2::RequestTemplate;
using ::android::hardware::camera::device::V3_2::StreamConfiguration;
using ::android::hardware::camera::provider::V2_4::ICameraProvider;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::sp;

constexpr const char* kPropFrontCameraOn = "vendor.camera_hal_wrapper.front_on";
constexpr const char* kPropRearFlashOn   = "vendor.camera_hal_wrapper.rear_flash_on";

constexpr const char* kPropHeartbeat     = "vendor.camera_hal_wrapper.heartbeat";
constexpr int kHeartbeatIntervalSec     = 5;

class CameraSignalEmitter {
public:
    static CameraSignalEmitter& getInstance();

    ~CameraSignalEmitter();

    void setFrontCameraOn(bool on);
    void setRearFlashOn(bool on);

    void startHeartbeat();
    void stopHeartbeat();

private:
    CameraSignalEmitter();

    std::mutex mLock;
    bool mFrontCameraOn GUARDED_BY(mLock);
    bool mRearFlashOn GUARDED_BY(mLock);

    std::atomic<bool> mHeartbeatRunning{false};
    std::thread mHeartbeatThread;
};

class CameraProviderWrapper : public ICameraProvider {
public:
    explicit CameraProviderWrapper(sp<ICameraProvider> underlying);

    Return<Status> setCallback(
            const sp<::android::hardware::camera::provider::V2_4::
                    ICameraProviderCallback>& callback) override;
    Return<void> getVendorTags(getVendorTags_cb _hidl_cb) override;
    Return<void> getCameraIdList(getCameraIdList_cb _hidl_cb) override;
    Return<void> isSetTorchModeSupported(
            isSetTorchModeSupported_cb _hidl_cb) override;
    Return<void> getCameraDeviceInterface_V1_x(
            const hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V1_x_cb _hidl_cb) override;
    Return<void> getCameraDeviceInterface_V3_x(
            const hidl_string& cameraDeviceName,
            getCameraDeviceInterface_V3_x_cb _hidl_cb) override;

private:
    sp<ICameraProvider> mUnderlying;
};

class CameraDeviceWrapper : public ICameraDevice {
public:
    CameraDeviceWrapper(sp<ICameraDevice> underlying, bool isFront);

    Return<void> getResourceCost(getResourceCost_cb _hidl_cb) override;
    Return<void> getCameraCharacteristics(
            getCameraCharacteristics_cb _hidl_cb) override;
    Return<Status> setTorchMode(
            ::android::hardware::camera::common::V1_0::TorchMode mode) override;
    Return<void> open(const sp<ICameraDeviceCallback>& callback,
                     open_cb _hidl_cb) override;
    Return<void> dumpState(const ::android::hardware::hidl_handle& fd) override;

private:
    sp<ICameraDevice> mUnderlying;
    bool mIsFront;
};

class CameraDeviceSessionWrapper : public ICameraDeviceSession {
public:
    CameraDeviceSessionWrapper(sp<ICameraDeviceSession> underlying,
                               bool isFront);
    ~CameraDeviceSessionWrapper() override;

    Return<void> constructDefaultRequestSettings(
            RequestTemplate type,
            constructDefaultRequestSettings_cb _hidl_cb) override;
    Return<void> configureStreams(
            const StreamConfiguration& requestedConfiguration,
            configureStreams_cb _hidl_cb) override;
    Return<void> processCaptureRequest(
            const hidl_vec<CaptureRequest>& requests,
            const hidl_vec<BufferCache>& cachesToRemove,
            processCaptureRequest_cb _hidl_cb) override;
    Return<void> getCaptureRequestMetadataQueue(
            getCaptureRequestMetadataQueue_cb _hidl_cb) override;
    Return<void> getCaptureResultMetadataQueue(
            getCaptureResultMetadataQueue_cb _hidl_cb) override;
    Return<Status> flush() override;
    Return<void> close() override;

private:
    void inspectRequestLocked(const CaptureRequest& request)
            REQUIRES(mInspectLock);
    void cleanupSignalsLocked() REQUIRES(mInspectLock);

    sp<ICameraDeviceSession> mUnderlying;
    const bool mIsFront;

    std::mutex mInspectLock;

    bool mLastFrontSignalEmitted GUARDED_BY(mInspectLock);
    bool mLastRearFlashSignalEmitted GUARDED_BY(mInspectLock);
};

}  // namespace camera_hal_wrapper
}  // namespace oplus
}  // namespace vendor
