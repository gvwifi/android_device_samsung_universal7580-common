/*
 * Copyright (C) 2020 The LineageOS Project
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

#define LOG_TAG "vibrator-samsung"

#include "Vibrator.h"

#include <android-base/logging.h>

#include <cmath>
#include <fstream>
#include <iostream>

namespace aidl {
namespace android {
namespace hardware {
namespace vibrator {

/*
 * Write value to path and close file.
 */
template <typename T>
static ndk::ScopedAStatus writeNode(const std::string& path, const T& value) {
    std::ofstream node(path);
    if (!node) {
        LOG(ERROR) << "Failed to open: " << path;
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }

    LOG(DEBUG) << "writeNode node: " << path << " value: " << value;

    node << value << std::endl;
    if (!node) {
        LOG(ERROR) << "Failed to write: " << value;
        return ndk::ScopedAStatus::fromExceptionCode(EX_SERVICE_SPECIFIC);
    }

    return ndk::ScopedAStatus::ok();
}

static bool nodeExists(const std::string& path) {
    std::ofstream f(path.c_str());
    return f.good();
}

Vibrator::Vibrator() {
    mIsTimedOutVibrator = nodeExists(VIBRATOR_TIMEOUT_PATH);
    mHasTimedOutIntensity = nodeExists(VIBRATOR_INTENSITY_PATH);
}

