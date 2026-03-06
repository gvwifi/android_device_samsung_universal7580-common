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

#include <android-base/logging.h>
#include <android/binder_interface_utils.h>
#include <health-impl/Health.h>
#include <health/utils.h>

#ifndef CHARGER_FORCE_NO_UI
#define CHARGER_FORCE_NO_UI 0
#endif

#if !CHARGER_FORCE_NO_UI
#include <health-impl/ChargerUtils.h>
#endif

using aidl::android::hardware::health::HalHealthLoop;
using aidl::android::hardware::health::Health;

#if !CHARGER_FORCE_NO_UI
using aidl::android::hardware::health::charger::ChargerCallback;
using aidl::android::hardware::health::charger::ChargerModeMain;
#endif

static constexpr const char* gInstanceName = "default";
static constexpr std::string_view gChargerArg{"--charger"};

int main(int argc, char** argv) {
#ifdef __ANDROID_RECOVERY__
    android::base::InitLogging(argv, android::base::KernelLogger);
#endif

    LOG(INFO) << "Samsung Health HAL starting...";

    // make a default health service for Samsung/Exynos devices
    auto config = std::make_unique<healthd_config>();
    ::android::hardware::health::InitHealthdConfig(config.get());
    
    // Samsung/Exynos specific battery configuration
    config->batteryStatusPath = "/sys/class/power_supply/battery/status";
    config->batteryHealthPath = "/sys/class/power_supply/battery/health";
    config->batteryPresentPath = "/sys/class/power_supply/battery/present";
    config->batteryCapacityPath = "/sys/class/power_supply/battery/capacity";
    config->batteryVoltagePath = "/sys/class/power_supply/battery/voltage_now";
    config->batteryTemperaturePath = "/sys/class/power_supply/battery/temp";
    config->batteryTechnologyPath = "/sys/class/power_supply/battery/technology";
    config->batteryCurrentNowPath = "/sys/class/power_supply/battery/current_now";
    config->batteryCurrentAvgPath = "/sys/class/power_supply/battery/current_avg";
    config->batteryChargeCounterPath = "/sys/class/power_supply/battery/charge_counter";
    config->batteryFullChargePath = "/sys/class/power_supply/battery/charge_full";
    config->batteryCycleCountPath = "/sys/class/power_supply/battery/cycle_count";
    
    auto binder = ndk::SharedRefBase::make<Health>(gInstanceName, std::move(config));

    if (argc >= 2 && argv[1] == gChargerArg) {
#if !CHARGER_FORCE_NO_UI
        LOG(INFO) << "Starting Samsung charger mode with UI.";
        return ChargerModeMain(binder, std::make_shared<ChargerCallback>(binder));
#else
        LOG(INFO) << "Starting Samsung charger mode without UI.";
#endif
    } else {
        LOG(INFO) << "Starting Samsung health HAL.";
    }

    auto hal_health_loop = std::make_shared<HalHealthLoop>(binder, binder);
    return hal_health_loop->StartLoop();
}
