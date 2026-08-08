#!/usr/bin/env -S PYTHONPATH=../../../tools/extract-utils python3
#
# SPDX-FileCopyrightText: 2024 The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from extract_utils.file import File
from extract_utils.fixups_blob import (
    BlobFixupCtx,
    blob_fixup,
    blob_fixups_user_type,
)
from extract_utils.fixups_lib import (
    lib_fixup_remove,
    lib_fixups,
    lib_fixups_user_type,
)
from extract_utils.main import (
    ExtractUtils,
    ExtractUtilsModule,
)

namespace_imports = [
    'device/oppo/OP46B1',
    'hardware/qcom-caf/sdm845',
    'hardware/qcom-caf/wlan',
    'vendor/qcom/opensource/commonsys/display',
    'vendor/qcom/opensource/commonsys-intf/display',
    'vendor/qcom/opensource/dataservices',
    'vendor/qcom/opensource/display',
]


def lib_fixup_vendor_suffix(lib: str, partition: str, *args, **kwargs):
    return f'{lib}_{partition}' if partition == 'vendor' else None


lib_fixups: lib_fixups_user_type = {
    **lib_fixups,
    (
        'com.qualcomm.qti.dpm.api@1.0',
        'com.qualcomm.qti.imscmservice@1.0',
        'com.qualcomm.qti.imscmservice@2.0',
        'com.qualcomm.qti.imscmservice@2.1',
        'com.qualcomm.qti.imscmservice@2.2',
        'com.qualcomm.qti.uceservice@2.0',
        'com.qualcomm.qti.uceservice@2.1',
        'com.qualcomm.qti.uceservice@2.2',
        'libmmosal',
        'vendor.qti.hardware.alarm@1.0',
        'vendor.qti.hardware.fm@1.0',
        'vendor.qti.hardware.radio.am@1.0',
        'vendor.qti.hardware.radio.ims@1.0',
        'vendor.qti.hardware.radio.ims@1.1',
        'vendor.qti.hardware.radio.ims@1.2',
        'vendor.qti.hardware.radio.ims@1.3',
        'vendor.qti.hardware.radio.ims@1.4',
        'vendor.qti.hardware.wifidisplaysession@1.0',
        'vendor.qti.ims.callinfo@1.0',
        'vendor.qti.ims.rcsconfig@1.0',
        'vendor.qti.imsrtpservice@3.0',
    ): lib_fixup_vendor_suffix,
}

blob_fixups: blob_fixups_user_type = {
    (
        'odm/lib64/mediadrm/libwvdrmengine.so',
        'odm/lib64/libwvhidl.so'
    ): blob_fixup()
        .add_needed('libcrypto_shim.so'),
    'odm/lib64/oplus.sensors.ssc.so': blob_fixup()
        .binary_regex_replace(
            b'android.sensor.tp_proximity',
            b'android.sensor.proximity\x00\x00\x00'
        )
        .sig_replace(
            '00 40 26 FA 01 00 00 A0 40', # mov w1, #0x1fa2640
            '00 08 00 00 00 00 00 A0 40'  # mov w1, #8
        ),
    (
        'odm/bin/hw/vendor.oplus.hardware.biometrics.fingerprint@2.1-service',
        'odm/lib64/libgf_hal_G2.so',
        'odm/lib64/libgf_hal_G3.so',
        'odm/lib64/libgf_hal_G5.so',
        'odm/lib64/libgf_hal_G6.so'
    ): blob_fixup()
        .binary_regex_replace(
            b'odm/vendor/firmware',
            b'odm/firmware\x00\x00\x00\x00\x00\x00\x00'
        ),
    'system_ext/lib/libwfdservice.so': blob_fixup()
        .replace_needed(
            'android.media.audio.common.types-V2-cpp.so',
            'android.media.audio.common.types-V4-cpp.so'
        ),
    'system_ext/lib64/lib-imsvideocodec.so': blob_fixup()
        .add_needed('libgui_shim.so'),
    'vendor/lib64/hw/camera.qcom.so': blob_fixup()
        .add_needed('liboplus_camera_shim.so'),
    'vendor/lib64/libhvx_proxy_stub.so': blob_fixup()
        .clear_symbol_version('remote_handle64_close')
        .clear_symbol_version('remote_handle64_invoke')
        .clear_symbol_version('remote_handle64_open')
        .clear_symbol_version('remote_register_dma_handle')
        .clear_symbol_version('remote_register_dma_handle_attr'),
    'vendor/lib64/libvidhance.so': blob_fixup()
        .add_needed('libcomparetf2_shim.so'),
    'vendor/lib64/sensors.ssc.so': blob_fixup()
        .binary_regex_replace(
            b'qti.sensor.wise_light',
            b'android.sensor.light\x00'
        )
        .sig_replace(
            'F9 69 CA 84 52 49 3F A0 72', # mov w9, #0x1fa2653
            'F9 A9 00 80 52 09 00 A0 72'  # mov w9, #5
        ),
}  # fmt: skip

module = ExtractUtilsModule(
    'OP46B1',
    'oppo',
    blob_fixups=blob_fixups,
    lib_fixups=lib_fixups,
    namespace_imports=namespace_imports,
)

if __name__ == '__main__':
    utils = ExtractUtils.device(module)
    utils.run()
