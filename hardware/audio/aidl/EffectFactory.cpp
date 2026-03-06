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

#define LOG_TAG "audio_hw_aidl_effect_factory"

#include <dlfcn.h>
#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>

#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>
#include <system/audio_aidl_utils.h>
#include <system/thread_defs.h>

#include "EffectFactory.h"

using aidl::android::media::audio::common::AudioUuid;

namespace aidl::android::hardware::audio::effect {

EffectFactory::EffectFactory(const std::string& configFile)
        : mConfig(EffectConfig(configFile)) {
    LOG(DEBUG) << __func__ << " loading config: " << configFile;
    loadEffectLibs();
}

EffectFactory::~EffectFactory() {
    if (auto count = mEffectMap.size()) {
        LOG(WARNING) << __func__ << " " << count << " effect instances not destroyed (resource leak)";
        for (const auto& it : mEffectMap) {
            if (auto sp = it.first.lock()) {
                destroyEffectImpl_l(sp);
            }
        }
    }
}

// Load all effect libraries from the config file.
void EffectFactory::loadEffectLibs() {
    const auto& configEffectsMap = mConfig.getEffectsMap();
    for (const auto& configEffects : configEffectsMap) {
        if (AudioUuid type; EffectConfig::findUuid(configEffects, &type)) {
            const auto& configLibs = configEffects.second;
            std::optional<AudioUuid> proxyUuid;
            if (configLibs.proxyLibrary.has_value()) {
                proxyUuid = configLibs.proxyLibrary.value().uuid;
            }
            for (const auto& configLib : configLibs.libraries) {
                createIdentityWithConfig(configLib, type, proxyUuid);
            }
        } else {
            LOG(WARNING) << __func__ << ": no type UUID for effect " << configEffects.first;
        }
    }
}

bool EffectFactory::openEffectLibrary(const AudioUuid& implUuid,
                                      const std::string& path) {
    std::function<void(void*)> dlClose = [](void* h) {
        if (h && dlclose(h)) {
            LOG(ERROR) << "dlclose failed: " << dlerror();
        }
    };
    auto libHandle = std::unique_ptr<void, decltype(dlClose)>{
            dlopen(path.c_str(), RTLD_LAZY), dlClose};
    if (!libHandle) {
        LOG(ERROR) << __func__ << ": dlopen(" << path << ") failed: " << dlerror();
        return false;
    }
    LOG(DEBUG) << __func__ << " loaded: " << path
               << " uuid: " << ::android::audio::utils::toString(implUuid);
    auto iface = new effect_dl_interface_s{nullptr, nullptr, nullptr};
    mEffectLibMap.insert({implUuid,
            std::make_tuple(std::move(libHandle),
                            std::unique_ptr<struct effect_dl_interface_s>(iface),
                            path)});
    return true;
}

void EffectFactory::createIdentityWithConfig(
        const EffectConfig::Library& configLib,
        const AudioUuid& typeUuid,
        const std::optional<AudioUuid>& proxyUuid) {
    const auto& libMap = mConfig.getLibraryMap();
    const std::string& libName = configLib.name;
    auto it = libMap.find(libName);
    if (it == libMap.end()) {
        LOG(ERROR) << __func__ << ": library '" << libName << "' not in library map";
        return;
    }
    Descriptor::Identity id;
    id.type = typeUuid;
    id.uuid = configLib.uuid;
    id.proxy = proxyUuid;
    if (openEffectLibrary(id.uuid, it->second)) {
        mIdentitySet.insert(std::move(id));
    }
}

void EffectFactory::getDlSyms_l(DlEntry& entry) {
    auto& dlHandle = std::get<kMapEntryHandleIndex>(entry);
    if (!dlHandle) return;
    auto& dlIface = std::get<kMapEntryInterfaceIndex>(entry);
    if (!dlIface->createEffectFunc) {
        dlIface->createEffectFunc =
                (EffectCreateFunctor)dlsym(dlHandle.get(), "createEffect");
    }
    if (!dlIface->queryEffectFunc) {
        dlIface->queryEffectFunc =
                (EffectQueryFunctor)dlsym(dlHandle.get(), "queryEffect");
    }
    if (!dlIface->destroyEffectFunc) {
        dlIface->destroyEffectFunc =
                (EffectDestroyFunctor)dlsym(dlHandle.get(), "destroyEffect");
    }
    if (!dlIface->createEffectFunc || !dlIface->destroyEffectFunc ||
        !dlIface->queryEffectFunc) {
        const std::string& libPath = std::get<kMapEntryLibNameIndex>(entry);
        LOG(ERROR) << __func__ << ": missing AIDL effect symbols in " << libPath
                   << " create=" << dlIface->createEffectFunc
                   << " query=" << dlIface->queryEffectFunc
                   << " destroy=" << dlIface->destroyEffectFunc
                   << " dlerror=" << dlerror();
    }
}

ndk::ScopedAStatus EffectFactory::getDescriptorWithUuid_l(const AudioUuid& uuid,
                                                           Descriptor* desc) {
    if (!desc) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    auto it = mEffectLibMap.find(uuid);
    if (it == mEffectLibMap.end()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    getDlSyms_l(it->second);
    auto& libIface = std::get<kMapEntryInterfaceIndex>(it->second);
    if (!libIface || !libIface->queryEffectFunc) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    binder_exception_t ex = libIface->queryEffectFunc(&uuid, desc);
    if (ex != EX_NONE) {
        return ndk::ScopedAStatus::fromExceptionCode(ex);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectFactory::queryEffects(
        const std::optional<AudioUuid>& in_type,
        const std::optional<AudioUuid>& in_impl,
        const std::optional<AudioUuid>& in_proxy,
        std::vector<Descriptor>* _aidl_return) {
    std::lock_guard<std::mutex> lg(mMutex);
    std::vector<Descriptor::Identity> idList;
    std::copy_if(mIdentitySet.begin(), mIdentitySet.end(),
                 std::back_inserter(idList),
                 [&](const auto& id) {
                     return (!in_type.has_value()  || in_type.value()  == id.type) &&
                            (!in_impl.has_value()  || in_impl.value()  == id.uuid) &&
                            (!in_proxy.has_value() ||
                             (id.proxy.has_value() && in_proxy.value() == id.proxy.value()));
                 });
    for (const auto& id : idList) {
        if (mEffectLibMap.count(id.uuid)) {
            Descriptor desc;
            auto status = getDescriptorWithUuid_l(id.uuid, &desc);
            if (!status.isOk()) {
                LOG(WARNING) << __func__ << ": getDescriptor failed for "
                             << ::android::audio::utils::toString(id.uuid);
                continue;
            }
            desc.common.id.proxy = id.proxy;
            _aidl_return->emplace_back(std::move(desc));
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectFactory::queryProcessing(
        const std::optional<Processing::Type>& in_type,
        std::vector<Processing>* _aidl_return) {
    std::lock_guard<std::mutex> lg(mMutex);
    const auto& processings = mConfig.getProcessingMap();
    for (const auto& procIter : processings) {
        if (!in_type.has_value() || in_type.value() == procIter.first) {
            Processing proc = {.type = procIter.first};
            for (const auto& libs : procIter.second) {
                for (const auto& lib : libs.libraries) {
                    Descriptor desc;
                    if (libs.proxyLibrary.has_value()) {
                        desc.common.id.proxy = libs.proxyLibrary.value().uuid;
                    }
                    auto status = getDescriptorWithUuid_l(lib.uuid, &desc);
                    if (!status.isOk()) continue;
                    proc.ids.emplace_back(desc);
                }
            }
            _aidl_return->emplace_back(proc);
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectFactory::createEffect(
        const AudioUuid& in_impl_uuid,
        std::shared_ptr<IEffect>* _aidl_return) {
    LOG(DEBUG) << __func__ << " UUID: " << ::android::audio::utils::toString(in_impl_uuid);
    std::lock_guard<std::mutex> lg(mMutex);
    auto it = mEffectLibMap.find(in_impl_uuid);
    if (it == mEffectLibMap.end()) {
        LOG(ERROR) << __func__ << ": UUID not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    getDlSyms_l(it->second);
    auto& libIface = std::get<kMapEntryInterfaceIndex>(it->second);
    if (!libIface || !libIface->createEffectFunc) {
        LOG(ERROR) << __func__ << ": null createEffectFunc";
        return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    std::shared_ptr<IEffect> effectSp;
    binder_exception_t ex = libIface->createEffectFunc(&in_impl_uuid, &effectSp);
    if (ex != EX_NONE) {
        return ndk::ScopedAStatus::fromExceptionCode(ex);
    }
    if (!effectSp) {
        LOG(WARNING) << __func__ << ": library returned null effect without error";
        return ndk::ScopedAStatus::fromExceptionCode(EX_TRANSACTION_FAILED);
    }
    *_aidl_return = effectSp;
    ndk::SpAIBinder effectBinder = effectSp->asBinder();
    AIBinder_setMinSchedulerPolicy(effectBinder.get(), SCHED_NORMAL, ANDROID_PRIORITY_AUDIO);
    AIBinder_setInheritRt(effectBinder.get(), true);
    mEffectMap[std::weak_ptr<IEffect>(effectSp)] =
            std::make_pair(in_impl_uuid, std::move(effectBinder));
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus EffectFactory::destroyEffectImpl_l(
        const std::shared_ptr<IEffect>& in_handle) {
    std::weak_ptr<IEffect> wpHandle(in_handle);
    auto effectIt = mEffectMap.find(wpHandle);
    if (effectIt == mEffectMap.end()) {
        LOG(ERROR) << __func__ << ": instance not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    const auto& uuid = effectIt->second.first;
    auto libIt = mEffectLibMap.find(uuid);
    if (libIt != mEffectLibMap.end()) {
        auto& iface = std::get<kMapEntryInterfaceIndex>(libIt->second);
        if (!iface || !iface->destroyEffectFunc) {
            return ndk::ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
        }
        binder_exception_t ex = iface->destroyEffectFunc(in_handle);
        if (ex != EX_NONE) {
            return ndk::ScopedAStatus::fromExceptionCode(ex);
        }
    } else {
        LOG(ERROR) << __func__ << ": UUID "
                   << ::android::audio::utils::toString(uuid) << " not in libMap";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    mEffectMap.erase(effectIt);
    return ndk::ScopedAStatus::ok();
}

void EffectFactory::cleanupEffectMap_l() {
    for (auto it = mEffectMap.begin(); it != mEffectMap.end();) {
        if (!it->first.lock()) {
            it = mEffectMap.erase(it);
        } else {
            ++it;
        }
    }
}

ndk::ScopedAStatus EffectFactory::destroyEffect(
        const std::shared_ptr<IEffect>& in_handle) {
    std::lock_guard<std::mutex> lg(mMutex);
    auto status = destroyEffectImpl_l(in_handle);
    cleanupEffectMap_l();
    return status;
}

}  // namespace aidl::android::hardware::audio::effect
