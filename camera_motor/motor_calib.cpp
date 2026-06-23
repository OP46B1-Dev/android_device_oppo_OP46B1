/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * One-shot boot helper that restores the per-device factory motor hall
 * calibration to /sys/class/motor/hall_calibration.
 *
 * Why this exists: the kernel loads a generic, UNCALIBRATED hall-irq threshold
 * from the device tree on every boot. That default is wrong for this unit, so
 * the kernel mis-detects arrival and keeps driving past the top position
 * (over-drive). The official system's engineer HAL
 * (vendor-oplus-hardware-engineer@1.0-service) overwrites it at boot with the
 * per-device factory value read from the reserve partition; LineageOS ships no
 * engineer HAL, so this binary reproduces that one restore.
 *
 * This runs as a root one-shot (see vendor.oplus.motor-calib-restore.rc), which
 * is why it needs no permission dance on the reserve block node: root reads the
 * 0600 node directly and writes the sysfs node directly. The reserve partition
 * survives the LineageOS flash (raw block partition), so the value stays
 * per-device correct with no hardcoding. Verified against the official engineer
 * HAL via IDA: offset 0x434000 on this UFS device; 24 bytes = 6x int32 LE;
 * all-zero record is skipped.
 */

#define LOG_TAG "CameraMotorCalibRestore"

#include <android-base/logging.h>

#include <cstdint>
#include <cstdio>
#include <fcntl.h>
#include <string>
#include <unistd.h>

namespace {

constexpr const char* kPathReserve = "/dev/block/bootdevice/by-name/opporeserve1";
constexpr const char* kPathHallCalib = "/sys/class/motor/hall_calibration";
constexpr off_t kOffset = 0x434000;

// Fallback used when the per-device factory value can't be obtained
// (reserve partition absent, short read, or all-zero invalid record).
constexpr const char* kFallbackCalib = "-170,-170,-430,0,0,-430";

bool writeSysfs(const char* path, const std::string& value) {
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) {
        PLOG(ERROR) << "open " << path << " fail";
        return false;
    }
    ssize_t n = write(fd, value.c_str(), value.size());
    close(fd);
    if (n != static_cast<ssize_t>(value.size())) {
        PLOG(ERROR) << "write " << path << " fail";
        return false;
    }
    return true;
}

}  // namespace

int main() {
    std::string calib = kFallbackCalib;
    bool fromFactory = false;

    int fd = open(kPathReserve, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        PLOG(ERROR) << "open " << kPathReserve << " fail, using fallback";
    } else {
        int32_t data[6] = {};
        ssize_t got = pread(fd, data, sizeof(data), kOffset);
        close(fd);
        if (got != static_cast<ssize_t>(sizeof(data))) {
            PLOG(ERROR) << "read " << kPathReserve << " @0x" << std::hex << kOffset << " got "
                        << std::dec << got << " of " << sizeof(data) << ", using fallback";
        } else {
            // Official skips an all-zero record ("invalid motor hall cali data").
            bool allZero = true;
            for (int32_t v : data) {
                if (v != 0) {
                    allZero = false;
                    break;
                }
            }
            if (allZero) {
                LOG(INFO) << "invalid (all-zero) motor hall cali data, using fallback";
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf), "%d,%d,%d,%d,%d,%d", data[0], data[1], data[2],
                         data[3], data[4], data[5]);
                calib = buf;
                fromFactory = true;
            }
        }
    }

    if (writeSysfs(kPathHallCalib, calib)) {
        LOG(INFO) << "wrote " << (fromFactory ? "factory" : "fallback") << " hall calib "
                  << calib;
    }
    return 0;
}
