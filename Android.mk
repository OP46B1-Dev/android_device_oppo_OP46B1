#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_DEVICE),OP46B1)
include $(call all-subdir-makefiles,$(LOCAL_PATH))

EGL_SYMLINK := $(TARGET_OUT_VENDOR)/lib/libGLESv2_adreno.so
$(EGL_SYMLINK): $(LOCAL_INSTALLED_MODULE)
	@mkdir -p $(dir $@)
	$(hide) ln -sf egl/$(notdir $@) $@

ALL_DEFAULT_INSTALLED_MODULES += $(EGL_SYMLINK)

METADATA_SYMLINK := $(TARGET_ROOT_OUT)/metadata
$(METADATA_SYMLINK): $(LOCAL_INSTALLED_MODULE)
	@echo "Creating $@"
	@mkdir -p $(TARGET_ROOT_OUT)/metadata
	$(hide) ln -sf /data/vendor/metadata_apex $@/apex

ALL_DEFAULT_INSTALLED_MODULES += $(METADATA_SYMLINK)

endif
