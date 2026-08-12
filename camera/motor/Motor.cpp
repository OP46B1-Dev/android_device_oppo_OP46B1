/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraMotorHal"

#include "Motor.h"

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace aidl::vendor::oplus::hardware::motor {
namespace {

constexpr off_t kCalibrationOffset = 0x434000;
constexpr size_t kCalibrationValueCount = 6;
constexpr size_t kCalibrationSize = kCalibrationValueCount * sizeof(int32_t);
constexpr int32_t kMoveStateUpward = 1;
constexpr int32_t kMoveStateDownward = 2;
constexpr useconds_t kStopPollIntervalMicros = 10'000;
constexpr int kStopPollAttempts = 150;

constexpr std::array<int32_t, kCalibrationValueCount> kFallbackCalibration = {
        -170, -170, -430, 0, 0, -430,
};

constexpr std::array<const char*, 1> kReservePaths = {
        "/dev/block/platform/soc/1d84000.ufshc/by-name/opporeserve1",
};

struct MotorNodeSet {
    const char* name;
    const char* direction;
    const char* start;
    const char* position;
    const char* moveState;
};

// CONFIG_MOTOR_CLASS_INTERFACE, enabled by the OP46B1 kernel, exposes this ABI.
constexpr MotorNodeSet kMotorClassNodes = {
        "motor class",
        "/sys/class/motor/direction",
        "/sys/class/motor/enable",
        "/sys/class/motor/position",
        "/sys/class/motor/move_state",
};

constexpr std::array<const MotorNodeSet*, 1> kControlNodeSets = {
        &kMotorClassNodes,
};

constexpr std::array<const char*, 1> kCalibrationPaths = {
        "/sys/class/motor/hall_calibration",
};

struct Failure {
    int error = EIO;
    std::string message;
};

void setErrnoFailure(Failure* failure, int error, const char* operation, const char* path) {
    failure->error = error == 0 ? EIO : error;
    failure->message = std::string(operation) + " " + path + ": " + std::strerror(failure->error);
}

ndk::ScopedAStatus statusFromFailure(const Failure& failure) {
    const int error = failure.error == 0 ? EIO : failure.error;
    return ndk::ScopedAStatus::fromServiceSpecificErrorWithMessage(error, failure.message.c_str());
}

ndk::ScopedAStatus illegalArgument(const char* message) {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT, message);
}

bool writeTextFile(const char* path, const std::string& value, Failure* failure) {
    int rawFd;
    do {
        rawFd = open(path, O_WRONLY | O_CLOEXEC);
    } while (rawFd < 0 && errno == EINTR);

    android::base::unique_fd fd(rawFd);
    if (fd.get() < 0) {
        setErrnoFailure(failure, errno, "open", path);
        return false;
    }

    ssize_t written;
    do {
        written = write(fd.get(), value.data(), value.size());
    } while (written < 0 && errno == EINTR);

    if (written < 0) {
        setErrnoFailure(failure, errno, "write", path);
        return false;
    }
    if (static_cast<size_t>(written) != value.size()) {
        failure->error = EIO;
        failure->message = "short write to " + std::string(path) + ": " + std::to_string(written) +
                           " of " + std::to_string(value.size()) + " bytes";
        return false;
    }
    return true;
}

bool readTextFile(const char* path, std::string* value, Failure* failure) {
    int rawFd;
    do {
        rawFd = open(path, O_RDONLY | O_CLOEXEC);
    } while (rawFd < 0 && errno == EINTR);

    android::base::unique_fd fd(rawFd);
    if (fd.get() < 0) {
        setErrnoFailure(failure, errno, "open", path);
        return false;
    }

    value->clear();
    std::array<char, 64> buffer{};
    for (;;) {
        ssize_t count;
        do {
            count = read(fd.get(), buffer.data(), buffer.size());
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            setErrnoFailure(failure, errno, "read", path);
            return false;
        }
        if (count == 0) {
            break;
        }
        if (value->size() + static_cast<size_t>(count) > 128) {
            failure->error = EOVERFLOW;
            failure->message = "oversized integer value from " + std::string(path);
            return false;
        }
        value->append(buffer.data(), static_cast<size_t>(count));
    }

    if (value->empty()) {
        failure->error = ENODATA;
        failure->message = "empty value from " + std::string(path);
        return false;
    }
    return true;
}

bool parseInt32(const std::string& text, int32_t* value, Failure* failure, const char* path) {
    errno = 0;
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str()) {
        failure->error = EINVAL;
        failure->message = "invalid integer from " + std::string(path) + ": " + text;
        return false;
    }
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
        ++end;
    }
    if (errno == ERANGE || parsed < std::numeric_limits<int32_t>::min() ||
        parsed > std::numeric_limits<int32_t>::max() || *end != '\0') {
        failure->error = EINVAL;
        failure->message = "invalid integer from " + std::string(path) + ": " + text;
        return false;
    }

    *value = static_cast<int32_t>(parsed);
    return true;
}

