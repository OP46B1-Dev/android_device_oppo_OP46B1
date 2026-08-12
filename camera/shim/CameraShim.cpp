/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "OPlusCameraShim"

#include <android/log.h>
#include <dlfcn.h>
#include <elf.h>
#include <hardware/camera3.h>
#include <hardware/camera_common.h>
#include <hardware/gralloc.h>
#include <link.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>
#include <vendor/oplus/hardware/camera/signal/1.0/types.h>

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <new>

#include "SignalDispatcher.h"

namespace oplus::camera::shim {

using ::vendor::oplus::hardware::camera::signal::V1_0::CameraState;

namespace {

struct WrappedCameraDevice {
    camera3_device_t device{};
    camera3_device_ops_t ops{};
    camera3_device_t* delegate = nullptr;

    std::mutex lock;
    int32_t cameraId = -1;
    CameraState state = CameraState::IDLE;
    int64_t sessionId = 0;
    int32_t configurationId = 0;
    uint8_t captureIntent = ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW;
    uint8_t aeMode = ANDROID_CONTROL_AE_MODE_ON;
    uint8_t flashMode = ANDROID_FLASH_MODE_OFF;
    bool frontFacing = false;
    bool videoConfigured = false;
};

camera_module_t* gCameraModule = nullptr;
hw_module_methods_t gWrappedModuleMethods{};
int (*gOriginalOpen)(const hw_module_t*, const char*, hw_device_t**) = nullptr;
std::atomic<int64_t> gSequence{0};
std::atomic<int64_t> gLastSessionId{0};

int64_t nextSessionId() {
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    int64_t candidate = static_cast<int64_t>(now.tv_sec) * 1000000000LL + now.tv_nsec;
    int64_t previous = gLastSessionId.load(std::memory_order_relaxed);
    do {
        if (candidate <= previous) {
            candidate = previous + 1;
        }
    } while (!gLastSessionId.compare_exchange_weak(previous, candidate, std::memory_order_relaxed));
    return candidate;
}

uint32_t gnuHash(const char* name) {
    uint32_t hash = 5381;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(name); *p; ++p) {
        hash = hash * 33 + *p;
    }
    return hash;
}

template <typename T>
T* loadedAddress(ElfW(Addr) base, ElfW(Addr) value) {
    uintptr_t address = static_cast<uintptr_t>(value);
    if (base != 0 && address < static_cast<uintptr_t>(base)) {
        address += static_cast<uintptr_t>(base);
    }
    return reinterpret_cast<T*>(address);
}

void* findGnuSymbol(const dl_phdr_info* info, const char* wanted) {
    const ElfW(Dyn)* dynamic = nullptr;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        if (info->dlpi_phdr[i].p_type == PT_DYNAMIC) {
            dynamic = reinterpret_cast<const ElfW(Dyn)*>(info->dlpi_addr +
                                                         info->dlpi_phdr[i].p_vaddr);
            break;
        }
    }
    if (dynamic == nullptr) {
        return nullptr;
    }

    const ElfW(Sym)* symbols = nullptr;
    const char* strings = nullptr;
    const uint32_t* hashTable = nullptr;
    for (const ElfW(Dyn)* entry = dynamic; entry->d_tag != DT_NULL; ++entry) {
        switch (entry->d_tag) {
            case DT_SYMTAB:
                symbols = loadedAddress<ElfW(Sym)>(info->dlpi_addr, entry->d_un.d_ptr);
                break;
            case DT_STRTAB:
                strings = loadedAddress<char>(info->dlpi_addr, entry->d_un.d_ptr);
                break;
            case DT_GNU_HASH:
                hashTable = loadedAddress<uint32_t>(info->dlpi_addr, entry->d_un.d_ptr);
                break;
            default:
                break;
        }
    }
    if (symbols == nullptr || strings == nullptr || hashTable == nullptr) {
        return nullptr;
    }

    const uint32_t bucketCount = hashTable[0];
    const uint32_t symbolOffset = hashTable[1];
    const uint32_t bloomSize = hashTable[2];
    const uint32_t bloomShift = hashTable[3];
    if (bucketCount == 0 || bloomSize == 0) {
        return nullptr;
    }

    const auto* bloom = reinterpret_cast<const ElfW(Addr)*>(hashTable + 4);
    const auto* buckets = reinterpret_cast<const uint32_t*>(bloom + bloomSize);
    const auto* chains = buckets + bucketCount;
    constexpr uint32_t kWordBits = sizeof(ElfW(Addr)) * 8;
    const uint32_t hash = gnuHash(wanted);
    const ElfW(Addr) mask = (static_cast<ElfW(Addr)>(1) << (hash % kWordBits)) |
                            (static_cast<ElfW(Addr)>(1) << ((hash >> bloomShift) % kWordBits));
    if ((bloom[(hash / kWordBits) % bloomSize] & mask) != mask) {
        return nullptr;
    }

