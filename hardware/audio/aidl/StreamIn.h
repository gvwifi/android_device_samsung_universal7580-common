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

#include <hardware/audio.h>
#include <fmq/AidlMessageQueue.h>
#include <aidl/android/hardware/audio/core/IStreamIn.h>
#include <aidl/android/hardware/audio/core/BnStreamIn.h>
#include <aidl/android/hardware/audio/core/StreamDescriptor.h>
#include <thread>
#include <atomic>

#include "StreamCommon.h"

using ::aidl::android::hardware::common::fmq::MQDescriptor;
using ::aidl::android::hardware::common::fmq::SynchronizedReadWrite;
using ::aidl::android::hardware::audio::core::StreamDescriptor;

namespace aidl::android::hardware::audio::core {

using DataMQ = ::android::AidlMessageQueue<int8_t, SynchronizedReadWrite>;
using CommandMQ = ::android::AidlMessageQueue<StreamDescriptor::Command, SynchronizedReadWrite>;
using ReplyMQ = ::android::AidlMessageQueue<StreamDescriptor::Reply, SynchronizedReadWrite>;

class StreamIn : public BnStreamIn {
  public:
    StreamIn(audio_hw_device_t* device, struct audio_stream_in* legacyStream);
    ~StreamIn() override;

    // IStreamIn Interface
    ndk::ScopedAStatus getStreamCommon(
            std::shared_ptr<IStreamCommon>* _aidl_return) override;
    ndk::ScopedAStatus getActiveMicrophones(
            std::vector<::aidl::android::media::audio::common::MicrophoneDynamicInfo>* _aidl_return) override;
    ndk::ScopedAStatus getMicrophoneDirection(
            ::aidl::android::hardware::audio::core::IStreamIn::MicrophoneDirection*
                    _aidl_return) override;
    ndk::ScopedAStatus setMicrophoneDirection(
            ::aidl::android::hardware::audio::core::IStreamIn::MicrophoneDirection in_direction) override;
    ndk::ScopedAStatus getMicrophoneFieldDimension(float* _aidl_return) override;
    ndk::ScopedAStatus setMicrophoneFieldDimension(float in_zoom) override;
    ndk::ScopedAStatus updateMetadata(
            const ::aidl::android::hardware::audio::common::SinkMetadata& in_sinkMetadata) override;
    ndk::ScopedAStatus getHwGain(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwGain(const std::vector<float>& in_channelGains) override;

    // TODO: Same note as StreamOut - we need to implement FMQ handling to actually push data.

  private:
    audio_hw_device_t* mLegacyDevice = nullptr;
    struct audio_stream_in* mLegacyStream;
    std::shared_ptr<StreamCommon> mStreamCommon;

    // State machine
    std::atomic<StreamDescriptor::State> mState{StreamDescriptor::State::STANDBY};

    // Position / misc tracking
    int64_t mFramesRead = 0;
    size_t  mFrameSize  = 4;

    // Data Plane
    std::unique_ptr<CommandMQ> mCommandMQ;
    std::unique_ptr<ReplyMQ> mReplyMQ;
    std::unique_ptr<DataMQ> mDataMQ;
    std::thread mWorker;
    std::atomic<bool> mStopWorker{false};
    std::atomic<bool> mClosed{false};

    void workerThread();

public:
    ndk::ScopedAStatus prepareToClose();
    ndk::ScopedAStatus close();
    ndk::ScopedAStatus init(StreamDescriptor* desc);
};

}  // namespace aidl::android::hardware::audio::core