bool readIntFile(const char* path, int32_t* value, Failure* failure) {
    std::string text;
    return readTextFile(path, &text, failure) && parseInt32(text, value, failure, path);
}

int32_t directionForMoveState(int32_t moveState) {
    if (moveState == kMoveStateUpward) {
        return IMotor::DIRECTION_UP;
    }
    if (moveState == kMoveStateDownward) {
        return IMotor::DIRECTION_DOWN;
    }
    return -1;
}

bool waitUntilStopped(const MotorNodeSet& nodes, Failure* failure) {
    for (int attempt = 0; attempt < kStopPollAttempts; ++attempt) {
        int32_t moveState;
        if (!readIntFile(nodes.moveState, &moveState, failure)) {
            return false;
        }
        if (directionForMoveState(moveState) < 0) {
            return true;
        }
        usleep(kStopPollIntervalMicros);
    }

    failure->error = ETIMEDOUT;
    failure->message = "timed out waiting for motor to stop through " +
                       std::string(nodes.moveState);
    return false;
}

const MotorNodeSet* selectControlNodeSet(Failure* failure) {
    Failure lastFailure;
    for (const MotorNodeSet* nodes : kControlNodeSets) {
        const std::array<const char*, 4> paths = {
                nodes->direction,
                nodes->start,
                nodes->position,
                nodes->moveState,
        };
        bool complete = true;
        for (const char* path : paths) {
            struct stat statBuffer{};
            if (stat(path, &statBuffer) != 0) {
                setErrnoFailure(&lastFailure, errno, "stat", path);
                complete = false;
                break;
            }
        }
        if (complete) {
            return nodes;
        }
    }

    failure->error = lastFailure.error;
    failure->message = "no complete motor sysfs layout found; last error: " + lastFailure.message;
    return nullptr;
}

template <size_t N>
bool writeFirstAvailable(const std::array<const char*, N>& paths, const std::string& value,
                         const char** usedPath, Failure* failure) {
    Failure lastFailure;
    for (const char* path : paths) {
        if (writeTextFile(path, value, &lastFailure)) {
            *usedPath = path;
            return true;
        }
        if (lastFailure.error != ENOENT && lastFailure.error != ENOTDIR) {
            *failure = lastFailure;
            return false;
        }
    }
    *failure = lastFailure;
    return false;
}

bool preadFully(int fd, void* data, size_t size, off_t offset, const char* path, Failure* failure) {
    auto* bytes = static_cast<uint8_t*>(data);
    size_t total = 0;
    while (total < size) {
        ssize_t count;
        do {
            count = pread(fd, bytes + total, size - total, offset + static_cast<off_t>(total));
        } while (count < 0 && errno == EINTR);

        if (count < 0) {
            setErrnoFailure(failure, errno, "pread", path);
            return false;
        }
        if (count == 0) {
            failure->error = ENODATA;
            failure->message = "short calibration read from " + std::string(path) + ": " +
                               std::to_string(total) + " of " + std::to_string(size) + " bytes";
            return false;
        }
        total += static_cast<size_t>(count);
    }
    return true;
}

int32_t decodeLittleEndianInt32(const uint8_t* bytes) {
    const uint32_t raw = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
                         (static_cast<uint32_t>(bytes[2]) << 16) |
                         (static_cast<uint32_t>(bytes[3]) << 24);
    const int64_t signedValue = raw <= static_cast<uint32_t>(INT32_MAX)
                                        ? static_cast<int64_t>(raw)
                                        : static_cast<int64_t>(raw) - (INT64_C(1) << 32);
    return static_cast<int32_t>(signedValue);
}

bool readFactoryCalibration(std::array<int32_t, kCalibrationValueCount>* values,
                            const char** usedPath, Failure* failure) {
    Failure lastFailure;
    for (const char* path : kReservePaths) {
        int rawFd;
        do {
            rawFd = open(path, O_RDONLY | O_CLOEXEC);
        } while (rawFd < 0 && errno == EINTR);

        android::base::unique_fd fd(rawFd);
        if (fd.get() < 0) {
            setErrnoFailure(&lastFailure, errno, "open", path);
            continue;
        }

        std::array<uint8_t, kCalibrationSize> raw{};
        if (!preadFully(fd.get(), raw.data(), raw.size(), kCalibrationOffset, path, &lastFailure)) {
            continue;
        }

        bool allZero = true;
        for (size_t i = 0; i < values->size(); ++i) {
            (*values)[i] = decodeLittleEndianInt32(raw.data() + i * sizeof(int32_t));
            allZero = allZero && ((*values)[i] == 0);
        }
        if (allZero) {
            lastFailure.error = ENODATA;
            lastFailure.message = "all-zero calibration record in " + std::string(path);
            continue;
        }

        *usedPath = path;
        return true;
    }

    *failure = lastFailure;
    return false;
}