    uint32_t index = buckets[hash % bucketCount];
    if (index < symbolOffset) {
        return nullptr;
    }
    for (;;) {
        const uint32_t chainHash = chains[index - symbolOffset];
        if ((chainHash | 1U) == (hash | 1U) &&
            std::strcmp(strings + symbols[index].st_name, wanted) == 0 &&
            symbols[index].st_shndx != SHN_UNDEF) {
            return reinterpret_cast<void*>(info->dlpi_addr + symbols[index].st_value);
        }
        if ((chainHash & 1U) != 0) {
            return nullptr;
        }
        ++index;
    }
}

struct SymbolSearch {
    const char* objectName;
    const char* symbolName;
    void* result;
};

int findLoadedSymbolCallback(dl_phdr_info* info, size_t, void* data) {
    auto* search = static_cast<SymbolSearch*>(data);
    const char* slash = std::strrchr(info->dlpi_name, '/');
    const char* baseName = slash == nullptr ? info->dlpi_name : slash + 1;
    if (std::strcmp(baseName, search->objectName) != 0) {
        return 0;
    }
    search->result = findGnuSymbol(info, search->symbolName);
    return search->result == nullptr ? 0 : 1;
}

void* findLoadedSymbol(const char* objectName, const char* symbolName) {
    SymbolSearch search{objectName, symbolName, nullptr};
    dl_iterate_phdr(findLoadedSymbolCallback, &search);
    return search.result;
}

camera_module_t* findCameraModule() {
    constexpr const char* kPaths[] = {
            "camera.qcom.so",
#if defined(__LP64__)
            "/vendor/lib64/hw/camera.qcom.so",
#else
            "/vendor/lib/hw/camera.qcom.so",
#endif
    };
    for (const char* path : kPaths) {
        void* handle = dlopen(path, RTLD_NOW | RTLD_NOLOAD);
        if (handle == nullptr) {
            continue;
        }
        auto* module = static_cast<camera_module_t*>(dlsym(handle, HAL_MODULE_INFO_SYM_AS_STR));
        dlclose(handle);
        if (module != nullptr) {
            return module;
        }
    }
    return static_cast<camera_module_t*>(
            findLoadedSymbol("camera.qcom.so", HAL_MODULE_INFO_SYM_AS_STR));
}

bool readByte(const camera_metadata_t* metadata, uint32_t tag, uint8_t* value) {
    camera_metadata_ro_entry_t entry{};
    if (metadata == nullptr || find_camera_metadata_ro_entry(metadata, tag, &entry) != 0 ||
        entry.count == 0 || entry.data.u8 == nullptr) {
        return false;
    }
    *value = entry.data.u8[0];
    return true;
}

bool hasVideoStream(const camera3_stream_configuration_t* configuration) {
    if (configuration == nullptr || configuration->streams == nullptr) {
        return false;
    }
    for (uint32_t i = 0; i < configuration->num_streams; ++i) {
        const camera3_stream_t* stream = configuration->streams[i];
        if (stream != nullptr && stream->stream_type != CAMERA3_STREAM_INPUT &&
            (stream->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) != 0 &&
            stream->data_space != HAL_DATASPACE_HEIF) {
            return true;
        }
    }
    return false;
}

CameraState deriveRearState(const WrappedCameraDevice* wrapper) {
    if (wrapper->videoConfigured ||
        wrapper->captureIntent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD ||
        wrapper->captureIntent == ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT) {
        return CameraState::REAR_VIDEO;
    }
    if (wrapper->aeMode == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH ||
        wrapper->aeMode == ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE) {
        return CameraState::REAR_AUTO_FLASH;
    }
    if (wrapper->aeMode == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH ||
        wrapper->flashMode == ANDROID_FLASH_MODE_SINGLE ||
        wrapper->flashMode == ANDROID_FLASH_MODE_TORCH) {
        return CameraState::REAR_ALWAYS_FLASH;
    }
    return CameraState::IDLE;
}

void publishState(WrappedCameraDevice* wrapper, CameraState state, bool force) {
    if (wrapper->cameraId < 0) {
        return;
    }

    CameraSnapshot snapshot{};
    {
        std::lock_guard<std::mutex> guard(wrapper->lock);
        if (!force && wrapper->state == state) {
            return;
        }
        wrapper->state = state;
        snapshot.cameraId = wrapper->cameraId;
        snapshot.state = state;
        snapshot.sessionId = wrapper->sessionId;
        snapshot.sequence = gSequence.fetch_add(1, std::memory_order_relaxed) + 1;
        snapshot.configurationId = wrapper->configurationId;
    }
    SignalDispatcher::getInstance().submit(snapshot);
}

