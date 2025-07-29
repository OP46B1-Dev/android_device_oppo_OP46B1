#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Enable updating of APEXes
$(call inherit-product, $(SRC_TARGET_DIR)/product/updatable_apex.mk)

# Include GSI keys
$(call inherit-product, $(SRC_TARGET_DIR)/product/gsi_keys.mk)

# Inherit the proprietary files
# $(call inherit-product, vendor/oppo/OP46B1/OP46B1-vendor.mk)

# Setup dalvik vm configs
$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)

# API levels
PRODUCT_SHIPPING_API_LEVEL := 28

# Camera
PRODUCT_PACKAGES += \
    OnePlusCameraHelper

# Health
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-impl.recovery \
    android.hardware.health@2.1-service

# Overlay
PRODUCT_PACKAGES += \
    FrameworkResOverlayOP46B1 \
    SystemUIOverlayOP46B1

# Product characteristics
PRODUCT_CHARACTERISTICS := nosdcard

# Rootdir
PRODUCT_PACKAGES += \
    fstab.qcom \
    fstab.qcom_ramdisk \
    init.recovery.qcom.rc

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)
