/*
 * Copyright (C) 2025 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "android.hardware.power-service.universal7580"

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "Power.h"

using aidl::android::hardware::power::impl::universal7580::Power;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    auto power = ndk::SharedRefBase::make<Power>();
    const std::string instance = std::string(Power::descriptor) + "/default";

    binder_status_t status =
        AServiceManager_addService(power->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        ALOGE("Cannot register AIDL Power HAL service: %d", status);
        return 1;
    }

    ALOGI("Universal7580 AIDL Power HAL service started");
    ABinderProcess_joinThreadPool();

    return 1;  // ABinderProcess_joinThreadPool should not return
}
