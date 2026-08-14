/*
 * Copyright (C) 2026 The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <aidl/vendor/lineage/livedisplay/BnDisplayModes.h>
#include <map>

namespace aidl {
namespace vendor {
namespace lineage {
namespace livedisplay {

class DisplayModes : public BnDisplayModes {
  public:
    DisplayModes();

    // Methods from ::aidl::vendor::lineage::livedisplay::BnDisplayModes follow.
    ndk::ScopedAStatus getDisplayModes(std::vector<DisplayMode>* _aidl_return) override;
    ndk::ScopedAStatus getCurrentDisplayMode(DisplayMode* _aidl_return) override;
    ndk::ScopedAStatus getDefaultDisplayMode(DisplayMode* _aidl_return) override;
    ndk::ScopedAStatus setDisplayMode(int32_t modeID, bool makeDefault) override;

  private:
    // Mode id -> (name, seed value written to /sys/kernel/oppo_display/seed).
    // The Samsung AMS641RW01 panel implements SEED CRC LUTs for modes 0/1 and
    // falls back to the seed-off command for any other value.
    struct ModeInfo {
        std::string name;
        int seed;
    };

    const std::map<int32_t, ModeInfo> kModeMap = {
            {0, {"Vivid", 0}},
            {1, {"Natural", 1}},
            {2, {"Cinematic", -1}},
    };

    int32_t mCurrentModeId;
    int32_t mDefaultModeId;
};

}  // namespace livedisplay
}  // namespace lineage
}  // namespace vendor
}  // namespace aidl
