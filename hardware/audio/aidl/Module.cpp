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

#define LOG_TAG "audio_hw_aidl_module"

#include <algorithm>
#include <errno.h>
#include <android-base/logging.h>
#include <stdio.h>
#include <hardware/audio.h>
#include <hardware/hardware.h>

#include <aidl/android/media/audio/common/AudioChannelLayout.h>
#include <aidl/android/media/audio/common/AudioDeviceDescription.h>
#include <aidl/android/media/audio/common/AudioDeviceType.h>
#include <aidl/android/media/audio/common/AudioFormatDescription.h>
#include <aidl/android/media/audio/common/AudioFormatType.h>
#include <aidl/android/media/audio/common/AudioIoFlags.h>
#include <aidl/android/media/audio/common/AudioInputFlags.h>
#include <aidl/android/media/audio/common/AudioOutputFlags.h>
#include <aidl/android/media/audio/common/AudioPortDeviceExt.h>
#include <aidl/android/media/audio/common/AudioPortExt.h>
#include <aidl/android/media/audio/common/AudioPortMixExt.h>
#include <aidl/android/media/audio/common/AudioProfile.h>
#include <aidl/android/media/audio/common/PcmType.h>

#include "Module.h"
#include "StreamOut.h"
#include "StreamIn.h"

using aidl::android::media::audio::common::AudioChannelLayout;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioFormatDescription;
using aidl::android::media::audio::common::AudioFormatType;
using aidl::android::media::audio::common::AudioIoFlags;
using aidl::android::media::audio::common::AudioInputFlags;
using aidl::android::media::audio::common::AudioOutputFlags;
using aidl::android::media::audio::common::AudioPortDeviceExt;
using aidl::android::media::audio::common::AudioPortExt;
using aidl::android::media::audio::common::AudioPortMixExt;
using aidl::android::media::audio::common::AudioProfile;
using aidl::android::media::audio::common::PcmType;
using aidl::android::media::audio::common::AudioMode;
using aidl::android::media::audio::common::AudioPort;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::hardware::audio::core::IModule;
using aidl::android::hardware::audio::core::AudioPatch;
using aidl::android::hardware::audio::core::AudioRoute;

namespace aidl::android::hardware::audio::core {

// AIDL AudioOutputFlags ordinals differ from legacy bitmask after FAST
// (AIDL skips LOW_LATENCY which is 0x8 in legacy). Manual mapping required.
static audio_output_flags_t aidlOutputFlagsToLegacy(int32_t aidlFlags) {
    audio_output_flags_t result = AUDIO_OUTPUT_FLAG_NONE;
    // Ordinal 0 = DIRECT
    if (aidlFlags & (1 << 0))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_DIRECT);
    // Ordinal 1 = PRIMARY
    if (aidlFlags & (1 << 1))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_PRIMARY);
    // Ordinal 2 = FAST
    if (aidlFlags & (1 << 2))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_FAST);
    // Ordinal 3 = DEEP_BUFFER   (legacy 0x10, not 0x8!)
    if (aidlFlags & (1 << 3))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
    // Ordinal 4 = COMPRESS_OFFLOAD
    if (aidlFlags & (1 << 4))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD);
    // Ordinal 5 = NON_BLOCKING
    if (aidlFlags & (1 << 5))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_NON_BLOCKING);
    // Ordinal 6 = HW_AV_SYNC
    if (aidlFlags & (1 << 6))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_HW_AV_SYNC);
    // Ordinal 7 = TTS / INCALL_MUSIC
    if (aidlFlags & (1 << 7))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_TTS);
    // Ordinal 8 = RAW
    if (aidlFlags & (1 << 8))  result = (audio_output_flags_t)(result | AUDIO_OUTPUT_FLAG_RAW);
    return result;
}

// AIDL AudioInputFlags ordinals match legacy bitmask positions — direct cast is safe
static audio_input_flags_t aidlInputFlagsToLegacy(int32_t aidlFlags) {
    return static_cast<audio_input_flags_t>(aidlFlags);
}

static audio_devices_t aidlDeviceToLegacy(AudioDeviceType type) {
    switch (type) {
        case AudioDeviceType::OUT_SPEAKER: return AUDIO_DEVICE_OUT_SPEAKER;
        case AudioDeviceType::OUT_SPEAKER_EARPIECE: return AUDIO_DEVICE_OUT_EARPIECE;
        case AudioDeviceType::OUT_HEADSET: return AUDIO_DEVICE_OUT_WIRED_HEADSET;
        case AudioDeviceType::OUT_HEADPHONE: return AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
        case AudioDeviceType::OUT_DEVICE: return AUDIO_DEVICE_OUT_DEFAULT;
        case AudioDeviceType::OUT_BROADCAST: return AUDIO_DEVICE_OUT_SPEAKER;
        case AudioDeviceType::IN_MICROPHONE: return AUDIO_DEVICE_IN_BUILTIN_MIC;
        case AudioDeviceType::IN_MICROPHONE_BACK: return AUDIO_DEVICE_IN_BACK_MIC;
        case AudioDeviceType::IN_HEADSET: return AUDIO_DEVICE_IN_WIRED_HEADSET;
        default: return AUDIO_DEVICE_NONE;
    }
}

