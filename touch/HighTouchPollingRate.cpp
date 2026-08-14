/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.OP46B1"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include "HighTouchPollingRate.h"

using ::android::base::ReadFileToString;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {
constexpr const char* kGameSwitchEnablePath = "/proc/touchpanel/game_switch_enable";
}  // anonymous namespace

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

HighTouchPollingRate::HighTouchPollingRate() {}

ndk::ScopedAStatus HighTouchPollingRate::getEnabled(bool* _aidl_return) {
    std::string value;

    if (!ReadFileToString(kGameSwitchEnablePath, &value)) {
        LOG(ERROR) << "Failed to read current HighTouchPollingRate state";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    *_aidl_return = Trim(value) != "0";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus HighTouchPollingRate::setEnabled(bool enabled) {
    if (!WriteStringToFile(enabled ? "1" : "0", kGameSwitchEnablePath, true)) {
        LOG(ERROR) << "Failed to write HighTouchPollingRate state";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    return ndk::ScopedAStatus::ok();
}

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
