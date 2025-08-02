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
$(call inherit-product, vendor/oppo/OP46B1/OP46B1-vendor.mk)

# Setup dalvik vm configs
$(call inherit-product, frameworks/native/build/phone-xhdpi-6144-dalvik-heap.mk)

# API levels
PRODUCT_SHIPPING_API_LEVEL := 28

# Atrace HAL
PRODUCT_PACKAGES += \
    android.hardware.atrace@1.0-service

# Audio
PRODUCT_PACKAGES += \
    audio.primary.default \
    audio.primary.sdm710 \
    audio.r_submix.default \
    audio.usb.default \
    android.hardware.audio@2.0-service \
    android.hardware.audio@2.0-impl \
    android.hardware.audio@4.0-impl \
    android.hardware.audio@5.0-impl \
    android.hardware.audio@6.0-impl \
    android.hardware.audio.effect@2.0-impl \
    android.hardware.audio.effect@4.0-impl \
    android.hardware.audio.effect@5.0-impl \
    android.hardware.audio.effect@6.0-impl \
    android.hardware.soundtrigger@2.1-impl \
    liba2dpoffload \
    libbatterylistener \
    libcirrusspkrprot \
    libcomprcapture \
    libdynproc \
    libexthwplugin \
    libhdmiedid \
    libhfp \
    libqcompostprocbundle \
    libqcomvisualizer \
    libqcomvoiceprocessing \
    libsndmonitor \
    libspkrprot \
    libssrec \
    libvolumelistener \
    libalsautils \
    libbluetooth_audio_session

# ANT+
PRODUCT_PACKAGES += \
    AntHalService-Soong \
    com.dsi.ant@1.0.vendor

# Bluetooth
PRODUCT_PACKAGES += \
    android.hardware.bluetooth.audio@2.0-impl \
    com.qualcomm.qti.bluetooth_audio@1.0.vendor \
    vendor.qti.hardware.bluetooth_audio@2.0.vendor \
    vendor.qti.hardware.bluetooth_audio@2.1.vendor \
    vendor.qti.hardware.btconfigstore@1.0.vendor \
    vendor.qti.hardware.btconfigstore@2.0.vendor \
    vendor.qti.hardware.capabilityconfigstore@1.0.vendor \
    audio.bluetooth.default

# Camera
PRODUCT_PACKAGES += \
    android.hardware.camera.provider@2.4-service \
    android.hardware.camera.provider@2.4-impl \
    camera.qcom \
    camera.device@1.0-impl \
    camera.device@3.2-impl \
    camera.device@3.3-impl \
    camera.device@3.4-impl \
    camera.device@3.4-external-impl.so \
    camera.device@3.5-impl \
    camera.device@3.5-external-impl.so \
    camera.device@3.6-impl \
    camera.device@3.6-external-impl.so \
    libcamera2ndk_vendor \
    vendor.qti.hardware.camera.device@1.0 \
    OnePlusCameraHelper

# Context Hub
PRODUCT_PACKAGES += \
    android.hardware.contexthub@1.0-impl.generic \
    android.hardware.contexthub@1.0-service

# CAS
PRODUCT_PACKAGES += \
    android.hardware.cas@1.2-service

