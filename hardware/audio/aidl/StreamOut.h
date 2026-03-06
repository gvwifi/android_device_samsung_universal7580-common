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
#include <aidl/android/hardware/audio/core/IStreamOut.h>
#include <aidl/android/hardware/audio/core/BnStreamOut.h>
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

class StreamOut : public BnStreamOut {
  public:
    StreamOut(audio_hw_device_t* device, struct audio_stream_out* legacyStream);
    ~StreamOut() override;

    // IStreamOut Interface
    ndk::ScopedAStatus getStreamCommon(
            std::shared_ptr<IStreamCommon>* _aidl_return) override;
    ndk::ScopedAStatus updateMetadata(
            const ::aidl::android::hardware::audio::common::SourceMetadata& in_sourceMetadata)
            override;
    ndk::ScopedAStatus updateOffloadMetadata(
            const ::aidl::android::hardware::audio::common::AudioOffloadMetadata&
                    in_offloadMetadata) override;
    ndk::ScopedAStatus getHwVolume(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwVolume(const std::vector<float>& in_channelVolumes) override;
    ndk::ScopedAStatus getAudioDescriptionMixLevel(float* _aidl_return) override;
    ndk::ScopedAStatus setAudioDescriptionMixLevel(float in_leveldB) override;
    ndk::ScopedAStatus getDualMonoMode(
            ::aidl::android::media::audio::common::AudioDualMonoMode* _aidl_return) override;
    ndk::ScopedAStatus setDualMonoMode(
            ::aidl::android::media::audio::common::AudioDualMonoMode in_mode) override;
    ndk::ScopedAStatus getRecommendedLatencyModes(
            std::vector<::aidl::android::media::audio::common::AudioLatencyMode>* _aidl_return)
            override;
    ndk::ScopedAStatus setLatencyMode(
            ::aidl::android::media::audio::common::AudioLatencyMode in_mode) override;
    ndk::ScopedAStatus getPlaybackRateParameters(
            ::aidl::android::media::audio::common::AudioPlaybackRate* _aidl_return)
            override;
    ndk::ScopedAStatus setPlaybackRateParameters(
            const ::aidl::android::media::audio::common::AudioPlaybackRate&
                    in_playbackRate) override;
    ndk::ScopedAStatus selectPresentation(int32_t in_presentationId, int32_t in_programId) override;

    // IStreamCommon Interface (via BnStreamCommon? No, IStreamCommon is returned by getStreamCommon)
    // We need a separate class or implement it here?
    // Standard practice: Return a helper or implement IStreamCommon separately.
    // For simplicity, we can implement methods that might be called on the stream context.
    
    // NOTE: In AIDL HAL, the framework interacts with the StreamDescriptor, which contains FMQs
    // for data and commands. The StreamOut implementation is responsible for reading from the
    // FMQ and writing to the hardware.
    // This usually requires a worker thread. Default implementations (ModuleAlsa) handle this.
    // Since we are wrapping a blocking C API (write), we need to handle the FMQ <-> write bridge.
    
    // HOWEVER: Writing a full FMQ handler from scratch is complex.
    // Ideally we would inherit from StreamAlsa or use a helper. 
    // Plan: We will use the *Stub* approach for now where we might not fully implement the FMQ 
    // mechanism if we can't reusing AOSP code easily, BUT we MUST to get audio.
    
    // WAIT: The "Legacy Wrapper" implies we are providing the AIDL interface.
    // The framework expects us to provide a valid descriptor with FMQs.
    // We cannot just "wrap" audio_stream_out->write directly without handling the FMQ.
    
    // Let's implement the basic interface methods first.
    // Real data transfer logic needs to go into a thread that polls the FMQ and calls legacy->write.
    // For the initial build, we will stub complex parts to verify registration.

  private:
    audio_hw_device_t* mLegacyDevice = nullptr;
    struct audio_stream_out* mLegacyStream;
    std::shared_ptr<StreamCommon> mStreamCommon;

    // State machine
    std::atomic<StreamDescriptor::State> mState{StreamDescriptor::State::STANDBY};

    // Position / latency tracking
    int32_t mLatencyMs = 0;
    int64_t mFramesWritten = 0;  // total frames written to hardware
    size_t  mFrameSize = 4;      // bytes per frame (channels * bits/8)

    // Data Plane
    std::unique_ptr<CommandMQ> mCommandMQ;
    std::unique_ptr<ReplyMQ> mReplyMQ;
    std::unique_ptr<DataMQ> mDataMQ;
    std::thread mWorker;
    std::atomic<bool> mStopWorker{false};
    std::atomic<bool> mClosed{false};

    void workerThread();
public:
    // Helper to setup FMQs and return descriptor
    ndk::ScopedAStatus prepareToClose();
    ndk::ScopedAStatus close();
    
    // Method to initialize FMQs called by Module
    ndk::ScopedAStatus init(StreamDescriptor* desc);
};

}  // namespace aidl::android::hardware::audio::core
