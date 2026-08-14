/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "SunlightEnhancementService"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include "SunlightEnhancement.h"

namespace aidl {
namespace vendor {
namespace lineage {
namespace livedisplay {

namespace {
constexpr const char* kHbmPath = "/sys/kernel/oppo_display/hbm";
}  // anonymous namespace

SunlightEnhancement::SunlightEnhancement() {}

ndk::ScopedAStatus SunlightEnhancement::getEnabled(bool* _aidl_return) {
    std::string tmp;
    if (::android::base::ReadFileToString(kHbmPath, &tmp)) {
        *_aidl_return = std::stoi(::android::base::Trim(tmp)) > 0;
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << "Failed to read current SunlightEnhancement state from " << kHbmPath;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus SunlightEnhancement::setEnabled(bool enabled) {
    // HBM mode 3 is the outdoor sunlight brightness level (mode 1 is the
    // on-screen fingerprint brightness bump).
    if (!::android::base::WriteStringToFile(enabled ? "3" : "0", kHbmPath, true)) {
        LOG(ERROR) << "Failed to set SunlightEnhancement state on " << kHbmPath;
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
