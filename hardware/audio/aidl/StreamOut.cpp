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

#define LOG_TAG "audio_hw_aidl_stream_out"

#include <android-base/logging.h>
#include <time.h>
#include <algorithm>

#include "StreamOut.h"
#include <sys/resource.h>
#include <pthread.h>

// Binder status codes used in StreamDescriptor::Reply::status
static constexpr int32_t STREAM_STATUS_OK               =   0;
static constexpr int32_t STREAM_STATUS_INVALID_OPERATION = -38;  // ENOSYS
static constexpr int32_t STREAM_STATUS_BAD_VALUE         = -22;  // EINVAL

namespace aidl::android::hardware::audio::core {

StreamOut::StreamOut(audio_hw_device_t* device, struct audio_stream_out* legacyStream)
    : mLegacyDevice(device), mLegacyStream(legacyStream) {
    mStreamCommon = ndk::SharedRefBase::make<StreamCommon>(
            [this]() { return this->close(); },
            [this]() { return this->prepareToClose(); });
    LOG(INFO) << "Created StreamOut wrapper for legacy stream: " << legacyStream;
}

StreamOut::~StreamOut() {
    if (mStreamCommon) {
        mStreamCommon->detach();
    }
    close();
}

ndk::ScopedAStatus StreamOut::init(StreamDescriptor* desc) {
    if (!mLegacyStream) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);

    mFrameSize = audio_stream_out_frame_size(mLegacyStream);
    if (mFrameSize == 0) mFrameSize = 4;  // stereo 16-bit fallback

    size_t bufferSize = mLegacyStream->common.get_buffer_size(&mLegacyStream->common);
    uint32_t sampleRate = mLegacyStream->common.get_sample_rate(&mLegacyStream->common);
    if (sampleRate == 0) sampleRate = 48000;

    // Read latency from legacy stream (milliseconds)
    if (mLegacyStream->get_latency) {
        mLatencyMs = (int32_t)mLegacyStream->get_latency(mLegacyStream);
    }
    if (mLatencyMs <= 0 || mLatencyMs > 1000) mLatencyMs = 45;  // safe default

    // Create FMQs (Command, Reply, Data)
    mCommandMQ = std::make_unique<CommandMQ>(1, true /* configureEventFlagWord */);
    mReplyMQ   = std::make_unique<ReplyMQ>  (1, true /* configureEventFlagWord */);

    // Enforce at least 20 ms worth of frames for the DataMQ
    size_t frameCount = bufferSize / mFrameSize;
    const size_t kMinFrames = sampleRate * 20 / 1000;  // 20 ms minimum
    if (frameCount < kMinFrames) {
        LOG(INFO) << "StreamOut: bumping frameCount from " << frameCount
                  << " to " << kMinFrames << " (20ms min)";
        frameCount = kMinFrames;
    }

    // DataMQ holds 4× a single-burst to provide headroom
    size_t dataMqSize = frameCount * mFrameSize * 4;
    mDataMQ = std::make_unique<DataMQ>(dataMqSize, true /* configureEventFlagWord */);