ndk::ScopedAStatus Vibrator::getCapabilities(int32_t* _aidl_return) {
    LOG(VERBOSE) << "Vibrator reporting capabilities";
    *_aidl_return = IVibrator::CAP_AMPLITUDE_CONTROL | IVibrator::CAP_EXTERNAL_CONTROL;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::off() {
    return activate(0);
}

ndk::ScopedAStatus Vibrator::on(int32_t timeoutMs,
                                const std::shared_ptr<IVibratorCallback>& /*callback*/) {
    return activate(timeoutMs);
}

ndk::ScopedAStatus Vibrator::perform(Effect effect, EffectStrength strength,
                                     const std::shared_ptr<IVibratorCallback>& /*callback*/,
                                     int32_t* _aidl_return) {
    bool supported;

    LOG(DEBUG) << "perform effect: " << static_cast<int32_t>(effect)
               << ", strength: " << static_cast<int32_t>(strength);

    float amplitude = strengthToAmplitude(strength, &supported);
    if (!supported) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    setAmplitude(amplitude);

    uint32_t ms = effectToMs(effect, &supported);
    if (!supported) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    ndk::ScopedAStatus status = activate(ms);
    if (!status.isOk()) {
        *_aidl_return = 0;
        return status;
    }

    *_aidl_return = static_cast<int32_t>(ms);
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getSupportedEffects(std::vector<Effect>* _aidl_return) {
    *_aidl_return = {
            Effect::CLICK,       Effect::DOUBLE_CLICK, Effect::HEAVY_CLICK,
            Effect::TICK,        Effect::TEXTURE_TICK, Effect::THUD,
            Effect::POP,         Effect::RINGTONE_1,   Effect::RINGTONE_2,
            Effect::RINGTONE_3,  Effect::RINGTONE_4,   Effect::RINGTONE_5,
            Effect::RINGTONE_6,  Effect::RINGTONE_7,   Effect::RINGTONE_8,
            Effect::RINGTONE_9,  Effect::RINGTONE_10,  Effect::RINGTONE_11,
            Effect::RINGTONE_12, Effect::RINGTONE_13,  Effect::RINGTONE_14,
            Effect::RINGTONE_15,
    };
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setAmplitude(float amplitude) {
    if (amplitude <= 0.0f || amplitude > 1.0f) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }

    LOG(DEBUG) << "setting amplitude: " << amplitude;

    uint32_t intensity = std::lround(amplitude * INTENSITY_MAX);
    if (intensity > INTENSITY_MAX) {
        intensity = INTENSITY_MAX;
    }

    LOG(DEBUG) << "setting intensity: " << intensity;

    if (mHasTimedOutIntensity) {
        return writeNode(VIBRATOR_INTENSITY_PATH, intensity);
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::setExternalControl(bool enabled) {
    if (mEnabled) {
        LOG(WARNING) << "Setting external control while the vibrator is enabled is "
                        "unsupported!";
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    LOG(INFO) << "ExternalControl: " << mExternalControl << " -> " << enabled;
    mExternalControl = enabled;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Vibrator::getCompositionDelayMax(int32_t* /*maxDelayMs*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getCompositionSizeMax(int32_t* /*maxSize*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedPrimitives(
        std::vector<CompositePrimitive>* /*supported*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPrimitiveDuration(CompositePrimitive /*primitive*/,
                                                  int32_t* /*durationMs*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::compose(const std::vector<CompositeEffect>& /*composite*/,
                                     const std::shared_ptr<IVibratorCallback>& /*callback*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedAlwaysOnEffects(std::vector<Effect>* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::alwaysOnEnable(int32_t /*id*/, Effect /*effect*/,
                                            EffectStrength /*strength*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::alwaysOnDisable(int32_t /*id*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getResonantFrequency(float* /*resonantFreqHz*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getQFactor(float* /*qFactor*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getFrequencyResolution(float* /*freqResolutionHz*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getFrequencyMinimum(float* /*freqMinimumHz*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getBandwidthAmplitudeMap(std::vector<float>* /*_aidl_return*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPwlePrimitiveDurationMax(int32_t* /*durationMs*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getPwleCompositionSizeMax(int32_t* /*maxSize*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::getSupportedBraking(std::vector<Braking>* /*supported*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus Vibrator::composePwle(
        const std::vector<PrimitivePwle>& /*composite*/,
        const std::shared_ptr<IVibratorCallback>& /*callback*/) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

// Private methods

ndk::ScopedAStatus Vibrator::activate(uint32_t timeoutMs) {
    std::lock_guard<std::mutex> lock{mMutex};
    if (!mIsTimedOutVibrator) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }

    mEnabled = (timeoutMs > 0);
    return writeNode(VIBRATOR_TIMEOUT_PATH, timeoutMs);
}

float Vibrator::strengthToAmplitude(EffectStrength strength, bool* supported) {
    *supported = true;

    switch (strength) {
        case EffectStrength::LIGHT:
            return 78.0f / 255.0f;
        case EffectStrength::MEDIUM:
            return 128.0f / 255.0f;
        case EffectStrength::STRONG:
            return 204.0f / 255.0f;
    }

    *supported = false;
    return 0.0f;
}

uint32_t Vibrator::effectToMs(Effect effect, bool* supported) {
    *supported = true;

    switch (effect) {
        case Effect::CLICK:
            return 20;
        case Effect::DOUBLE_CLICK:
            return 25;
        case Effect::HEAVY_CLICK:
            return 30;
        case Effect::TICK:
        case Effect::TEXTURE_TICK:
        case Effect::THUD:
        case Effect::POP:
            return 15;
        case Effect::RINGTONE_1:
        case Effect::RINGTONE_2:
        case Effect::RINGTONE_3:
        case Effect::RINGTONE_4:
        case Effect::RINGTONE_5:
        case Effect::RINGTONE_6:
        case Effect::RINGTONE_7:
        case Effect::RINGTONE_8:
        case Effect::RINGTONE_9:
        case Effect::RINGTONE_10:
        case Effect::RINGTONE_11:
        case Effect::RINGTONE_12:
        case Effect::RINGTONE_13:
        case Effect::RINGTONE_14:
        case Effect::RINGTONE_15:
            return 300;
    }

    *supported = false;
    return 0;
}

}  // namespace vibrator
}  // namespace hardware
}  // namespace android
}  // namespace aidl
