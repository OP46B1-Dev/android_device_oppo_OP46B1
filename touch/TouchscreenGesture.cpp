/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "vendor.lineage.touch-service.OP46B1"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include "TouchscreenGestureConfig.h"

using ::android::base::ReadFileToString;
using ::android::base::Trim;
using ::android::base::WriteStringToFile;

namespace {
constexpr const char* kGestureEnableIndepPath = "/proc/touchpanel/double_tap_enable_indep";
}  // anonymous namespace

namespace aidl {
namespace vendor {
namespace lineage {
namespace touch {

ndk::ScopedAStatus TouchscreenGesture::getSupportedGestures(std::vector<Gesture>* _aidl_return) {
    std::vector<Gesture> gestures;

    for (const auto& [id, name] : kGestureNames) {
        if (kSupportedGestures & (1 << id)) {
            gestures.push_back({static_cast<int>(gestures.size()), name, kGestureStartKey + id});
        }
    }

    *_aidl_return = gestures;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus TouchscreenGesture::setGestureEnabled(const Gesture& gesture, bool enabled) {
    std::string tmp;
    int contents = 0;

    // The kernel node is read back in hex and written in decimal.
    if (!ReadFileToString(kGestureEnableIndepPath, &tmp)) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    contents = std::stoi(Trim(tmp), nullptr, 16);

    if (enabled) {
        contents |= (1 << (gesture.keycode - kGestureStartKey));
    } else {
        contents &= ~(1 << (gesture.keycode - kGestureStartKey));
    }

    if (!WriteStringToFile(std::to_string(contents), kGestureEnableIndepPath, true)) {
        LOG(ERROR) << "Failed to write gesture enable state";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    return ndk::ScopedAStatus::ok();
}

}  // namespace touch
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
