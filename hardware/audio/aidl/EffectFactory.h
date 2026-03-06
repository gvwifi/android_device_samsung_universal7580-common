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

#include <any>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include <aidl/android/hardware/audio/effect/BnFactory.h>
#include <android-base/thread_annotations.h>
#include <effectFactory-impl/EffectConfig.h>
#include <effect-impl/EffectTypes.h>

namespace aidl::android::hardware::audio::effect {

class EffectFactory : public BnFactory {
  public:
    explicit EffectFactory(const std::string& configFile);
    ~EffectFactory();

    ndk::ScopedAStatus queryEffects(
            const std::optional<::aidl::android::media::audio::common::AudioUuid>& in_type,
            const std::optional<::aidl::android::media::audio::common::AudioUuid>& in_impl,
            const std::optional<::aidl::android::media::audio::common::AudioUuid>& in_proxy,
            std::vector<::aidl::android::hardware::audio::effect::Descriptor>* _aidl_return) override;
    ndk::ScopedAStatus createEffect(
            const ::aidl::android::media::audio::common::AudioUuid& in_impl_uuid,
            std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>* _aidl_return) override;
    ndk::ScopedAStatus destroyEffect(
            const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_handle) override;
    ndk::ScopedAStatus queryProcessing(
            const std::optional<::aidl::android::hardware::audio::effect::Processing::Type>& in_type,
            std::vector<::aidl::android::hardware::audio::effect::Processing>* _aidl_return) override;

  private:
    const EffectConfig mConfig;

    std::mutex mMutex;
    // Set of effect identities loaded from config.
    std::set<Descriptor::Identity> mIdentitySet GUARDED_BY(mMutex);

    // Indices into the DlEntry tuple.
    static constexpr int kMapEntryHandleIndex    = 0;
    static constexpr int kMapEntryInterfaceIndex = 1;
    static constexpr int kMapEntryLibNameIndex   = 2;
    typedef std::tuple<
            std::unique_ptr<void, std::function<void(void*)>>,  // dlHandle
            std::unique_ptr<struct effect_dl_interface_s>,       // interfaces
            std::string                                          // library name
    > DlEntry;

    // Map from implementation UUID → (dlHandle, interface, libPath).
    std::map<::aidl::android::media::audio::common::AudioUuid, DlEntry> mEffectLibMap
            GUARDED_BY(mMutex);

    // Map of active effect instances → (UUID, binder).
    typedef std::pair<::aidl::android::media::audio::common::AudioUuid, ndk::SpAIBinder> EffectEntry;
    std::map<std::weak_ptr<IEffect>, EffectEntry, std::owner_less<>> mEffectMap
            GUARDED_BY(mMutex);

    // --- private helpers ---
    void loadEffectLibs();
    bool openEffectLibrary(const ::aidl::android::media::audio::common::AudioUuid& implUuid,
                           const std::string& path) NO_THREAD_SAFETY_ANALYSIS;
    void createIdentityWithConfig(
            const EffectConfig::Library& configLib,
            const ::aidl::android::media::audio::common::AudioUuid& typeUuid,
            const std::optional<::aidl::android::media::audio::common::AudioUuid>& proxyUuid)
            NO_THREAD_SAFETY_ANALYSIS;

    void getDlSyms_l(DlEntry& entry) REQUIRES(mMutex);
    ndk::ScopedAStatus getDescriptorWithUuid_l(
            const ::aidl::android::media::audio::common::AudioUuid& uuid,
            Descriptor* desc) REQUIRES(mMutex);
    ndk::ScopedAStatus destroyEffectImpl_l(
            const std::shared_ptr<IEffect>& in_handle) REQUIRES(mMutex);
    void cleanupEffectMap_l() REQUIRES(mMutex);
};

}  // namespace aidl::android::hardware::audio::effect