std::string formatCalibration(const std::array<int32_t, kCalibrationValueCount>& calibration) {
    std::string result;
    for (size_t i = 0; i < calibration.size(); ++i) {
        if (i != 0) {
            result += ',';
        }
        result += std::to_string(calibration[i]);
    }
    return result;
}

}  // namespace

ndk::ScopedAStatus Motor::initializeCalibration() {
    std::lock_guard<std::mutex> lock(mLock);
    return initializeCalibrationLocked();
}

ndk::ScopedAStatus Motor::initializeCalibrationLocked() {
    if (mCalibrationInitialized) {
        return ndk::ScopedAStatus::ok();
    }

    std::array<int32_t, kCalibrationValueCount> calibration{};
    const char* reservePath = nullptr;
    Failure readFailure;
    const bool fromFactory = readFactoryCalibration(&calibration, &reservePath, &readFailure);
    if (!fromFactory) {
        calibration = kFallbackCalibration;
        LOG(WARNING) << "Factory motor calibration unavailable (" << readFailure.message
                     << "); using fallback";
    }

    const std::string value = formatCalibration(calibration);
    const char* calibrationPath = nullptr;
    Failure writeFailure;
    if (!writeFirstAvailable(kCalibrationPaths, value, &calibrationPath, &writeFailure)) {
        LOG(ERROR) << "Motor calibration write failed: " << writeFailure.message;
        return statusFromFailure(writeFailure);
    }

    mCalibrationInitialized = true;
    LOG(INFO) << "Initialized " << (fromFactory ? "factory" : "fallback")
              << " motor calibration from " << (fromFactory ? reservePath : "built-in values")
              << " through " << calibrationPath;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Motor::move(int32_t direction, int32_t startMode) {
    if (direction != IMotor::DIRECTION_DOWN && direction != IMotor::DIRECTION_UP) {
        return illegalArgument("direction must be DIRECTION_DOWN or DIRECTION_UP");
    }
    if (startMode != IMotor::START_NORMAL && startMode != IMotor::START_FORCE) {
        return illegalArgument("startMode must be START_NORMAL or START_FORCE");
    }

    std::lock_guard<std::mutex> lock(mLock);
    ndk::ScopedAStatus calibrationStatus = initializeCalibrationLocked();
    if (!calibrationStatus.isOk()) {
        return calibrationStatus;
    }

    Failure failure;
    const MotorNodeSet* nodes = selectControlNodeSet(&failure);
    if (nodes == nullptr) {
        return statusFromFailure(failure);
    }

    int32_t moveState;
    if (!readIntFile(nodes->moveState, &moveState, &failure)) {
        return statusFromFailure(failure);
    }
    const int32_t movingDirection = directionForMoveState(moveState);
    if (movingDirection == direction && startMode == IMotor::START_NORMAL) {
        LOG(INFO) << "Motor already moving in requested direction " << direction;
        return ndk::ScopedAStatus::ok();
    }
    if (movingDirection >= 0) {
        LOG(INFO) << "Stopping direction " << movingDirection << " before "
                  << (movingDirection == direction ? "force-restarting" : "reversing to") << ' '
                  << direction;
        if (!writeTextFile(nodes->start, "0", &failure) ||
            !waitUntilStopped(*nodes, &failure)) {
            LOG(ERROR) << "Motor restart failed: " << failure.message;
            return statusFromFailure(failure);
        }
    }

    if (!writeTextFile(nodes->direction, std::to_string(direction), &failure) ||
        !writeTextFile(nodes->start, std::to_string(startMode), &failure)) {
        LOG(ERROR) << "Motor move failed: " << failure.message;
        return statusFromFailure(failure);
    }

    LOG(INFO) << "Motor move via " << nodes->name << ": direction=" << direction
              << " startMode=" << startMode;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Motor::getPosition(int32_t* _aidl_return) {
    if (_aidl_return == nullptr) {
        return illegalArgument("position output must not be null");
    }

    std::lock_guard<std::mutex> lock(mLock);
    Failure failure;
    const MotorNodeSet* nodes = selectControlNodeSet(&failure);
    if (nodes == nullptr || !readIntFile(nodes->position, _aidl_return, &failure)) {
        return statusFromFailure(failure);
    }
    if (*_aidl_return < IMotor::POSITION_UNKNOWN || *_aidl_return > IMotor::POSITION_MID) {
        failure.error = EPROTO;
        failure.message = "invalid motor position " + std::to_string(*_aidl_return) + " from " +
                          nodes->position;
        return statusFromFailure(failure);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Motor::getMoveState(int32_t* _aidl_return) {
    if (_aidl_return == nullptr) {
        return illegalArgument("move-state output must not be null");
    }

    std::lock_guard<std::mutex> lock(mLock);
    Failure failure;
    const MotorNodeSet* nodes = selectControlNodeSet(&failure);
    if (nodes == nullptr || !readIntFile(nodes->moveState, _aidl_return, &failure)) {
        return statusFromFailure(failure);
    }
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::vendor::oplus::hardware::motor
