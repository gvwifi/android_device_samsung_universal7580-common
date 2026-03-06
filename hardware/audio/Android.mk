# Copyright (C) 2017 The LineageOS Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

LOCAL_PATH := $(call my-dir)

# Legacy HAL re-enabled for AIDL wrapper usage


include $(CLEAR_VARS)

LOCAL_MODULE := audio.primary.universal7580
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_VENDOR_MODULE := true

LOCAL_SRC_FILES := \
    audience.c \
    audio_hw.c \
    compress_offload.c \
    ril_interface.c \
    voice.c

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/include \
    external/tinyalsa/include \
    external/tinycompress/include \
    device/samsung/universal7580-common/hardware/ril/libsecril-client \
    system/media/audio_route/include

LOCAL_CFLAGS := \
    -Werror \
    -Wall \
    -DPREPROCESSING_ENABLED

LOCAL_SHARED_LIBRARIES := \
    liblog \
    libcutils \
    libaudioutils \
    libhardware \
    libtinyalsa \
    libtinycompress \
    libaudioroute \
    libdl \
    libprocessgroup \
    libsecril-client

LOCAL_HEADER_LIBRARIES := \
    libhardware_headers \
    libaudioeffects \
    ril_headers_7580

include $(BUILD_SHARED_LIBRARY)


