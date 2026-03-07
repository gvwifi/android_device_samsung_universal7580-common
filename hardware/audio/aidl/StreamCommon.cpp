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

#define LOG_TAG "audio_hw_aidl_stream_common"

#include <android-base/logging.h>
#include "StreamCommon.h"

namespace aidl::android::hardware::audio::core {

StreamCommon::StreamCommon(Action closeAction, Action prepareToCloseAction)
    : mCloseAction(std::move(closeAction)),
      mPrepareToCloseAction(std::move(prepareToCloseAction)) {}

void StreamCommon::detach() {
    std::lock_guard lg(mLock);
    mCloseAction = nullptr;
    mPrepareToCloseAction = nullptr;
}

ndk::ScopedAStatus StreamCommon::close() {
    Action closeAction;
    {
        std::lock_guard lg(mLock);
        closeAction = mCloseAction;
    }
    if (!closeAction) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return closeAction();
}

ndk::ScopedAStatus StreamCommon::prepareToClose() {
    Action prepareToCloseAction;
    {
        std::lock_guard lg(mLock);
        prepareToCloseAction = mPrepareToCloseAction;
    }
    if (!prepareToCloseAction) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    return prepareToCloseAction();
}

ndk::ScopedAStatus StreamCommon::updateHwAvSyncId(int32_t in_hwAvSyncId) {
    (void)in_hwAvSyncId;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamCommon::getVendorParameters(
        const std::vector<std::string>& in_ids,
        std::vector<::aidl::android::hardware::audio::core::VendorParameter>* _aidl_return) {
    _aidl_return->clear();
    _aidl_return->reserve(in_ids.size());
    for (const auto& id : in_ids) {
        VendorParameter parameter;
        parameter.id = id;
        _aidl_return->push_back(std::move(parameter));
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamCommon::setVendorParameters(
        const std::vector<::aidl::android::hardware::audio::core::VendorParameter>& in_parameters,
        bool in_async) {
    (void)in_parameters;
    (void)in_async;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamCommon::addEffect(
        const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    (void)in_effect;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamCommon::removeEffect(
        const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    (void)in_effect;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamCommon::createMmapBuffer(
        ::aidl::android::hardware::audio::core::MmapBufferDescriptor* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::audio::core
