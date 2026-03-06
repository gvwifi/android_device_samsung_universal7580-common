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

#define LOG_TAG "audio_effect_hw_aidl_service"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "EffectFactory.h"

using aidl::android::hardware::audio::effect::EffectFactory;

/** Path to the audio effects configuration on the vendor partition. */
static const char* kVendorEffectsConfig = "/vendor/etc/audio_effects.xml";

int main() {
    // Need at least 4 threads for processing effect requests concurrently.
    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();

    auto effectFactory = ndk::SharedRefBase::make<EffectFactory>(kVendorEffectsConfig);
    const std::string instance = std::string(EffectFactory::descriptor) + "/default";
    binder_status_t status = AServiceManager_addService(effectFactory->asBinder().get(), instance.c_str());
    if (status != STATUS_OK) {
        LOG(ERROR) << "Failed to register audio effect service: " << status;
        return EXIT_FAILURE;
    }

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // should not reach
}