static const AudioPortConfig* findPortConfigById(const std::vector<AudioPortConfig>& configs,
                                                 int32_t id) {
    auto it = std::find_if(configs.begin(), configs.end(),
                           [id](const auto& cfg) { return cfg.id == id; });
    return it == configs.end() ? nullptr : &(*it);
}

static const AudioPort* findPortById(const std::vector<AudioPort>& ports, int32_t id) {
    auto it = std::find_if(ports.begin(), ports.end(),
                           [id](const auto& port) { return port.id == id; });
    return it == ports.end() ? nullptr : &(*it);
}

static audio_devices_t portConfigToLegacyDevice(const std::vector<AudioPortConfig>& configs,
                                                const std::vector<AudioPort>& ports,
                                                int32_t portConfigId) {
    const auto* cfg = findPortConfigById(configs, portConfigId);
    if (!cfg) return AUDIO_DEVICE_NONE;
    const auto* port = findPortById(ports, cfg->portId);
    if (!port) return AUDIO_DEVICE_NONE;
    if (port->ext.getTag() != AudioPortExt::Tag::device) return AUDIO_DEVICE_NONE;
    const auto& dev = port->ext.get<AudioPortExt::Tag::device>().device.type;
    return aidlDeviceToLegacy(dev.type);
}

static bool containsConfigId(const std::vector<int32_t>& ids, int32_t targetId) {
    return std::find(ids.begin(), ids.end(), targetId) != ids.end();
}

static audio_io_handle_t resolveLegacyHandleForPortConfig(const std::vector<AudioPortConfig>& configs,
                                                          int32_t portConfigId) {
    const auto* cfg = findPortConfigById(configs, portConfigId);
    if (!cfg || cfg->ext.getTag() != AudioPortExt::Tag::mix) {
        return static_cast<audio_io_handle_t>(portConfigId);
    }
    const int32_t mixHandle = cfg->ext.get<AudioPortExt::Tag::mix>().handle;
    return static_cast<audio_io_handle_t>(mixHandle != 0 ? mixHandle : portConfigId);
}

static audio_devices_t resolveOutputDevicesForPortConfig(
        const std::vector<AudioPatch>& patches,
        const std::vector<AudioPortConfig>& configs,
        const std::vector<AudioPort>& ports,
        int32_t mixPortConfigId) {
    for (const auto& patch : patches) {
        if (!containsConfigId(patch.sourcePortConfigIds, mixPortConfigId)) continue;
        audio_devices_t devices = AUDIO_DEVICE_NONE;
        for (int32_t sinkId : patch.sinkPortConfigIds) {
            devices = static_cast<audio_devices_t>(
                    devices | portConfigToLegacyDevice(configs, ports, sinkId));
        }
        if (devices != AUDIO_DEVICE_NONE) return devices;
    }
    return AUDIO_DEVICE_NONE;
}

static audio_devices_t resolveInputDevicesForPortConfig(
        const std::vector<AudioPatch>& patches,
        const std::vector<AudioPortConfig>& configs,
        const std::vector<AudioPort>& ports,
        int32_t mixPortConfigId) {
    for (const auto& patch : patches) {
        if (!containsConfigId(patch.sinkPortConfigIds, mixPortConfigId)) continue;
        audio_devices_t devices = AUDIO_DEVICE_NONE;
        for (int32_t sourceId : patch.sourcePortConfigIds) {
            devices = static_cast<audio_devices_t>(
                    devices | portConfigToLegacyDevice(configs, ports, sourceId));
        }
        if (devices != AUDIO_DEVICE_NONE) return devices;
    }
    return AUDIO_DEVICE_NONE;
}


Module::Module() {
    int ret = hw_get_module_by_class(AUDIO_HARDWARE_MODULE_ID, "primary", &mLegacyModule);
    if (ret != 0) {
        LOG(FATAL) << "Failed to load legacy audio module: " << ret;
        return;
    }

    ret = audio_hw_device_open(mLegacyModule, &mLegacyDevice);
    if (ret != 0) {
        LOG(FATAL) << "Failed to open legacy audio device: " << ret;
        return;
    }
    LOG(INFO) << "Loaded legacy audio module: " << mLegacyModule->name;
    initTopology();
}

Module::~Module() {
    if (mLegacyDevice) {
        audio_hw_device_close(mLegacyDevice);
    }
}

