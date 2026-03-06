/*
 * Copyright (C) 2023 The Android Open Source Project
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

#define LOG_TAG "audio_hw_aidl_service"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "Config.h"
#include "Module.h"

using aidl::android::hardware::audio::core::Config;
using aidl::android::hardware::audio::core::Module;

int main() {
    // This is a vendor service.
    ABinderProcess_setThreadPoolMaxThreadCount(16);
    ABinderProcess_startThreadPool();

    // Create the Module instance
    auto module = ndk::SharedRefBase::make<Module>();

    const std::string instance = std::string() + Module::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(module->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    LOG(INFO) << "Universal7580 Audio AIDL Service started as " << instance;

    // Create and register IConfig/default — required by audioserver on Android 16.
    // Without this registration, audioserver loops waiting for IConfig and triggers
    // watchdog reboot.
    auto config = ndk::SharedRefBase::make<Config>();
    const std::string configInstance = std::string() + Config::descriptor + "/default";
    binder_status_t configStatus = AServiceManager_addService(config->asBinder().get(), configInstance.c_str());
    CHECK_EQ(configStatus, STATUS_OK);

    LOG(INFO) << "Universal7580 Audio AIDL Config Service started as " << configInstance;

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