WrappedCameraDevice* unwrap(const camera3_device_t* device) {
    return reinterpret_cast<WrappedCameraDevice*>(const_cast<camera3_device_t*>(device));
}

int initializeCamera(const camera3_device_t* device, const camera3_callback_ops_t* callbackOps) {
    WrappedCameraDevice* wrapper = unwrap(device);
    return wrapper->delegate->ops->initialize(wrapper->delegate, callbackOps);
}

int configureStreams(const camera3_device_t* device,
                     camera3_stream_configuration_t* configuration) {
    WrappedCameraDevice* wrapper = unwrap(device);
    const int result = wrapper->delegate->ops->configure_streams(wrapper->delegate, configuration);
    if (result != 0) {
        return result;
    }

    CameraState state;
    {
        std::lock_guard<std::mutex> guard(wrapper->lock);
        ++wrapper->configurationId;
        wrapper->videoConfigured = hasVideoStream(configuration);
        state = wrapper->frontFacing ? CameraState::FRONT_CAMERA : deriveRearState(wrapper);
    }
    // Retain the last request metadata until the first request for the new
    // configuration arrives. This avoids an artificial idle edge while moving
    // between video and flash-assisted photo configurations.
    publishState(wrapper, state, true);
    return result;
}

int registerStreamBuffers(const camera3_device_t* device,
                          const camera3_stream_buffer_set_t* bufferSet) {
    WrappedCameraDevice* wrapper = unwrap(device);
    return wrapper->delegate->ops->register_stream_buffers(wrapper->delegate, bufferSet);
}

const camera_metadata_t* constructDefaultRequestSettings(const camera3_device_t* device, int type) {
    WrappedCameraDevice* wrapper = unwrap(device);
    return wrapper->delegate->ops->construct_default_request_settings(wrapper->delegate, type);
}

int processCaptureRequest(const camera3_device_t* device, camera3_capture_request_t* request) {
    WrappedCameraDevice* wrapper = unwrap(device);
    uint8_t captureIntent;
    uint8_t aeMode;
    uint8_t flashMode;
    {
        std::lock_guard<std::mutex> guard(wrapper->lock);
        captureIntent = wrapper->captureIntent;
        aeMode = wrapper->aeMode;
        flashMode = wrapper->flashMode;
    }
    if (request != nullptr && request->settings != nullptr) {
        readByte(request->settings, ANDROID_CONTROL_CAPTURE_INTENT, &captureIntent);
        readByte(request->settings, ANDROID_CONTROL_AE_MODE, &aeMode);
        readByte(request->settings, ANDROID_FLASH_MODE, &flashMode);
    }

    const int result = wrapper->delegate->ops->process_capture_request(wrapper->delegate, request);
    if (result != 0) {
        return result;
    }

    CameraState state;
    {
        std::lock_guard<std::mutex> guard(wrapper->lock);
        wrapper->captureIntent = captureIntent;
        wrapper->aeMode = aeMode;
        wrapper->flashMode = flashMode;
        state = wrapper->frontFacing ? CameraState::FRONT_CAMERA : deriveRearState(wrapper);
    }
    publishState(wrapper, state, false);
    return result;
}

void getMetadataVendorTagOps(const camera3_device_t* device, vendor_tag_query_ops_t* ops) {
    WrappedCameraDevice* wrapper = unwrap(device);
    wrapper->delegate->ops->get_metadata_vendor_tag_ops(wrapper->delegate, ops);
}

void dumpCamera(const camera3_device_t* device, int fd) {
    WrappedCameraDevice* wrapper = unwrap(device);
    wrapper->delegate->ops->dump(wrapper->delegate, fd);
}

int flushCamera(const camera3_device_t* device) {
    WrappedCameraDevice* wrapper = unwrap(device);
    return wrapper->delegate->ops->flush(wrapper->delegate);
}

void signalStreamFlush(const camera3_device_t* device, uint32_t numStreams,
                       const camera3_stream_t* const* streams) {
    WrappedCameraDevice* wrapper = unwrap(device);
    wrapper->delegate->ops->signal_stream_flush(wrapper->delegate, numStreams, streams);
}

int isReconfigurationRequired(const camera3_device_t* device,
                              const camera_metadata_t* oldSessionParams,
                              const camera_metadata_t* newSessionParams) {
    WrappedCameraDevice* wrapper = unwrap(device);
    return wrapper->delegate->ops->is_reconfiguration_required(wrapper->delegate, oldSessionParams,
                                                               newSessionParams);
}