void Module::initTopology() {
    if (mTopologyInitialized) return;
    mTopologyInitialized = true;

    // Primary output flags: PRIMARY | DEEP_BUFFER
    const int32_t primaryOutputFlags =
            (1 << static_cast<int>(AudioOutputFlags::PRIMARY)) |
            (1 << static_cast<int>(AudioOutputFlags::DEEP_BUFFER));

    // Standard PCM 16-bit audio profile for output (stereo)
    AudioProfile outputPcm16;
    outputPcm16.format.type = AudioFormatType::PCM;
    outputPcm16.format.pcm = PcmType::INT_16_BIT;
    outputPcm16.channelMasks = {
        AudioChannelLayout::make<AudioChannelLayout::layoutMask>(AudioChannelLayout::LAYOUT_STEREO)
    };
    outputPcm16.sampleRates = {8000, 11025, 16000, 32000, 44100, 48000};

    // Standard PCM 16-bit audio profile for input (mono + stereo)
    AudioProfile inputPcm16;
    inputPcm16.format.type = AudioFormatType::PCM;
    inputPcm16.format.pcm = PcmType::INT_16_BIT;
    inputPcm16.channelMasks = {
        AudioChannelLayout::make<AudioChannelLayout::layoutMask>(AudioChannelLayout::LAYOUT_MONO),
        AudioChannelLayout::make<AudioChannelLayout::layoutMask>(AudioChannelLayout::LAYOUT_STEREO)
    };
    inputPcm16.sampleRates = {8000, 11025, 16000, 32000, 44100, 48000};

    // -----------------------------------------------------------------------
    // Device ports
    // -----------------------------------------------------------------------

    // Helper lambda to build a device port
    int32_t nextId = 1;
    auto makeDevPort = [&](const std::string& name, AudioDeviceType type,
                           bool isInput, bool isDefault,
                           const std::string& connection) -> AudioPort {
        AudioPort port;
        port.id = nextId++;
        port.name = name;
        port.flags = isInput
                ? AudioIoFlags::make<AudioIoFlags::Tag::input>(0)
                : AudioIoFlags::make<AudioIoFlags::Tag::output>(0);
        AudioPortDeviceExt devExt;
        devExt.device.type.type = type;
        devExt.device.type.connection = connection;
        if (isDefault) {
            devExt.flags = 1 << AudioPortDeviceExt::FLAG_INDEX_DEFAULT_DEVICE;
        }
        port.ext = AudioPortExt::make<AudioPortExt::Tag::device>(devExt);
        return port;
    };

    // Attached output devices
    AudioPort speakerPort = makeDevPort("Speaker",        AudioDeviceType::OUT_SPEAKER,
                                        false, true,  "");   // default output
    AudioPort earpiecePort = makeDevPort("Earpiece",      AudioDeviceType::OUT_SPEAKER_EARPIECE,
                                         false, false, "");

    // Attached input devices
    AudioPort builtinMicPort  = makeDevPort("Built-In Mic",       AudioDeviceType::IN_MICROPHONE,
                                             true, true,  "");  // default input
    AudioPort backMicPort     = makeDevPort("Built-In Back Mic",  AudioDeviceType::IN_MICROPHONE_BACK,
                                             true, false, "");

    // External (template) device ports – they have a non-empty connection type
    AudioPort wiredHsOutPort  = makeDevPort("Wired Headset",      AudioDeviceType::OUT_HEADSET,
                                             false, false, AudioDeviceDescription::CONNECTION_ANALOG);
    AudioPort wiredHpOutPort  = makeDevPort("Wired Headphone",    AudioDeviceType::OUT_HEADPHONE,
                                             false, false, AudioDeviceDescription::CONNECTION_ANALOG);
    AudioPort wiredHsMicPort  = makeDevPort("Wired Headset Mic",  AudioDeviceType::IN_HEADSET,
                                             true,  false, AudioDeviceDescription::CONNECTION_ANALOG);

    mTopologyPorts.push_back(speakerPort);
    mTopologyPorts.push_back(earpiecePort);
    mTopologyPorts.push_back(builtinMicPort);
    mTopologyPorts.push_back(backMicPort);
    mTopologyPorts.push_back(wiredHsOutPort);
    mTopologyPorts.push_back(wiredHpOutPort);
    mTopologyPorts.push_back(wiredHsMicPort);

    // -----------------------------------------------------------------------
    // Mix ports
    // -----------------------------------------------------------------------

    // Primary output mix port
    AudioPort primaryOutMix;
    primaryOutMix.id = nextId++;
    primaryOutMix.name = "primary output";
    primaryOutMix.flags = AudioIoFlags::make<AudioIoFlags::Tag::output>(primaryOutputFlags);
    primaryOutMix.profiles.push_back(outputPcm16);
    {
        AudioPortMixExt mixExt;
        mixExt.maxOpenStreamCount = 8;
        mixExt.maxActiveStreamCount = 8;
        primaryOutMix.ext = AudioPortExt::make<AudioPortExt::Tag::mix>(mixExt);
    }
    mTopologyPorts.push_back(primaryOutMix);

    // Primary input mix port
    AudioPort primaryInMix;
    primaryInMix.id = nextId++;
    primaryInMix.name = "primary input";
    primaryInMix.flags = AudioIoFlags::make<AudioIoFlags::Tag::input>(0);
    primaryInMix.profiles.push_back(inputPcm16);
    {
        AudioPortMixExt mixExt;
        mixExt.maxOpenStreamCount = 4;
        mixExt.maxActiveStreamCount = 4;
        primaryInMix.ext = AudioPortExt::make<AudioPortExt::Tag::mix>(mixExt);
    }
    mTopologyPorts.push_back(primaryInMix);

    // -----------------------------------------------------------------------
    // Routes
    // -----------------------------------------------------------------------

    // Output: primary out mix → each sink device
    for (int32_t sinkId : {speakerPort.id, earpiecePort.id,
                            wiredHsOutPort.id, wiredHpOutPort.id}) {
        AudioRoute r;
        r.sourcePortIds = {primaryOutMix.id};
        r.sinkPortId    = sinkId;
        r.isExclusive   = false;
        mTopologyRoutes.push_back(r);
    }

    // Input: each source device → primary input mix
    {
        AudioRoute r;
        r.sourcePortIds = {builtinMicPort.id, backMicPort.id, wiredHsMicPort.id};
        r.sinkPortId    = primaryInMix.id;
        r.isExclusive   = false;
        mTopologyRoutes.push_back(r);
    }

    // -----------------------------------------------------------------------
    // Initial port configs for attached device ports (dynamic = empty profile)
    // -----------------------------------------------------------------------
    auto makeDynDevConfig = [](const AudioPort& port) {
        AudioPortConfig cfg;
        cfg.id     = port.id;   // initial config ID == port ID
        cfg.portId = port.id;
        cfg.flags  = port.flags;
        cfg.ext    = port.ext;
        // format / channelMask / sampleRate are std::optional – leave empty (dynamic)
        return cfg;
    };

    for (const AudioPort* p : {&speakerPort, &earpiecePort,
                                &builtinMicPort, &backMicPort}) {
        AudioPortConfig cfg = makeDynDevConfig(*p);
        mTopologyPortConfigs.push_back(cfg);
        mInitialPortConfigs.push_back(cfg);
    }

    // nextPortConfigId starts well above the port id range
    mNextPortConfigId = nextId + 50;

    LOG(INFO) << "initTopology: registered " << mTopologyPorts.size()
              << " ports, " << mTopologyRoutes.size() << " routes";
}

