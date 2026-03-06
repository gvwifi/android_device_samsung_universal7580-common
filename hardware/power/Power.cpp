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

#define LOG_TAG "android.hardware.power@1.0-service.universal7580"

#include <log/log.h>
#include "Power.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_0 {
namespace implementation {

Return<void> Power::setInteractive(bool interactive) {
    ALOGV("%s: interactive=%d", __func__, interactive);
    // CPU governor tweaks are handled via init.power.rc sysfs writes.
    return Void();
}

Return<void> Power::powerHint(PowerHint hint, int32_t data) {
    ALOGV("%s: hint=%d data=%d", __func__, static_cast<int>(hint), data);
    // No-op: basic power management is done via cpufreq interactive governor.
    return Void();
}

Return<void> Power::setFeature(Feature feature, bool activate) {
    ALOGV("%s: feature=%d activate=%d", __func__, static_cast<int>(feature), activate);
    return Void();
}

Return<void> Power::getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) {
    hidl_vec<PowerStatePlatformSleepState> states;
    // Return empty list — device-specific sleep counters not implemented.
    _hidl_cb(states, Status::SUCCESS);
    return Void();
}

}  // namespace implementation
}  // namespace V1_0
}  // namespace power
}  // namespace hardware
}  // namespace android
