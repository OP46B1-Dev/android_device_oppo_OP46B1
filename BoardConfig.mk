#
# Copyright (C) 2025 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

# Inherit the proprietary files
# include vendor/oppo/OP46B1/BoardConfigVendor.mk

DEVICE_PATH := device/oppo/OP46B1

# APEX
DEXPREOPT_GENERATE_APEX_IMAGE := true

# Architecture
TARGET_ARCH := arm64
TARGET_ARCH_VARIANT := armv8-a
TARGET_CPU_ABI := arm64-v8a
TARGET_CPU_ABI2 := 
TARGET_CPU_VARIANT := generic
TARGET_CPU_VARIANT_RUNTIME := kryo385

TARGET_2ND_ARCH := arm
TARGET_2ND_ARCH_VARIANT := armv7-a-neon
TARGET_2ND_CPU_ABI := armeabi-v7a
TARGET_2ND_CPU_ABI2 := armeabi
TARGET_2ND_CPU_VARIANT := generic
TARGET_2ND_CPU_VARIANT_RUNTIME := cortex-a9

# Bootloader
TARGET_BOOTLOADER_BOARD_NAME := sdm710
TARGET_NO_BOOTLOADER := true

# Display
TARGET_SCREEN_DENSITY := 480
TARGET_SCREEN_HEIGHT := 2340
TARGET_SCREEN_WIDTH := 1080

# Kernel
BOARD_BOOTIMG_HEADER_VERSION := 1
BOARD_KERNEL_BASE          := 0x00000000
BOARD_KERNEL_PAGESIZE      := 4096
BOARD_KERNEL_OFFSET        := 0x00008000
BOARD_KERNEL_SECOND_OFFSET := 0x00f00000
BOARD_KERNEL_TAGS_OFFSET   := 0x00000100
BOARD_RAMDISK_OFFSET       := 0x01000000
BOARD_KERNEL_CMDLINE := console=ttyMSM0,115200n8 \
                        earlycon=msm_geni_serial,0xA90000 \
                        androidboot.hardware=qcom \
                        androidboot.console=ttyMSM0 \
                        video=vfb:640x400,bpp=32,memsize=3072000 \
                        msm_rtb.filter=0x237 \
                        ehci-hcd.park=3 \
                        lpm_levels.sleep_disabled=1 \
                        service_locator.enable=1 \
                        androidboot.configfs=true \
                        androidboot.usbcontroller=a600000.dwc3 \
                        swiotlb=1 \
                        loop.max_part=7 \
                        printk.devkmsg=on \
                        kpti=off
# TODO: Set SELinux to Permissive mode
BOARD_KERNEL_CMDLINE += androidboot.selinux=permissive
BOARD_MKBOOTIMG_ARGS += --header_version $(BOARD_BOOTIMG_HEADER_VERSION) \
                        --kernel_offset $(BOARD_KERNEL_OFFSET) \
                        --second_offset $(BOARD_KERNEL_SECOND_OFFSET) \
                        --tags_offset $(BOARD_KERNEL_TAGS_OFFSET) \
                        --ramdisk_offset $(BOARD_RAMDISK_OFFSET)
BOARD_KERNEL_IMAGE_NAME := Image.gz-dtb
BOARD_KERNEL_SEPARATED_DTBO := true
TARGET_KERNEL_CONFIG := sdm670-perf_defconfig
TARGET_KERNEL_SOURCE := kernel/oppo/sdm710

# Kernel - prebuilt
TARGET_FORCE_PREBUILT_KERNEL := true
ifeq ($(TARGET_FORCE_PREBUILT_KERNEL),true)
TARGET_PREBUILT_KERNEL := $(DEVICE_PATH)/prebuilts/Image.gz-dtb
BOARD_PREBUILT_DTBOIMAGE := $(DEVICE_PATH)/prebuilts/dtbo.img
endif

# Partitions
BOARD_BUILD_SYSTEM_ROOT_IMAGE := false
BOARD_FLASH_BLOCK_SIZE := 262144 # (BOARD_KERNEL_PAGESIZE * 64)
BOARD_BOOTIMAGE_PARTITION_SIZE := 67108864
BOARD_DTBOIMG_PARTITION_SIZE := 25165824
BOARD_ODMIMAGE_PARTITION_SIZE := 268435456
BOARD_RECOVERYIMAGE_PARTITION_SIZE := 67108864
BOARD_SYSTEMIMAGE_PARTITION_SIZE := 5213519872
BOARD_USERDATAIMAGE_PARTITION_SIZE := 243914452992
BOARD_VENDORIMAGE_PARTITION_SIZE := 1996488704

BOARD_ODMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_SYSTEMIMAGE_FILE_SYSTEM_TYPE := ext4
BOARD_VENDORIMAGE_FILE_SYSTEM_TYPE := ext4

# Platform
TARGET_BOARD_PLATFORM := sdm710

# Properties
TARGET_SYSTEM_PROP += $(DEVICE_PATH)/configs/prop/system.prop
TARGET_PRODUCT_PROP += $(DEVICE_PATH)/configs/prop/product.prop
TARGET_SYSTEM_EXT_PROP += $(DEVICE_PATH)/configs/prop/system_ext.prop

# Qualcomm Hardware
BOARD_USES_QCOM_HARDWARE := true
TARGET_USES_GRALLOC1 := true
TARGET_USES_HWC2 := true
TARGET_USES_ION := true

# Recovery
TARGET_RECOVERY_FSTAB := $(DEVICE_PATH)/rootdir/etc/fstab.qcom
BOARD_INCLUDE_RECOVERY_DTBO := true
BOARD_PREBUILT_RECOVERY_DTBOIMAGE := $(DEVICE_PATH)/prebuilts/dtb.img
TARGET_RECOVERY_PIXEL_FORMAT := RGBX_8888
TARGET_USERIMAGES_USE_EXT4 := true
TARGET_USERIMAGES_USE_F2FS := true

# Security patch level
VENDOR_SECURITY_PATCH := 2022-04-05

# Treble
BOARD_VNDK_VERSION := current
TARGET_COPY_OUT_VENDOR := vendor
TARGET_COPY_OUT_ODM := odm

# Verified Boot
BOARD_AVB_ENABLE := true
BOARD_AVB_MAKE_VBMETA_IMAGE_ARGS += --flags 3
BOARD_AVB_RECOVERY_KEY_PATH := external/avb/test/data/testkey_rsa4096.pem
BOARD_AVB_RECOVERY_ALGORITHM := SHA256_RSA4096
BOARD_AVB_RECOVERY_ROLLBACK_INDEX := 1
BOARD_AVB_RECOVERY_ROLLBACK_INDEX_LOCATION := 1

# VINTF
DEVICE_MANIFEST_FILE := $(DEVICE_PATH)/configs/vintf/manifest.xml
DEVICE_MATRIX_FILE := $(DEVICE_PATH)/configs/vintf/compatibility_matrix.xml