# Display
PRODUCT_PACKAGES += \
    android.hardware.memtrack@1.0-service \
    android.hardware.memtrack@1.0-impl \
    android.hardware.graphics.mapper@2.0-impl-qti-display \
    android.hardware.graphics.composer@2.3-impl \
    android.hardware.graphics.composer@2.3-service \
    android.hardware.light@2.0-impl \
    android.hardware.light@2.0-service \
    android.hardware.broadcastradio@1.0-impl \
    android.hardware.configstore@1.1-service \
    android.hardware.renderscript@1.0-impl \
    vendor.qti.hardware.display.allocator@1.0-service \
    vendor.qti.hardware.display.allocator@1.0.vendor \
    vendor.qti.hardware.display.allocator@3.0.vendor \
    vendor.qti.hardware.display.mapper@1.0.vendor \
    vendor.qti.hardware.display.mapper@1.1.vendor \
    vendor.qti.hardware.display.mapper@2.0.vendor \
    vendor.qti.hardware.display.mapper@3.0.vendor \
    vendor.qti.hardware.display.mapper@4.0.vendor \
    vendor.qti.hardware.display.mapperextensions@1.0.vendor \
    vendor.qti.hardware.display.mapperextensions@1.1.vendor \
    gralloc.default \
    gralloc.sdm710 \
    hwcomposer.sdm710 \
    light.sdm710 \
    libgralloc.qti \
    libqdutils \
    libqdMetaData \
    libqdMetaData.system \
    libdisplayconfig.qti \
    libdisplayconfig.system.qti \
    libtinyxml \
    libdisplaydebug \
    libdrmutils \
    libgpu_tonemapper \
    libqservice \
    libsdmcore \
    libsdmutils \
    vendor.display.config@1.0.so \
    vendor.display.config@1.1.so \
    vendor.display.config@1.2.so \
    vendor.display.config@1.3.so \
    vendor.display.config@1.4.so \
    vendor.display.config@1.5.so \
    vendor.display.config@1.6.so \
    vendor.display.config@1.7.so \
    vendor.display.config@1.8.so \
    vendor.display.config@1.9.so \
    vendor.display.config@1.10.so \
    vendor.display.config@1.11.so \
    vendor.display.config@2.0.so \
    vendor.qti.hardware.display.composer@1.0.vendor \
    vendor.qti.hardware.display.composer@2.0.vendor \
    vendor.qti.hardware.display.composer@3.0 \
    libhwc2onfbadapter

# DRM
PRODUCT_PACKAGES += \
    android.hardware.drm@1.3-service.clearkey \
    libdrmclearkeyplugin

# For android_filesystem_config.h
PRODUCT_PACKAGES += \
    fs_config_files

# GNSS
PRODUCT_PACKAGES += \
    libsensorndkbridge

# Health
PRODUCT_PACKAGES += \
    android.hardware.health@2.1-impl \
    android.hardware.health@2.1-impl.recovery \
    android.hardware.health@2.1-service

# Hidl
PRODUCT_PACKAGES += \
    android.hidl.base@1.0

# Overlay
PRODUCT_PACKAGES += \
    FrameworkResOverlayOP46B1 \
    SystemUIOverlayOP46B1

# Perf
PRODUCT_PACKAGES += \
    vendor.qti.hardware.perf@2.0.vendor \
    vendor.qti.hardware.perf@2.1.vendor \
    vendor.qti.hardware.perf@2.2.vendor

# Product characteristics
PRODUCT_CHARACTERISTICS := nosdcard

# IPA
PRODUCT_PACKAGES += \
    ipacm \
    IPACM_cfg.xml \
    libipanat \
    liboffloadhal

# Local time
PRODUCT_PACKAGES += \
    local_time.default

# Media
PRODUCT_PACKAGES += \
    android.hardware.media.omx@1.0-service \
    libmm-omxcore \
    libOmxAacEnc \
    libOmxAmrEnc \
    libOmxCore \
    libOmxEvrcEnc \
    libOmxG711Enc \
    libOmxQcelp13Enc \
    libOmxVdec \
    libOmxVenc \
    libc2dcolorconvert \
    libplatformconfig \
    libstagefrighthw \
    libstagefright_softomx_plugin.vendor

# Power
PRODUCT_PACKAGES += \
    power.default \
    power.qcom \
    android.hardware.power@1.0-impl \
    android.hardware.power@1.0-service

# Product characteristics
PRODUCT_CHARACTERISTICS := nosdcard

# Qualcomm
PRODUCT_PACKAGES += \
    libqti_vndfwk_detect

