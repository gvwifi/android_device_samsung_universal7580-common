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

/**
 * Config.cpp — Universal7580 AIDL Audio HAL engine configuration.
 *
 * getEngineConfig() returns a fully-populated AudioHalEngineConfig whose
 * volumeGroups are the direct C++ translation of:
 *   audio_policy_engine_stream_volumes.xml  (group definitions / inline points)
 *   audio_policy_engine_default_stream_volumes.xml (reference curves)
 *
 * Volume group names use the "AUDIO_STREAM_*" format to match the compiled-in
 * gOrderedStrategies in EngineDefaultConfig.h, which is what audioserver falls
 * back to when productStrategies is left empty.
 *
 * productStrategies is left empty so audioserver uses its built-in defaults.
 */

#define LOG_TAG "audio_hw_aidl"
#include <android-base/logging.h>

#include "Config.h"

#include <aidl/android/media/audio/common/AudioHalEngineConfig.h>
#include <aidl/android/media/audio/common/AudioHalVolumeGroup.h>
#include <aidl/android/media/audio/common/AudioHalVolumeCurve.h>

namespace aidl::android::hardware::audio::core {

using ::aidl::android::media::audio::common::AudioHalEngineConfig;
using ::aidl::android::media::audio::common::AudioHalVolumeGroup;
using ::aidl::android::media::audio::common::AudioHalVolumeCurve;
using DC = ::aidl::android::media::audio::common::AudioHalVolumeCurve::DeviceCategory;
using CP = ::aidl::android::media::audio::common::AudioHalVolumeCurve::CurvePoint;

// ---------------------------------------------------------------------------
// Reference curves (inlined from audio_policy_engine_default_stream_volumes.xml)
// ---------------------------------------------------------------------------
static const std::vector<CP> kMedia      = {{1,-5800},{20,-4000},{60,-1700},{100,0}};
static const std::vector<CP> kHeadset    = {{1,-4950},{33,-3350},{66,-1700},{100,0}};
static const std::vector<CP> kSpeaker    = {{1,-5800},{20,-4000},{60,-1700},{100,0}};
static const std::vector<CP> kSpkSystem  = {{1,-4680},{42,-2070},{85,-540}, {100,0}};
static const std::vector<CP> kEarpiece   = {{1,-4950},{33,-3350},{66,-1700},{100,0}};
static const std::vector<CP> kExtMedia   = {{1,-5800},{20,-4000},{60,-2100},{100,-1000}};
static const std::vector<CP> kHearingAid = {{1,-12700},{20,-8000},{60,-4000},{100,0}};
static const std::vector<CP> kSystem     = {{1,-2400},{33,-1800},{66,-1200},{100,-600}};
static const std::vector<CP> kFullScale  = {{0,0},{100,0}};
static const std::vector<CP> kSilent     = {{0,-9600},{100,-9600}};
// Non-mutable (index 0 always audible — used for alarm/accessibility/etc.)
static const std::vector<CP> kNmMedia    = {{0,-5800},{20,-4000},{60,-1700},{100,0}};
static const std::vector<CP> kNmHeadset  = {{0,-4950},{33,-3350},{66,-1700},{100,0}};
static const std::vector<CP> kNmSpeaker  = {{0,-5800},{20,-4000},{60,-1700},{100,0}};
static const std::vector<CP> kNmEarpiece = {{0,-4950},{33,-3350},{66,-1700},{100,0}};
static const std::vector<CP> kNmExtMedia = {{0,-5800},{20,-4000},{60,-2100},{100,-1000}};
static const std::vector<CP> kNmHearAid  = {{0,-12700},{20,-8000},{60,-4000},{100,0}};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static AudioHalVolumeCurve curve(DC cat, const std::vector<CP>& pts) {
    AudioHalVolumeCurve c;
    c.deviceCategory = cat;
    c.curvePoints = pts;
    return c;
}

/** Build a standard 5-category volume group. */
static AudioHalVolumeGroup group(const char* name, int minIdx, int maxIdx,
                                  const std::vector<CP>& hs,
                                  const std::vector<CP>& spk,
                                  const std::vector<CP>& ear,
                                  const std::vector<CP>& ext,
                                  const std::vector<CP>& ha) {
    AudioHalVolumeGroup g;
    g.name     = name;
    g.minIndex = minIdx;
    g.maxIndex = maxIdx;
    g.volumeCurves = {
        curve(DC::HEADSET,     hs),
        curve(DC::SPEAKER,     spk),
        curve(DC::EARPIECE,    ear),
        curve(DC::EXT_MEDIA,   ext),
        curve(DC::HEARING_AID, ha),
    };
    return g;
}

// ---------------------------------------------------------------------------
// Static engine config — built once, returned to every getEngineConfig() call.
// Volume group names MUST match the volumeGroup= strings in gOrderedStrategies
// (EngineDefaultConfig.h) because productStrategies is left empty.
// ---------------------------------------------------------------------------

static const AudioHalEngineConfig& staticEngineConfig() {
    static const AudioHalEngineConfig kCfg = []() {
        AudioHalEngineConfig cfg;
        cfg.defaultProductStrategyId = -1;  // SYS_RESERVED_NONE — let engine choose
        // productStrategies: empty → audioserver uses gOrderedStrategies built-ins

        auto& vg = cfg.volumeGroups;

        // AUDIO_STREAM_VOICE_CALL  min=1 max=7
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_VOICE_CALL"; g.minIndex = 1; g.maxIndex = 7;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{0,-4200},{33,-2800},{66,-1400},{100,0}}),
                curve(DC::SPEAKER,     {{0,-2400},{33,-1600},{66,-800}, {100,0}}),
                curve(DC::EARPIECE,    {{0,-2700},{33,-1800},{66,-900}, {100,0}}),
                curve(DC::EXT_MEDIA,   kMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_BLUETOOTH_SCO  min=0 max=15
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_BLUETOOTH_SCO"; g.minIndex = 0; g.maxIndex = 15;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{0,-4200},{33,-2800},{66,-1400},{100,0}}),
                curve(DC::SPEAKER,     {{0,-2400},{33,-1600},{66,-800}, {100,0}}),
                curve(DC::EARPIECE,    {{0,-4200},{33,-2800},{66,-1400},{100,0}}),
                curve(DC::EXT_MEDIA,   kMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_RING  min=0 max=7
        vg.push_back(group("AUDIO_STREAM_RING", 0, 7,
            kHeadset, kSpeaker, kEarpiece, kExtMedia, kHearingAid));

        // AUDIO_STREAM_ALARM  min=1 max=7  (non-mutable)
        vg.push_back(group("AUDIO_STREAM_ALARM", 1, 7,
            kNmHeadset, kNmSpeaker, kNmEarpiece, kNmExtMedia, kNmHearAid));

        // AUDIO_STREAM_ENFORCED_AUDIBLE  min=0 max=7
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_ENFORCED_AUDIBLE"; g.minIndex = 0; g.maxIndex = 7;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{1,-3000},{33,-2600},{66,-2200},{100,-1800}}),
                curve(DC::SPEAKER,     {{1,-3400},{71,-2400},{100,-2000}}),
                curve(DC::EARPIECE,    kSystem),
                curve(DC::EXT_MEDIA,   kExtMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_ACCESSIBILITY  min=1 max=15  (non-mutable)
        vg.push_back(group("AUDIO_STREAM_ACCESSIBILITY", 1, 15,
            kNmMedia, kNmSpeaker, kNmMedia, kNmMedia, kNmHearAid));

        // AUDIO_STREAM_NOTIFICATION  min=0 max=7
        vg.push_back(group("AUDIO_STREAM_NOTIFICATION", 0, 7,
            kHeadset, kSpkSystem, kEarpiece, kExtMedia, kHeadset));

        // AUDIO_STREAM_ASSISTANT  min=0 max=15
        vg.push_back(group("AUDIO_STREAM_ASSISTANT", 0, 15,
            kMedia, kSpeaker, kMedia, kMedia, kHearingAid));

        // AUDIO_STREAM_MUSIC  min=0 max=25  (also serves as defaultVolumeConfig)
        vg.push_back(group("AUDIO_STREAM_MUSIC", 0, 25,
            kMedia, kSpeaker, kMedia, kMedia, kHearingAid));

        // AUDIO_STREAM_SYSTEM  min=0 max=7
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_SYSTEM"; g.minIndex = 0; g.maxIndex = 7;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{1,-3000},{33,-2600},{66,-2200},{100,-1800}}),
                curve(DC::SPEAKER,     {{1,-5100},{57,-2800},{71,-2500},{85,-2300},{100,-2100}}),
                curve(DC::EARPIECE,    kSystem),
                curve(DC::EXT_MEDIA,   kExtMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_DTMF  min=0 max=15
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_DTMF"; g.minIndex = 0; g.maxIndex = 15;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{1,-3000},{33,-2600},{66,-2200},{100,-1800}}),
                curve(DC::SPEAKER,     {{1,-4000},{71,-2400},{100,-1400}}),
                curve(DC::EARPIECE,    kSystem),
                curve(DC::EXT_MEDIA,   kExtMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_CALL_ASSISTANT  min=0 max=15  (same curves as voice_call)
        {
            AudioHalVolumeGroup g;
            g.name = "AUDIO_STREAM_CALL_ASSISTANT"; g.minIndex = 0; g.maxIndex = 15;
            g.volumeCurves = {
                curve(DC::HEADSET,     {{0,-4200},{33,-2800},{66,-1400},{100,0}}),
                curve(DC::SPEAKER,     {{0,-2400},{33,-1600},{66,-800}, {100,0}}),
                curve(DC::EARPIECE,    {{0,-2700},{33,-1800},{66,-900}, {100,0}}),
                curve(DC::EXT_MEDIA,   kMedia),
                curve(DC::HEARING_AID, kHearingAid),
            };
            vg.push_back(std::move(g));
        }

        // AUDIO_STREAM_TTS  min=0 max=15  (speaker full-scale, everything else silent)
        vg.push_back(group("AUDIO_STREAM_TTS", 0, 15,
            kSilent, kFullScale, kSilent, kSilent, kSilent));

        // System streams — full scale (0 dB), used internally by AudioFlinger.
        // AUDIO_STREAM_REROUTING and AUDIO_STREAM_PATCH also set defaultSystemVolumeConfig.
        vg.push_back(group("AUDIO_STREAM_REROUTING", 0, 15,
            kFullScale, kFullScale, kFullScale, kFullScale, kFullScale));

        // AUDIO_STREAM_PATCH — processed last so it seeds defaultSystemVolumeConfig
        vg.push_back(group("AUDIO_STREAM_PATCH", 0, 15,
            kFullScale, kFullScale, kFullScale, kFullScale, kFullScale));

        return cfg;
    }();
    return kCfg;
}

// ---------------------------------------------------------------------------
// IConfig interface implementation
// ---------------------------------------------------------------------------

ndk::ScopedAStatus Config::getSurroundSoundConfig(SurroundSoundConfig* _aidl_return) {
    *_aidl_return = {};
    LOG(DEBUG) << __func__ << ": returning empty SurroundSoundConfig";
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Config::getEngineConfig(
        ::aidl::android::media::audio::common::AudioHalEngineConfig* _aidl_return) {
    *_aidl_return = staticEngineConfig();
    LOG(DEBUG) << __func__ << ": returning engine config with "
               << _aidl_return->volumeGroups.size() << " volume groups";
    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::audio::core