    if (!mCommandMQ->isValid() || !mReplyMQ->isValid() || !mDataMQ->isValid()) {
        LOG(ERROR) << "StreamOut::init: failed to create FMQs";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    desc->command          = mCommandMQ->dupeDesc();
    desc->reply            = mReplyMQ->dupeDesc();
    desc->audio.set<StreamDescriptor::AudioBuffer::Tag::fmq>(mDataMQ->dupeDesc());
    desc->frameSizeBytes   = (int32_t)mFrameSize;
    desc->bufferSizeFrames = (int32_t)frameCount;

    // Start worker thread
    mStopWorker = false;
    mState      = StreamDescriptor::State::STANDBY;
    mFramesWritten = 0;
    mWorker     = std::thread(&StreamOut::workerThread, this);

    LOG(INFO) << "StreamOut::init: frameSize=" << mFrameSize
              << " bufferFrames=" << frameCount
              << " sampleRate=" << sampleRate
              << " latencyMs=" << mLatencyMs
              << " dataMqBytes=" << dataMqSize;
    return ndk::ScopedAStatus::ok();
}

// ---------------------------------------------------------------------------
// Worker thread – full AIDL output stream state machine
// ---------------------------------------------------------------------------
// State transitions (from AOSP stream-out-sm.gv):
//   STANDBY + start   -> IDLE
//   STANDBY + burst   -> PAUSED   (discard data; producer active, consumer passive)
//   IDLE    + standby -> STANDBY
//   IDLE    + burst   -> ACTIVE   (begin writing to hardware)
//   ACTIVE  + burst   -> ACTIVE   (continue writing)
//   ACTIVE  + pause   -> PAUSED
//   ACTIVE  + drain   -> IDLE     (synchronous drain)
//   PAUSED  + burst   -> PAUSED   (buffer the data but don't write)
//   PAUSED  + start   -> ACTIVE
//   PAUSED  + flush   -> IDLE
//   DRAINING + burst  -> ACTIVE
//   DRAINING + pause  -> DRAIN_PAUSED
//   DRAIN_PAUSED + start -> DRAINING
//   DRAIN_PAUSED + burst -> PAUSED
//   DRAIN_PAUSED + flush -> IDLE
//   ANY     + getStatus -> current state (no side effects)
// ---------------------------------------------------------------------------
void StreamOut::workerThread() {
    LOG(INFO) << "StreamOut worker thread started (state=STANDBY)";
    setpriority(PRIO_PROCESS, 0, -19);

    std::vector<int8_t> buffer;
    buffer.reserve(mLegacyStream->common.get_buffer_size(&mLegacyStream->common) * 2);

    auto nowNs = []() -> int64_t {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (int64_t)ts.tv_sec * 1'000'000'000LL + ts.tv_nsec;
    };

    while (!mStopWorker) {
        StreamDescriptor::Command cmd;
        // 100 ms timeout so mStopWorker is checked regularly
        if (!mCommandMQ->readBlocking(&cmd, 1, 100'000'000LL)) {
            continue;
        }

        const StreamDescriptor::State state = mState.load();
        StreamDescriptor::Reply reply{};
        reply.status            = STREAM_STATUS_OK;
        reply.state             = state;
        reply.latencyMs         = mLatencyMs;
        reply.observable.frames = mFramesWritten;
        reply.observable.timeNs = nowNs();
        reply.hardware          = reply.observable;

        using Tag = StreamDescriptor::Command::Tag;

        switch (cmd.getTag()) {

            case Tag::getStatus:
                // No state change – already populated above
                break;

            case Tag::start:
                if (state == StreamDescriptor::State::STANDBY) {
                    mState = StreamDescriptor::State::IDLE;
                    reply.state = StreamDescriptor::State::IDLE;
                } else if (state == StreamDescriptor::State::PAUSED) {
                    mState = StreamDescriptor::State::ACTIVE;
                    reply.state = StreamDescriptor::State::ACTIVE;
                } else if (state == StreamDescriptor::State::DRAIN_PAUSED) {
                    mState = StreamDescriptor::State::DRAINING;
                    reply.state = StreamDescriptor::State::DRAINING;
                } else {
                    LOG(WARNING) << "StreamOut: 'start' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::burst: {
                const int32_t burstBytes = cmd.get<Tag::burst>();

                if (state == StreamDescriptor::State::STANDBY) {
                    // Discard data; go to PAUSED
                    if (burstBytes > 0) {
                        size_t n = std::min((size_t)burstBytes, mDataMQ->availableToRead());
                        if (n > 0) {
                            buffer.resize(n);
                            mDataMQ->read(buffer.data(), n);
                            reply.fmqByteCount = (int32_t)n;
                        }
                    }
                    mState = StreamDescriptor::State::PAUSED;
                    reply.state = StreamDescriptor::State::PAUSED;

                } else if (state == StreamDescriptor::State::IDLE   ||
                           state == StreamDescriptor::State::ACTIVE  ||
                           state == StreamDescriptor::State::DRAINING) {
                    // Read from FMQ and write to hardware; go ACTIVE
                    if (burstBytes > 0) {
                        buffer.resize((size_t)burstBytes);
                        if (mDataMQ->read(buffer.data(), (size_t)burstBytes)) {
                            ssize_t written = mLegacyStream->write(
                                    mLegacyStream, buffer.data(), (size_t)burstBytes);
                            if (written > 0) {
                                mFramesWritten += (int64_t)(written / (ssize_t)mFrameSize);
                                reply.fmqByteCount = (int32_t)written;
                            } else {
                                LOG(WARNING) << "StreamOut: legacy write() = " << written;
                            }
                        } else {
                            LOG(WARNING) << "StreamOut: DataMQ read(" << burstBytes << ") failed";
                        }
                    }
                    mState = StreamDescriptor::State::ACTIVE;
                    reply.state = StreamDescriptor::State::ACTIVE;

                } else if (state == StreamDescriptor::State::PAUSED     ||
                           state == StreamDescriptor::State::DRAIN_PAUSED) {
                    // Buffer only; stay PAUSED
                    reply.fmqByteCount = 0;
                    mState = StreamDescriptor::State::PAUSED;
                    reply.state = StreamDescriptor::State::PAUSED;

                } else {
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                // Refresh observable position (software-tracked write count)
                reply.observable.frames = mFramesWritten;
                reply.observable.timeNs = nowNs();
                // Query hardware presentation position when available for accuracy
                if (mLegacyStream->get_presentation_position) {
                    uint64_t hwFrames = 0;
                    struct timespec hwTs{};
                    if (mLegacyStream->get_presentation_position(mLegacyStream, &hwFrames, &hwTs) == 0) {
                        reply.hardware.frames = (int64_t)hwFrames;
                        reply.hardware.timeNs = (int64_t)hwTs.tv_sec * 1'000'000'000LL + hwTs.tv_nsec;
                    } else {
                        reply.hardware = reply.observable;
                    }
                } else {
                    reply.hardware = reply.observable;
                }
                break;
            }

            case Tag::standby:
                if (state == StreamDescriptor::State::IDLE  ||
                    state == StreamDescriptor::State::PAUSED) {
                    mLegacyStream->common.standby(&mLegacyStream->common);
                    mState = StreamDescriptor::State::STANDBY;
                    reply.state = StreamDescriptor::State::STANDBY;
                } else {
                    LOG(WARNING) << "StreamOut: 'standby' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::pause:
                if (state == StreamDescriptor::State::ACTIVE) {
                    mState = StreamDescriptor::State::PAUSED;
                    reply.state = StreamDescriptor::State::PAUSED;
                } else if (state == StreamDescriptor::State::DRAINING) {
                    mState = StreamDescriptor::State::DRAIN_PAUSED;
                    reply.state = StreamDescriptor::State::DRAIN_PAUSED;
                } else {
                    LOG(WARNING) << "StreamOut: 'pause' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::drain:
                if (state == StreamDescriptor::State::ACTIVE) {
                    mState = StreamDescriptor::State::IDLE;
                    reply.state = StreamDescriptor::State::IDLE;
                } else {
                    LOG(WARNING) << "StreamOut: 'drain' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::flush:
                if (state == StreamDescriptor::State::PAUSED     ||
                    state == StreamDescriptor::State::DRAIN_PAUSED) {
                    mState = StreamDescriptor::State::IDLE;
                    reply.state = StreamDescriptor::State::IDLE;
                } else {
                    LOG(WARNING) << "StreamOut: 'flush' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            default:
                LOG(WARNING) << "StreamOut: unknown command tag " << (int)cmd.getTag();
                reply.status = STREAM_STATUS_BAD_VALUE;
                break;
        }

        if (!mReplyMQ->writeBlocking(&reply, 1, 500'000'000LL /* 500 ms */)) {
            LOG(WARNING) << "StreamOut: ReplyMQ write timed out";
        }
    }
    LOG(INFO) << "StreamOut worker thread exited";
}

ndk::ScopedAStatus StreamOut::prepareToClose() {
    mStopWorker = true;
    // Worker uses 100 ms readBlocking timeout, so it exits within ~100 ms
    if (mWorker.joinable()) {
        mWorker.join();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOut::close() {
    if (mClosed.exchange(true)) {
        return ndk::ScopedAStatus::ok();
    }
    prepareToClose();
    if (mLegacyDevice && mLegacyStream) {
        LOG(INFO) << "StreamOut::close: closing legacy output stream";
        mLegacyDevice->close_output_stream(mLegacyDevice, mLegacyStream);
        mLegacyStream = nullptr;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOut::getStreamCommon(std::shared_ptr<IStreamCommon>* _aidl_return) {
    *_aidl_return = mStreamCommon;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOut::updateMetadata(const ::aidl::android::hardware::audio::common::SourceMetadata& in_sourceMetadata) {
    (void)in_sourceMetadata;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOut::updateOffloadMetadata(const ::aidl::android::hardware::audio::common::AudioOffloadMetadata& in_offloadMetadata) {
    (void)in_offloadMetadata;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOut::getHwVolume(std::vector<float>* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::setHwVolume(const std::vector<float>& in_channelVolumes) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::getAudioDescriptionMixLevel(float* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::setAudioDescriptionMixLevel(float in_leveldB) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::getDualMonoMode(::aidl::android::media::audio::common::AudioDualMonoMode* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::setDualMonoMode(::aidl::android::media::audio::common::AudioDualMonoMode in_mode) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::getRecommendedLatencyModes(std::vector<::aidl::android::media::audio::common::AudioLatencyMode>* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::setLatencyMode(::aidl::android::media::audio::common::AudioLatencyMode in_mode) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::getPlaybackRateParameters(::aidl::android::media::audio::common::AudioPlaybackRate* _aidl_return) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::setPlaybackRateParameters(const ::aidl::android::media::audio::common::AudioPlaybackRate& in_playbackRate) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamOut::selectPresentation(int32_t in_presentationId, int32_t in_programId) {
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::audio::core