# QMI
PRODUCT_PACKAGES += \
    libjson \
    libmmosal \
    libvndfwk_detect_jni.qti \
    libvndfwk_detect_jni.qti.vendor

# RIL
PRODUCT_PACKAGES += \
    librmnetctl \
    libnfnetlink \
    libnetfilter_conntrack \
    libavservices_minijail.vendor

# Rootdir
PRODUCT_PACKAGES += \
    fstab.qcom \
    fstab.qcom_ramdisk \
    init.recovery.qcom.rc \
    init.class_main.sh \
    init.crda.sh \
    init.mdm.sh \
    init.qcom.class_core.sh \
    init.qcom.coex.sh \
    init.qcom.early_boot.sh \
    init.qcom.efs.sync.sh \
    init.qcom.post_boot.sh \
    init.qcom.sdio.sh \
    init.qcom.sensors.sh \
    init.qcom.sh \
    init.qcom.usb.sh \
    init.qti.chg_policy.sh \
    init.qti.qcv.sh \
    qca6234-service.sh \
    init.oppo.sensor.rc \
    init.oppo.vendor.motor.rc \
    init.qcom.factory.rc \
    init.qcom.rc \
    init.qcom.usb.rc \
    init.qti.ufs.rc \
    init.target.rc \
    init.wlan.qcom.rc \
    init.wlan.target.rc

# Seccomp policy
PRODUCT_COPY_FILES += \
    $(LOCAL_PATH)/seccomp/atfwd@2.0.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/atfwd@2.0.policy \
    $(LOCAL_PATH)/seccomp/imsrtp.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/imsrtp.policy \
    $(LOCAL_PATH)/seccomp/mediacodec.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/mediacodec.policy \
    $(LOCAL_PATH)/seccomp/qti-systemd.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/qti-systemd.policy \
    $(LOCAL_PATH)/seccomp/vendor.qti.hardware.dsp.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/vendor.qti.hardware.dsp.policy \
    $(LOCAL_PATH)/seccomp/wfdhdcphalservice.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/wfdhdcphalservice.policy \
    $(LOCAL_PATH)/seccomp/wfdvndservice.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/wfdvndservice.policy \
    $(LOCAL_PATH)/seccomp/wifidisplayhalservice.policy:$(TARGET_COPY_OUT_VENDOR)/etc/seccomp_policy/wifidisplayhalservice.policy

# Servicetracker
PRODUCT_PACKAGES += \
    vendor.qti.hardware.servicetracker@1.0.vendor \
    vendor.qti.hardware.servicetracker@1.1.vendor \
    vendor.qti.hardware.servicetracker@1.2.vendor

# Sensors
PRODUCT_PACKAGES += \
    android.hardware.sensors@2.0-service.multihal

# Thermal
PRODUCT_PACKAGES += \
    android.hardware.thermal@2.0-service.qti

# USB
PRODUCT_PACKAGES += \
    android.hardware.usb@1.0-service

# Vibrator
PRODUCT_PACKAGES += \
    vendor.qti.hardware.vibrator.service \
    vibrator.default

# Wi-Fi
PRODUCT_PACKAGES += \
    vendor.qti.hardware.fstman@1.0.vendor \
    vendor.qti.hardware.wifi.hostapd@1.0.vendor \
    vendor.qti.hardware.wifi.hostapd@1.1.vendor \
    vendor.qti.hardware.wifi.hostapd@1.2.vendor \
    vendor.qti.hardware.wifi.supplicant@2.0.vendor \
    vendor.qti.hardware.wifi.supplicant@2.1.vendor \
    vendor.qti.hardware.wifi.supplicant@2.2.vendor \
    android.hardware.wifi@1.0-service \
    libwfdaac_vendor \
    libcld80211 \
    libkeystore-engine-wifi-hidl \
    libkeystore-wifi-hidl \
    libwifi-hal

# Soong namespaces
PRODUCT_SOONG_NAMESPACES += \
    $(LOCAL_PATH)
