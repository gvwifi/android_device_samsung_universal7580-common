/*
 * Copyright (C) 2026 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-Broadcom
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <log/log.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "libshim_gpsd"

// Dummy implementation of missing libsensor API:
// _ZN7android16SensorEventQueue4readEP12ASensorEventj
extern "C" ssize_t _ZN7android16SensorEventQueue4readEP12ASensorEventj(void* /*this*/, void* /*events*/, size_t /*count*/) {
    ALOGW("libshim_gpsd: Stubbed SensorEventQueue::read called");
    // Return 0 implying no events read
    return 0;
}

// _ZN7android13SensorManager21getInstanceForPackageERKNS_8String16E
// android::SensorManager::getInstanceForPackage(android::String16 const&)
extern "C" void* _ZN7android13SensorManager21getInstanceForPackageERKNS_8String16E(void* /*this*/, void* /*packageName*/) {
    ALOGW("libshim_gpsd: Stubbed SensorManager::getInstanceForPackage called");
    return nullptr;
}

// _ZN7android13SensorManager13getSensorListEPPKPKNS_6SensorE
// android::SensorManager::getSensorList(android::Sensor const* const** const&)
extern "C" ssize_t _ZN7android13SensorManager13getSensorListEPPKPKNS_6SensorE(void* /*this*/, void** /*list*/) {
    ALOGW("libshim_gpsd: Stubbed SensorManager::getSensorList called");
    return 0; // Return 0 sensors available
}

// _ZN7android13SensorManager16createEventQueueENS_7String8Ei
// android::SensorManager::createEventQueue(android::String8, int)
extern "C" void* _ZN7android13SensorManager16createEventQueueENS_7String8Ei(void* /*this*/, void* /*str*/, int /*mode*/) {
    ALOGW("libshim_gpsd: Stubbed SensorManager::createEventQueue called");
    return nullptr;
}

// _ZN7android13SensorManager16getDefaultSensorEi
// android::SensorManager::getDefaultSensor(int)
extern "C" void* _ZN7android13SensorManager16getDefaultSensorEi(void* /*this*/, int /*type*/) {
    ALOGW("libshim_gpsd: Stubbed SensorManager::getDefaultSensor called");
    return nullptr;
}

// _ZNK7android6Sensor7getNameEv
// android::Sensor::getName() const
extern "C" const char* _ZNK7android6Sensor7getNameEv(void* /*this*/) {
    ALOGW("libshim_gpsd: Stubbed Sensor::getName called");
    return "StubbedSensor";
}

// _ZN7android15elapsedRealtimeEv
extern "C" int64_t _ZN7android15elapsedRealtimeEv() {
    return 0;
}

// Looper and String8/16 are missing because libutils C++ ABIs changed.
// _ZN7android6LooperC1Eb
extern "C" void _ZN7android6LooperC1Eb(void* /*this*/, bool /*allowNonCallbacks*/) {}

// _ZN7android6Looper5addFdEiiiPFiiiPvES1_
extern "C" int _ZN7android6Looper5addFdEiiiPFiiiPvES1_(void* /*this*/, int /*fd*/, int /*ident*/, int /*events*/, void* /*callback*/, void* /*data*/) { return -1; }

// _ZN7android6Looper7pollAllEiPiS1_PPv
extern "C" int _ZN7android6Looper7pollAllEiPiS1_PPv(void* /*this*/, int /*timeoutMillis*/, int* /*outFd*/, int* /*outEvents*/, void** /*outData*/) { return -1; }

// _ZN7android6Looper8removeFdEi
extern "C" int _ZN7android6Looper8removeFdEi(void* /*this*/, int /*fd*/) { return -1; }

extern "C" void _ZN7android7String8C1EPKc(void* /*this*/, const char* /*o*/) {}
extern "C" void _ZN7android7String8D1Ev(void* /*this*/) {}
extern "C" void _ZN7android8String16C1EPKc(void* /*this*/, const char* /*o*/) {}
extern "C" void _ZN7android8String16D1Ev(void* /*this*/) {}

// Queue functions
extern "C" int _ZNK7android16SensorEventQueue12enableSensorEPKNS_6SensorE(void* /*this*/, void* /*sensor*/) { return 0; }
extern "C" int _ZNK7android16SensorEventQueue12setEventRateEPKNS_6SensorEx(void* /*this*/, void* /*sensor*/, int64_t /*rate*/) { return 0; }
extern "C" int _ZNK7android16SensorEventQueue13disableSensorEPKNS_6SensorE(void* /*this*/, void* /*sensor*/) { return 0; }
extern "C" int _ZNK7android16SensorEventQueue5getFdEv(void* /*this*/) { return -1; }

// Sensor Info
extern "C" float _ZNK7android6Sensor11getMaxValueEv(void* /*this*/) { return 0.0f; }
extern "C" int32_t _ZNK7android6Sensor11getMinDelayEv(void* /*this*/) { return 0; }
extern "C" float _ZNK7android6Sensor13getPowerUsageEv(void* /*this*/) { return 0.0f; }
extern "C" float _ZNK7android6Sensor13getResolutionEv(void* /*this*/) { return 0.0f; }
extern "C" int32_t _ZNK7android6Sensor7getTypeEv(void* /*this*/) { return 0; }
extern "C" int32_t _ZNK7android6Sensor9getHandleEv(void* /*this*/) { return 0; }
extern "C" const char* _ZNK7android6Sensor9getVendorEv(void* /*this*/) { return "StubbedVendor"; }

// RefBase
extern "C" void _ZNK7android7RefBase9decStrongEPKv(void* /*this*/, const void* /*id*/) {}
extern "C" void _ZNK7android7RefBase9incStrongEPKv(void* /*this*/, const void* /*id*/) {}

// Missing BoringSSL APIs (since Android 8 / Old OpenSSL)
extern "C" {

void SSL_load_error_strings(void) {
    ALOGW("libshim_gpsd: Stubbed SSL_load_error_strings");
}

int SSL_library_init(void) {
    ALOGW("libshim_gpsd: Stubbed SSL_library_init");
    return 1; // Assuming 1 means success usually
}

int CRYPTO_num_locks(void) {
    ALOGW("libshim_gpsd: Stubbed CRYPTO_num_locks");
    return 1; 
}

void CRYPTO_set_id_callback(unsigned long (*/*id_function*/)(void)) {
    ALOGW("libshim_gpsd: Stubbed CRYPTO_set_id_callback");
}

void CRYPTO_set_locking_callback(void (*/*locking_function*/)(int mode, int n, const char *file, int line)) {
    ALOGW("libshim_gpsd: Stubbed CRYPTO_set_locking_callback");
}

} // extern "C"
