/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <jni.h>

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>

#include <atomic>

#include <android/log.h>
#include <sys/system_properties.h>

extern "C" uint32_t __system_property_area_serial(void);

#define LOG_TAG "CameraPropWait"
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace {

JavaVM* g_vm = nullptr;
jobject g_callback_ref = nullptr;
jmethodID g_callback_method = nullptr;
std::atomic<bool> g_running{false};
pthread_t g_thread;

uint32_t g_initial_serial = 0;

constexpr timespec kWaitTimeout = { .tv_sec = 1, .tv_nsec = 0 };

void* wait_thread(void* /*arg*/) {
    JNIEnv* env = nullptr;
    JavaVMAttachArgs args = { JNI_VERSION_1_4, "PropWaitThread", nullptr };
    if (g_vm->AttachCurrentThread(&env, &args) != JNI_OK) {
        ALOGE("AttachCurrentThread failed");
        return nullptr;
    }

    uint32_t last_serial = g_initial_serial;
    ALOGI("prop wait thread started, last_serial=%u", last_serial);

    while (g_running.load()) {
        uint32_t new_serial = 0;
        bool ok = __system_property_wait(nullptr, last_serial, &new_serial,
                                          &kWaitTimeout);
        if (!ok) {
            if (errno != ETIMEDOUT) {
                ALOGE("__system_property_wait failed: %s", strerror(errno));
                timespec retry = { .tv_sec = 1, .tv_nsec = 0 };
                nanosleep(&retry, nullptr);
            }
            continue;
        }
        last_serial = new_serial;

        if (g_callback_ref != nullptr && g_callback_method != nullptr) {
            env->CallVoidMethod(g_callback_ref, g_callback_method);
            if (env->ExceptionCheck()) {
                env->ExceptionDescribe();
                env->ExceptionClear();
            }
        }
    }

    g_vm->DetachCurrentThread();
    ALOGI("prop wait thread exited");
    return nullptr;
}

}  // namespace

extern "C" {

JNIEXPORT void JNICALL
Java_org_lineageos_camerahelper_CameraMotorService_nativeStartPropWatch(
        JNIEnv* env, jclass /*clazz*/, jobject callback) {
    if (g_running.load()) {
        ALOGE("prop watch already running");
        return;
    }
    if (g_vm == nullptr) {
        env->GetJavaVM(&g_vm);
    }
    g_initial_serial = __system_property_area_serial();
    g_callback_ref = env->NewGlobalRef(callback);
    jclass cls = env->GetObjectClass(callback);
    g_callback_method = env->GetMethodID(cls, "run", "()V");
    env->DeleteLocalRef(cls);
    if (g_callback_method == nullptr) {
        ALOGE("GetMethodID run()V failed");
        if (env->ExceptionCheck()) env->ExceptionClear();
        env->DeleteGlobalRef(g_callback_ref);
        g_callback_ref = nullptr;
        return;
    }

    g_running.store(true);
    if (pthread_create(&g_thread, nullptr, wait_thread, nullptr) != 0) {
        ALOGE("pthread_create failed: %s", strerror(errno));
        env->DeleteGlobalRef(g_callback_ref);
        g_callback_ref = nullptr;
        g_callback_method = nullptr;
        g_running.store(false);
    }
}

JNIEXPORT void JNICALL
Java_org_lineageos_camerahelper_CameraMotorService_nativeStopPropWatch(
        JNIEnv* env, jclass /*clazz*/) {
    if (!g_running.load()) return;
    g_running.store(false);
    pthread_join(g_thread, nullptr);
    if (g_callback_ref != nullptr) {
        env->DeleteGlobalRef(g_callback_ref);
        g_callback_ref = nullptr;
    }
    g_callback_method = nullptr;
}

}  // extern "C"