int closeCamera(hw_device_t* device) {
    auto* wrapper = reinterpret_cast<WrappedCameraDevice*>(device);
    publishState(wrapper, CameraState::IDLE, true);
    const int result = wrapper->delegate->common.close(&wrapper->delegate->common);
    delete wrapper;
    return result;
}

int parseCameraId(const char* name) {
    if (name == nullptr || *name == '\0') {
        return -1;
    }
    errno = 0;
    char* end = nullptr;
    const long value = std::strtol(name, &end, 10);
    if (errno != 0 || end == name || *end != '\0' || value < 0 || value > INT32_MAX) {
        return -1;
    }
    return static_cast<int>(value);
}

int openCamera(const hw_module_t*, const char* name, hw_device_t** device) {
    const int result = gOriginalOpen(&gCameraModule->common, name, device);
    if (result != 0 || device == nullptr || *device == nullptr) {
        return result;
    }

    auto* delegate = reinterpret_cast<camera3_device_t*>(*device);
    if (delegate->ops == nullptr || delegate->common.close == nullptr ||
        delegate->common.version < CAMERA_DEVICE_API_VERSION_3_0) {
        return result;
    }

    auto* wrapper = new (std::nothrow) WrappedCameraDevice;
    if (wrapper == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "failed to allocate camera wrapper");
        return result;
    }

    wrapper->delegate = delegate;
    wrapper->device = *delegate;
    wrapper->ops = *delegate->ops;
    wrapper->device.common.close = closeCamera;
    wrapper->device.ops = &wrapper->ops;
    wrapper->device.priv = wrapper;
    wrapper->cameraId = parseCameraId(name);
    wrapper->sessionId = nextSessionId();

    camera_info info{};
    if (wrapper->cameraId >= 0 && gCameraModule->get_camera_info != nullptr &&
        gCameraModule->get_camera_info(wrapper->cameraId, &info) == 0) {
        wrapper->frontFacing = info.facing == CAMERA_FACING_FRONT;
    } else {
        __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "unable to determine facing for camera %s",
                            name);
    }

    if (wrapper->ops.initialize != nullptr) {
        wrapper->ops.initialize = initializeCamera;
    }
    if (wrapper->ops.configure_streams != nullptr) {
        wrapper->ops.configure_streams = configureStreams;
    }
    if (wrapper->ops.register_stream_buffers != nullptr) {
        wrapper->ops.register_stream_buffers = registerStreamBuffers;
    }
    if (wrapper->ops.construct_default_request_settings != nullptr) {
        wrapper->ops.construct_default_request_settings = constructDefaultRequestSettings;
    }
    if (wrapper->ops.process_capture_request != nullptr) {
        wrapper->ops.process_capture_request = processCaptureRequest;
    }
    if (wrapper->ops.get_metadata_vendor_tag_ops != nullptr) {
        wrapper->ops.get_metadata_vendor_tag_ops = getMetadataVendorTagOps;
    }
    if (wrapper->ops.dump != nullptr) {
        wrapper->ops.dump = dumpCamera;
    }
    if (delegate->common.version >= CAMERA_DEVICE_API_VERSION_3_1 &&
        wrapper->ops.flush != nullptr) {
        wrapper->ops.flush = flushCamera;
    }
    if (delegate->common.version >= CAMERA_DEVICE_API_VERSION_3_6 &&
        wrapper->ops.signal_stream_flush != nullptr) {
        wrapper->ops.signal_stream_flush = signalStreamFlush;
    }
    if (delegate->common.version >= CAMERA_DEVICE_API_VERSION_3_6 &&
        wrapper->ops.is_reconfiguration_required != nullptr) {
        wrapper->ops.is_reconfiguration_required = isReconfigurationRequired;
    }

    *device = &wrapper->device.common;
    publishState(
            wrapper,
            wrapper->frontFacing ? CameraState::FRONT_CAMERA : CameraState::IDLE,
            true);
    return result;
}

void installCameraShim() {
    gCameraModule = findCameraModule();
    if (gCameraModule == nullptr || gCameraModule->common.methods == nullptr ||
        gCameraModule->common.methods->open == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                            "camera.qcom HMI was not available for wrapping");
        return;
    }

    gWrappedModuleMethods = *gCameraModule->common.methods;
    gOriginalOpen = gWrappedModuleMethods.open;
    gWrappedModuleMethods.open = openCamera;
    gCameraModule->common.methods = &gWrappedModuleMethods;
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "camera.qcom camera3 shim installed");
}

}  // namespace

}  // namespace oplus::camera::shim

__attribute__((constructor)) static void initializeOPlusCameraShim() {
    oplus::camera::shim::installCameraShim();
}