ndk::ScopedAStatus Module::setModuleDebug(const ModuleDebug& in_debug) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getTelephony(std::shared_ptr<ITelephony>* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    *_aidl_return = nullptr; // Legacy HAL handles Telephony internally via RIL client
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetooth(std::shared_ptr<IBluetooth>* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    *_aidl_return = nullptr;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetoothA2dp(std::shared_ptr<IBluetoothA2dp>* _aidl_return) {
    *_aidl_return = nullptr;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getBluetoothLe(std::shared_ptr<IBluetoothLe>* _aidl_return) {
    *_aidl_return = nullptr;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::connectExternalDevice(const AudioPort& in_templateIdAndAdditionalData, AudioPort* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    // Legacy HAL might not need explicit connection for basic output/input if handled via set_parameters
    *_aidl_return = in_templateIdAndAdditionalData;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::disconnectExternalDevice(int32_t in_portId) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::prepareToDisconnectExternalDevice(int32_t in_portId) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPatches(std::vector<AudioPatch>* _aidl_return) {
    *_aidl_return = mAudioPatches;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPort(int32_t in_portId, AudioPort* _aidl_return) {
    for (const auto& port : mTopologyPorts) {
        if (port.id == in_portId) {
            *_aidl_return = port;
            return ndk::ScopedAStatus::ok();
        }
    }
    LOG(ERROR) << __func__ << ": port id " << in_portId << " not found";
    return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
}

ndk::ScopedAStatus Module::getAudioPortConfigs(std::vector<AudioPortConfig>* _aidl_return) {
    LOG(INFO) << __func__ << ": called, returning " << mTopologyPortConfigs.size() << " configs";
    *_aidl_return = mTopologyPortConfigs;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioPorts(std::vector<AudioPort>* _aidl_return) {
    LOG(INFO) << __func__ << ": called, returning " << mTopologyPorts.size() << " ports";
    *_aidl_return = mTopologyPorts;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioRoutes(std::vector<AudioRoute>* _aidl_return) {
    LOG(INFO) << __func__ << ": called, returning " << mTopologyRoutes.size() << " routes";
    *_aidl_return = mTopologyRoutes;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAudioRoutesForAudioPort(int32_t in_portId, std::vector<AudioRoute>* _aidl_return) {
    _aidl_return->clear();
    // Verify port exists
    bool found = false;
    for (const auto& p : mTopologyPorts) {
        if (p.id == in_portId) { found = true; break; }
    }
    if (!found) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    for (const auto& route : mTopologyRoutes) {
        if (route.sinkPortId == in_portId) {
            _aidl_return->push_back(route);
        } else {
            for (int32_t srcId : route.sourcePortIds) {
                if (srcId == in_portId) {
                    _aidl_return->push_back(route);
                    break;
                }
            }
        }
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::openInputStream(
        const IModule::OpenInputStreamArguments& in_args,
        IModule::OpenInputStreamReturn* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    struct audio_stream_in* legacyStream = nullptr;

    audio_io_handle_t handle = resolveLegacyHandleForPortConfig(mTopologyPortConfigs, in_args.portConfigId);
    audio_devices_t devices = resolveInputDevicesForPortConfig(
            mAudioPatches, mTopologyPortConfigs, mTopologyPorts, in_args.portConfigId);
    if (devices == AUDIO_DEVICE_NONE) {
        devices = AUDIO_DEVICE_IN_BUILTIN_MIC;
    }

    struct audio_config config = AUDIO_CONFIG_INITIALIZER;
    config.sample_rate = 48000;
    config.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    config.format = AUDIO_FORMAT_PCM_16_BIT;

    if (const auto* cfg = findPortConfigById(mTopologyPortConfigs, in_args.portConfigId)) {
        if (cfg->sampleRate.has_value()) {
            config.sample_rate = cfg->sampleRate.value().value;
        }
        if (cfg->channelMask.has_value()) {
            const auto& mask = cfg->channelMask.value();
            if (mask.getTag() == AudioChannelLayout::Tag::layoutMask &&
                mask.get<AudioChannelLayout::Tag::layoutMask>() == AudioChannelLayout::LAYOUT_MONO) {
                config.channel_mask = AUDIO_CHANNEL_IN_MONO;
            }
        }
    }

    // Extract audio source from sink metadata
    audio_source_t audioSource = AUDIO_SOURCE_DEFAULT;
    if (!in_args.sinkMetadata.tracks.empty()) {
        audioSource = static_cast<audio_source_t>(
                static_cast<int32_t>(in_args.sinkMetadata.tracks[0].source));
    }

    audio_input_flags_t inFlags = AUDIO_INPUT_FLAG_NONE;
    if (const auto* cfg = findPortConfigById(mTopologyPortConfigs, in_args.portConfigId);
        cfg != nullptr && cfg->flags.has_value() &&
        cfg->flags.value().getTag() == AudioIoFlags::Tag::input) {
        inFlags = aidlInputFlagsToLegacy(
                cfg->flags.value().get<AudioIoFlags::Tag::input>());
    }

    LOG(INFO) << __func__ << ": handle=" << handle << " devices=0x"
              << std::hex << static_cast<uint32_t>(devices)
              << " flags=0x" << static_cast<uint32_t>(inFlags)
              << " source=" << static_cast<uint32_t>(audioSource) << std::dec;

    int ret = mLegacyDevice->open_input_stream(mLegacyDevice, handle, devices, &config, &legacyStream,
                                               inFlags, nullptr, audioSource);
    if (ret != 0 || !legacyStream) {
        LOG(ERROR) << "Failed to open legacy input stream: " << ret;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    auto streamIn = ndk::SharedRefBase::make<StreamIn>(mLegacyDevice, legacyStream);
    _aidl_return->stream = streamIn;

    ndk::ScopedAStatus status = streamIn->init(&_aidl_return->desc);
    if (!status.isOk()) {
        LOG(ERROR) << "Failed to init StreamIn FMQ";
        streamIn->close();
        return status;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::openOutputStream(
        const IModule::OpenOutputStreamArguments& in_args,
        IModule::OpenOutputStreamReturn* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    struct audio_stream_out* legacyStream = nullptr;

    audio_io_handle_t handle = resolveLegacyHandleForPortConfig(mTopologyPortConfigs, in_args.portConfigId);
    audio_devices_t devices = resolveOutputDevicesForPortConfig(
            mAudioPatches, mTopologyPortConfigs, mTopologyPorts, in_args.portConfigId);
    if (devices == AUDIO_DEVICE_NONE) {
        devices = AUDIO_DEVICE_OUT_SPEAKER;
    }

    struct audio_config config = AUDIO_CONFIG_INITIALIZER;
    config.sample_rate = 48000;
    config.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    config.format = AUDIO_FORMAT_PCM_16_BIT;
    config.offload_info = AUDIO_INFO_INITIALIZER;

    if (const auto* cfg = findPortConfigById(mTopologyPortConfigs, in_args.portConfigId)) {
        if (cfg->sampleRate.has_value()) {
            config.sample_rate = cfg->sampleRate.value().value;
        }
        if (cfg->channelMask.has_value()) {
            const auto& mask = cfg->channelMask.value();
            if (mask.getTag() == AudioChannelLayout::Tag::layoutMask &&
                mask.get<AudioChannelLayout::Tag::layoutMask>() == AudioChannelLayout::LAYOUT_MONO) {
                config.channel_mask = AUDIO_CHANNEL_OUT_MONO;
            }
        }
    }

    audio_output_flags_t flags = (audio_output_flags_t)(AUDIO_OUTPUT_FLAG_PRIMARY | AUDIO_OUTPUT_FLAG_DEEP_BUFFER);
    if (const auto* cfg = findPortConfigById(mTopologyPortConfigs, in_args.portConfigId);
        cfg != nullptr && cfg->flags.has_value() &&
        cfg->flags.value().getTag() == AudioIoFlags::Tag::output) {
        flags = aidlOutputFlagsToLegacy(
                cfg->flags.value().get<AudioIoFlags::Tag::output>());
    }

    if ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) != 0 && in_args.offloadInfo.has_value()) {
        const auto& offloadInfo = in_args.offloadInfo.value();
        config.offload_info.sample_rate = config.sample_rate;
        config.offload_info.channel_mask = config.channel_mask;
        config.offload_info.format = config.format;
        config.offload_info.stream_type = static_cast<audio_stream_type_t>(offloadInfo.streamType);
        config.offload_info.bit_rate = static_cast<uint32_t>(offloadInfo.bitRatePerSecond);
        config.offload_info.duration_us = offloadInfo.durationUs;
        config.offload_info.has_video = offloadInfo.hasVideo;
        config.offload_info.is_streaming = offloadInfo.isStreaming;
        config.offload_info.bit_width = static_cast<uint32_t>(offloadInfo.bitWidth);
        config.offload_info.offload_buffer_size = static_cast<uint32_t>(offloadInfo.offloadBufferSize);
        config.offload_info.usage = static_cast<audio_usage_t>(offloadInfo.usage);
        config.offload_info.encapsulation_mode = static_cast<audio_encapsulation_mode_t>(
                offloadInfo.encapsulationMode);
        config.offload_info.content_id = offloadInfo.contentId;
        config.offload_info.sync_id = offloadInfo.syncId;
    }

    LOG(INFO) << __func__ << ": handle=" << handle << " devices=0x"
              << std::hex << static_cast<uint32_t>(devices)
              << " flags=0x" << static_cast<uint32_t>(flags) << std::dec;

    int ret = mLegacyDevice->open_output_stream(mLegacyDevice, handle, devices, flags, &config, &legacyStream, nullptr);
    if (ret != 0 || !legacyStream) {
        LOG(ERROR) << "Failed to open legacy output stream: " << ret;
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }
    
    auto streamOut = ndk::SharedRefBase::make<StreamOut>(mLegacyDevice, legacyStream);
    _aidl_return->stream = streamOut;

    ndk::ScopedAStatus status = streamOut->init(&_aidl_return->desc);
    if (!status.isOk()) {
        LOG(ERROR) << "Failed to init StreamOut FMQ";
        streamOut->close();
        return status;
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getSupportedPlaybackRateFactors(SupportedPlaybackRateFactors* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::setAudioPatch(const AudioPatch& in_requested, AudioPatch* _aidl_return) {
    LOG(INFO) << __func__ << ": called with id=" << in_requested.id;

    audio_devices_t legacyDevice = AUDIO_DEVICE_NONE;
    for (const auto& sinkId : in_requested.sinkPortConfigIds) {
        legacyDevice = static_cast<audio_devices_t>(
                legacyDevice | portConfigToLegacyDevice(mTopologyPortConfigs, mTopologyPorts, sinkId));
    }
    if (legacyDevice == AUDIO_DEVICE_NONE) {
        for (const auto& sourceId : in_requested.sourcePortConfigIds) {
            legacyDevice = static_cast<audio_devices_t>(
                    legacyDevice | portConfigToLegacyDevice(
                            mTopologyPortConfigs, mTopologyPorts, sourceId));
        }
    }

    if (legacyDevice != AUDIO_DEVICE_NONE && mLegacyDevice && mLegacyDevice->set_parameters) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "routing=%u", static_cast<uint32_t>(legacyDevice));
        mLegacyDevice->set_parameters(mLegacyDevice, cmd);
    }

    AudioPatch appliedPatch = in_requested;

    // Calculate minimumStreamBufferSizeFrames from mix port configs
    int32_t maxSampleRate = 0;
    auto findMixSampleRate = [&](int32_t configId) {
        const auto* cfg = findPortConfigById(mTopologyPortConfigs, configId);
        if (cfg && cfg->ext.getTag() == AudioPortExt::Tag::mix && cfg->sampleRate.has_value()) {
            maxSampleRate = std::max(maxSampleRate, cfg->sampleRate.value().value);
        }
    };
    for (int32_t id : appliedPatch.sourcePortConfigIds) findMixSampleRate(id);
    for (int32_t id : appliedPatch.sinkPortConfigIds) findMixSampleRate(id);
    if (maxSampleRate <= 0) maxSampleRate = 48000;
    // Use 20ms latency worth of frames as minimum buffer size
    constexpr int32_t kBufferLatencyMs = 20;
    appliedPatch.minimumStreamBufferSizeFrames = maxSampleRate * kBufferLatencyMs / 1000;

    // Set latencies for each sink port config
    constexpr int32_t kDefaultLatencyMs = 20;
    appliedPatch.latenciesMs.assign(
            std::max(appliedPatch.sinkPortConfigIds.size(), static_cast<size_t>(1)),
            kDefaultLatencyMs);

    // Assign unique patch ID for new patches, or update existing
    if (in_requested.id == 0) {
        // New patch
        appliedPatch.id = mNextPatchId++;
        mAudioPatches.push_back(appliedPatch);
    } else {
        // Update existing patch
        auto patchIt = std::find_if(mAudioPatches.begin(), mAudioPatches.end(),
                                    [&](const auto& patch) { return patch.id == in_requested.id; });
        if (patchIt != mAudioPatches.end()) {
            appliedPatch.id = patchIt->id;
            *patchIt = appliedPatch;
        } else {
            // Not found, treat as new
            appliedPatch.id = mNextPatchId++;
            mAudioPatches.push_back(appliedPatch);
        }
    }

    *_aidl_return = appliedPatch;

    LOG(INFO) << __func__ << ": applied patch id=" << appliedPatch.id
              << " minBufferFrames=" << appliedPatch.minimumStreamBufferSizeFrames;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setAudioPortConfig(
        const AudioPortConfig& in_requested,
        AudioPortConfig* out_suggested,
        bool* applied) {
    LOG(INFO) << __func__ << ": called for portId=" << in_requested.portId
              << " id=" << in_requested.id;

    // Find the port this config belongs to
    AudioPort* matchedPort = nullptr;
    for (auto& port : mTopologyPorts) {
        if (port.id == in_requested.portId) {
            matchedPort = &port;
            break;
        }
    }
    if (!matchedPort) {
        LOG(ERROR) << __func__ << ": portId " << in_requested.portId << " not found";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    // Look for an existing config with this id (update case)
    if (in_requested.id != 0) {
        for (auto& cfg : mTopologyPortConfigs) {
            if (cfg.id == in_requested.id) {
                // Merge: keep fields from in_requested that are set
                if (in_requested.format.has_value())      cfg.format      = in_requested.format;
                if (in_requested.channelMask.has_value()) cfg.channelMask = in_requested.channelMask;
                if (in_requested.sampleRate.has_value())  cfg.sampleRate  = in_requested.sampleRate;
                if (in_requested.flags.has_value())       cfg.flags       = in_requested.flags;
                // ext: only update the mix handle/usecase for mix ports, keep device ext
                if (in_requested.ext.getTag() == AudioPortExt::Tag::mix &&
                    cfg.ext.getTag() == AudioPortExt::Tag::mix) {
                    auto& dst = cfg.ext.get<AudioPortExt::Tag::mix>();
                    const auto& src = in_requested.ext.get<AudioPortExt::Tag::mix>();
                    dst.handle  = src.handle;
                    dst.usecase = src.usecase;
                }
                *out_suggested = cfg;
                *applied = true;
                return ndk::ScopedAStatus::ok();
            }
        }
    }

    // New config: create one from the port's defaults, apply requested fields
    AudioPortConfig newCfg;
    newCfg.id     = mNextPortConfigId++;
    newCfg.portId = matchedPort->id;
    newCfg.flags  = matchedPort->flags;
    newCfg.ext    = matchedPort->ext;

    // If port has profiles, use the first non-dynamic one as default
    if (!matchedPort->profiles.empty()) {
        const auto& prof = matchedPort->profiles[0];
        if (prof.format.type != AudioFormatType{}) {
            newCfg.format = prof.format;
        }
        if (!prof.channelMasks.empty()) {
            newCfg.channelMask = prof.channelMasks[0];
        }
        if (!prof.sampleRates.empty()) {
            newCfg.sampleRate = ::aidl::android::media::audio::common::Int{
                .value = prof.sampleRates[0]};
        }
    }

    // Override with requested fields if present
    if (in_requested.format.has_value())      newCfg.format      = in_requested.format;
    if (in_requested.channelMask.has_value()) newCfg.channelMask = in_requested.channelMask;
    if (in_requested.sampleRate.has_value())  newCfg.sampleRate  = in_requested.sampleRate;
    if (in_requested.flags.has_value())       newCfg.flags       = in_requested.flags;
    if (in_requested.ext.getTag() == AudioPortExt::Tag::mix &&
        newCfg.ext.getTag() == AudioPortExt::Tag::mix) {
        auto& dst = newCfg.ext.get<AudioPortExt::Tag::mix>();
        const auto& src = in_requested.ext.get<AudioPortExt::Tag::mix>();
        dst.handle  = src.handle;
        dst.usecase = src.usecase;
    }

    mTopologyPortConfigs.push_back(newCfg);
    *out_suggested = newCfg;
    *applied = true;
    LOG(INFO) << __func__ << ": created new port config id=" << newCfg.id
              << " for portId=" << newCfg.portId;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::resetAudioPatch(int32_t in_patchId) {
    auto patchIt = std::find_if(mAudioPatches.begin(), mAudioPatches.end(),
                                [&](const auto& patch) { return patch.id == in_patchId; });
    if (patchIt == mAudioPatches.end()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    mAudioPatches.erase(patchIt);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::resetAudioPortConfig(int32_t in_portConfigId) {
    auto cfgIt = std::find_if(mTopologyPortConfigs.begin(), mTopologyPortConfigs.end(),
                              [&](const auto& cfg) { return cfg.id == in_portConfigId; });
    if (cfgIt == mTopologyPortConfigs.end()) {
        // Already cleaned up or never existed - return OK to prevent cascading errors
        // in Hal2AidlMapper which calls resetUnusedPortConfigs after releaseAudioPatch
        return ndk::ScopedAStatus::ok();
    }

    auto initialIt = std::find_if(mInitialPortConfigs.begin(), mInitialPortConfigs.end(),
                                  [&](const auto& cfg) { return cfg.id == in_portConfigId; });
    if (initialIt != mInitialPortConfigs.end()) {
        *cfgIt = *initialIt;
        return ndk::ScopedAStatus::ok();
    }

    mTopologyPortConfigs.erase(cfgIt);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMasterMute(bool* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    *_aidl_return = mMasterMute;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMasterMute(bool in_mute) {
    LOG(INFO) << __func__ << ": called with mute " << in_mute;
    mMasterMute = in_mute;
    if (mLegacyDevice && mLegacyDevice->set_parameters) {
        char cmd[32];
        snprintf(cmd, sizeof(cmd), "master_mute=%d", in_mute ? 1 : 0);
        mLegacyDevice->set_parameters(mLegacyDevice, cmd);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMasterVolume(float* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    *_aidl_return = mMasterVolume;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMasterVolume(float in_volume) {
    LOG(INFO) << __func__ << ": called with volume " << in_volume;
    mMasterVolume = in_volume;
    if (mLegacyDevice->set_master_volume) {
        int ret = mLegacyDevice->set_master_volume(mLegacyDevice, in_volume);
        if (ret != 0 && ret != -ENOSYS) {
            LOG(WARNING) << "set_master_volume returned " << ret << ", trying set_parameters fallback";
        }
    }
    // Always try set_parameters fallback for volume (some HALs only handle this)
    if (mLegacyDevice && mLegacyDevice->set_parameters) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "master_volume=%f", in_volume);
        mLegacyDevice->set_parameters(mLegacyDevice, cmd);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMicMute(bool* _aidl_return) {
    LOG(INFO) << __func__ << ": called";
    if (mLegacyDevice->get_mic_mute) {
        int ret = mLegacyDevice->get_mic_mute(mLegacyDevice, _aidl_return);
        if (ret == -ENOSYS) return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        if (ret != 0) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setMicMute(bool in_mute) {
    LOG(INFO) << __func__ << ": called with mute " << in_mute;
    if (mLegacyDevice->set_mic_mute) {
        int ret = mLegacyDevice->set_mic_mute(mLegacyDevice, in_mute);
        if (ret == -ENOSYS) return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
        if (ret != 0) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    } else {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMicrophones(std::vector<::aidl::android::media::audio::common::MicrophoneInfo>* _aidl_return) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateAudioMode(AudioMode in_mode) {
    LOG(INFO) << __func__ << ": called with mode " << (int)in_mode;
    // Convert AIDL AudioMode to legacy audio_mode_t
    audio_mode_t legacyMode = AUDIO_MODE_NORMAL;
    switch (in_mode) {
        case AudioMode::NORMAL: legacyMode = AUDIO_MODE_NORMAL; break;
        case AudioMode::RINGTONE: legacyMode = AUDIO_MODE_RINGTONE; break;
        case AudioMode::IN_CALL: legacyMode = AUDIO_MODE_IN_CALL; break;
        case AudioMode::IN_COMMUNICATION: legacyMode = AUDIO_MODE_IN_COMMUNICATION; break;
        case AudioMode::CALL_SCREEN: legacyMode = AUDIO_MODE_CALL_SCREEN; break; 
        default: break;
    }
    
    if (mLegacyDevice->set_mode) {
        mLegacyDevice->set_mode(mLegacyDevice, legacyMode);
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateScreenRotation(IModule::ScreenRotation in_rotation) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::updateScreenState(bool in_isTurnedOn) {
    LOG(INFO) << __func__ << ": " << (in_isTurnedOn ? "on" : "off");
    if (mLegacyDevice && mLegacyDevice->set_parameters) {
        mLegacyDevice->set_parameters(mLegacyDevice,
                in_isTurnedOn ? "screen_state=on" : "screen_state=off");
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getSoundDose(std::shared_ptr<sounddose::ISoundDose>* _aidl_return) {
    *_aidl_return = nullptr;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::generateHwAvSyncId(int32_t* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Module::getVendorParameters(const std::vector<std::string>& in_ids, std::vector<::aidl::android::hardware::audio::core::VendorParameter>* _aidl_return) {
    _aidl_return->clear();
    _aidl_return->reserve(in_ids.size());
    for (const auto& id : in_ids) {
        VendorParameter parameter;
        parameter.id = id;
        _aidl_return->push_back(std::move(parameter));
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::setVendorParameters(const std::vector<::aidl::android::hardware::audio::core::VendorParameter>& in_parameters, bool in_async) {
    (void)in_async;
    if (!mLegacyDevice || !mLegacyDevice->set_parameters) return ndk::ScopedAStatus::ok();
    for (const auto& param : in_parameters) {
        // VendorParameter::value is an Any (parcelable); use the id as a key
        // and forward it as a key-value pair if we can
        std::string kv = param.id;
        mLegacyDevice->set_parameters(mLegacyDevice, kv.c_str());
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::addDeviceEffect(int32_t in_portConfigId, const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::removeDeviceEffect(int32_t in_portConfigId, const std::shared_ptr<::aidl::android::hardware::audio::effect::IEffect>& in_effect) {
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getMmapPolicyInfos(
        ::aidl::android::media::audio::common::AudioMMapPolicyType in_mmapPolicyType,
        std::vector<::aidl::android::media::audio::common::AudioMMapPolicyInfo>* _aidl_return) {
    (void)in_mmapPolicyType;
    _aidl_return->clear();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::supportsVariableLatency(bool* _aidl_return) {
    *_aidl_return = false;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAAudioMixerBurstCount(int32_t* _aidl_return) {
    *_aidl_return = 0;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Module::getAAudioHardwareBurstMinUsec(int32_t* _aidl_return) {
    *_aidl_return = 0;
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::audio::core
