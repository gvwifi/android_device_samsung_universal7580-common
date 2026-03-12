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

#include "Power.h"

#include <log/log.h>

namespace aidl::android::hardware::power::impl::universal7580 {

ndk::ScopedAStatus Power::setMode(Mode type, bool enabled) {
    ALOGV("%s: type=%d enabled=%d", __func__, static_cast<int>(type), enabled);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isModeSupported(Mode /*type*/, bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::setBoost(Boost type, int32_t durationMs) {
    ALOGV("%s: type=%d durationMs=%d", __func__, static_cast<int>(type), durationMs);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::isBoostSupported(Boost /*type*/, bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::createHintSession(int32_t /*tgid*/, int32_t /*uid*/,
                                             const std::vector<int32_t>& /*threadIds*/,
                                             int64_t /*durationNanos*/,
                                             std::shared_ptr<IPowerHintSession>* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::createHintSessionWithConfig(
        int32_t /*tgid*/, int32_t /*uid*/, const std::vector<int32_t>& /*threadIds*/,
        int64_t /*durationNanos*/, SessionTag /*tag*/, SessionConfig* /*config*/,
        std::shared_ptr<IPowerHintSession>* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getHintSessionPreferredRate(int64_t* outNanoseconds) {
    *outNanoseconds = -1;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getSessionChannel(int32_t /*tgid*/, int32_t /*uid*/,
                                             ChannelConfig* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::closeSessionChannel(int32_t /*tgid*/, int32_t /*uid*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getSupportInfo(SupportInfo* _aidl_return) {
    // This device does not support hint sessions, headroom, or composition data.
    // Returning a fully-populated SupportInfo with all features disabled is
    // required so that HintManagerService can initialize safely even on legacy
    // hardware that has no modern Power HAL support.
    _aidl_return->usesSessions = false;
    _aidl_return->boosts = 0;
    _aidl_return->modes = 0;
    _aidl_return->sessionHints = 0;
    _aidl_return->sessionModes = 0;
    _aidl_return->sessionTags = 0;

    _aidl_return->compositionData.isSupported = false;
    _aidl_return->compositionData.disableGpuFences = false;
    _aidl_return->compositionData.maxBatchSize = 0;
    _aidl_return->compositionData.alwaysBatch = false;

    _aidl_return->headroom.isCpuSupported = false;
    _aidl_return->headroom.isGpuSupported = false;
    _aidl_return->headroom.cpuMinIntervalMillis = 0;
    _aidl_return->headroom.gpuMinIntervalMillis = 0;

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Power::getCpuHeadroom(const CpuHeadroomParams& /*params*/,
                                          CpuHeadroomResult* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::getGpuHeadroom(const GpuHeadroomParams& /*params*/,
                                          GpuHeadroomResult* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::sendCompositionData(
        const std::vector<CompositionData>& /*in_data*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Power::sendCompositionUpdate(
        const CompositionUpdate& /*in_update*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::power::impl::universal7580
