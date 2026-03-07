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

#pragma once

#include <aidl/android/hardware/audio/core/BnStreamCommon.h>
#include <functional>
#include <mutex>
#include <utility>

namespace aidl::android::hardware::audio::core {

class StreamCommon : public BnStreamCommon {
  public:
    using Action = std::function<ndk::ScopedAStatus()>;

    StreamCommon() = default;
    StreamCommon(Action closeAction, Action prepareToCloseAction);
    ~StreamCommon() = default;

    void detach();

    ndk::ScopedAStatus close() override;
    ndk::ScopedAStatus prepareToClose() override;
    ndk::ScopedAStatus updateHwAvSyncId(int32_t in_hwAvSyncId) override;
    ndk::ScopedAStatus getVendorParameters(
            const std::vector<std::string>& in_ids,
            std::vector<::aidl::android::hardware::audio::core::VendorParameter>* _aidl_return) override;
    ndk::ScopedAStatus setVendorParameters(
            const std::vector<::aidl::android::hardware::audio::core::VendorParameter>& in_parameters,
            bool in_async) override;
    ndk::ScopedAStatus addEffect(
            const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) override;
    ndk::ScopedAStatus removeEffect(
            const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) override;
    ndk::ScopedAStatus createMmapBuffer(
            ::aidl::android::hardware::audio::core::MmapBufferDescriptor* _aidl_return) override;

  private:
    std::mutex mLock;
    Action mCloseAction;
    Action mPrepareToCloseAction;
};

}  // namespace aidl::android::hardware::audio::core
