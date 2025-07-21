#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit from those products. Most specific first.
$(call inherit-product, $(SRC_TARGET_DIR)/product/core_64_bit.mk)
$(call inherit-product, $(SRC_TARGET_DIR)/product/full_base_telephony.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Inherit from OP46B1 device
$(call inherit-product, device/oppo/OP46B1/device.mk)

PRODUCT_DEVICE := OP46B1
PRODUCT_NAME := lineage_OP46B1
PRODUCT_BRAND := OPPO
PRODUCT_MODEL := PCAM00
PRODUCT_MANUFACTURER := OPPO

PRODUCT_GMS_CLIENTID_BASE := android-oppo

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="PCAM00-user 11 RKQ1.201217.002 1736421576479 release-keys"

BUILD_FINGERPRINT := OPPO/PCAM00/OP46B1:11/RKQ1.201217.002/1736421576479:user/release-keys
