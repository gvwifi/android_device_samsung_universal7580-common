/*
 * Copyright (C) 2024 The LineageOS Project
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

#pragma once

#include <aidl/android/hardware/audio/core/BnConfig.h>

namespace aidl::android::hardware::audio::core {

/**
 * Minimal stub implementation of IConfig for the Universal7580 audio HAL.
 * Returns empty/default configurations which is sufficient to allow audioserver
 * to initialize properly on Android 16.
 */
class Config : public BnConfig {
  public:
    ndk::ScopedAStatus getSurroundSoundConfig(SurroundSoundConfig* _aidl_return) override;
    ndk::ScopedAStatus getEngineConfig(
            ::aidl::android::media::audio::common::AudioHalEngineConfig* _aidl_return) override;
};

}  // namespace aidl::android::hardware::audio::core
