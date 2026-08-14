/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "AntiFlickerService"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include "AntiFlicker.h"

namespace aidl {
namespace vendor {
namespace lineage {
namespace livedisplay {

namespace {
constexpr const char* kDimlayerBlEn = "/sys/kernel/oppo_display/dimlayer_bl_en";
}  // anonymous namespace

AntiFlicker::AntiFlicker() {}

ndk::ScopedAStatus AntiFlicker::getEnabled(bool* _aidl_return) {
    std::string tmp;
    if (::android::base::ReadFileToString(kDimlayerBlEn, &tmp)) {
        *_aidl_return = ::android::base::Trim(tmp) == "1";
        return ndk::ScopedAStatus::ok();
    }
    LOG(ERROR) << "Failed to read current AntiFlicker state from " << kDimlayerBlEn;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus AntiFlicker::setEnabled(bool enabled) {
    if (!::android::base::WriteStringToFile(enabled ? "1" : "0", kDimlayerBlEn, true)) {
        LOG(ERROR) << "Failed to set AntiFlicker state on " << kDimlayerBlEn;
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
