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

#define LOG_TAG "audio_hw_aidl_stream_in"

#include <android-base/logging.h>
#include <time.h>
#include <algorithm>

#include "StreamIn.h"
#include <sys/resource.h>
#include <pthread.h>
#include <vector>

// Binder status codes used in StreamDescriptor::Reply::status
static constexpr int32_t STREAM_STATUS_OK               =   0;
static constexpr int32_t STREAM_STATUS_INVALID_OPERATION = -38;
static constexpr int32_t STREAM_STATUS_BAD_VALUE         = -22;

namespace aidl::android::hardware::audio::core {

StreamIn::StreamIn(audio_hw_device_t* device, struct audio_stream_in* legacyStream)
    : mLegacyDevice(device), mLegacyStream(legacyStream) {
    mStreamCommon = ndk::SharedRefBase::make<StreamCommon>(
            [this]() { return this->close(); },
            [this]() { return this->prepareToClose(); });
    LOG(INFO) << "Created StreamIn wrapper for legacy stream: " << legacyStream;
}

StreamIn::~StreamIn() {
    if (mStreamCommon) {
        mStreamCommon->detach();
    }
    close();
}

ndk::ScopedAStatus StreamIn::init(StreamDescriptor* desc) {
    if (!mLegacyStream) return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);

    mFrameSize = audio_stream_in_frame_size(mLegacyStream);
    if (mFrameSize == 0) mFrameSize = 4;

    size_t bufferSize = mLegacyStream->common.get_buffer_size(&mLegacyStream->common);
    size_t frameCount = bufferSize / mFrameSize;

    // Create FMQs (Command, Reply, Data)
    mCommandMQ = std::make_unique<CommandMQ>(1, true /* configureEventFlagWord */);
    mReplyMQ   = std::make_unique<ReplyMQ>  (1, true /* configureEventFlagWord */);

    // DataMQ holds 4× buffer size for headroom
    size_t dataMqSize = bufferSize * 4;
    mDataMQ = std::make_unique<DataMQ>(dataMqSize, true /* configureEventFlagWord */);

    if (!mCommandMQ->isValid() || !mReplyMQ->isValid() || !mDataMQ->isValid()) {
        LOG(ERROR) << "StreamIn::init: failed to create FMQs";
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
    }

    desc->command          = mCommandMQ->dupeDesc();
    desc->reply            = mReplyMQ->dupeDesc();
    desc->audio.set<StreamDescriptor::AudioBuffer::Tag::fmq>(mDataMQ->dupeDesc());
    desc->frameSizeBytes   = (int32_t)mFrameSize;
    desc->bufferSizeFrames = (int32_t)frameCount;

    mStopWorker = false;
    mState      = StreamDescriptor::State::STANDBY;
    mFramesRead = 0;
    mWorker     = std::thread(&StreamIn::workerThread, this);

    LOG(INFO) << "StreamIn::init: frameSize=" << mFrameSize
              << " bufferFrames=" << frameCount
              << " dataMqBytes=" << dataMqSize;
    return ndk::ScopedAStatus::ok();
}

// ---------------------------------------------------------------------------
// Worker thread – AIDL input stream state machine
// ---------------------------------------------------------------------------
// State transitions:
//   STANDBY + start   -> IDLE
//   IDLE    + standby -> STANDBY
//   IDLE    + burst   -> ACTIVE   (begin reading from hardware)
//   ACTIVE  + burst   -> ACTIVE   (continue reading)
//   ACTIVE  + standby -> STANDBY
//   ACTIVE  + pause   -> PAUSED
//   ACTIVE  + flush   -> IDLE     (discard buffered data)
//   PAUSED  + burst   -> PAUSED
//   PAUSED  + start   -> ACTIVE
//   PAUSED  + flush   -> IDLE
//   PAUSED  + standby -> STANDBY
//   ANY     + getStatus -> current state (no side effects)
// ---------------------------------------------------------------------------
void StreamIn::workerThread() {
    LOG(INFO) << "StreamIn worker thread started (state=STANDBY)";
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
        if (!mCommandMQ->readBlocking(&cmd, 1, 100'000'000LL /* 100 ms */)) {
            continue;
        }

        const StreamDescriptor::State state = mState.load();
        StreamDescriptor::Reply reply{};
        reply.status            = STREAM_STATUS_OK;
        reply.state             = state;
        reply.observable.frames = mFramesRead;
        reply.observable.timeNs = nowNs();
        reply.hardware          = reply.observable;

        using Tag = StreamDescriptor::Command::Tag;

        switch (cmd.getTag()) {

            case Tag::getStatus:
                // No state change
                break;

            case Tag::start:
                if (state == StreamDescriptor::State::STANDBY) {
                    mState = StreamDescriptor::State::IDLE;
                    reply.state = StreamDescriptor::State::IDLE;
                } else if (state == StreamDescriptor::State::PAUSED) {
                    mState = StreamDescriptor::State::ACTIVE;
                    reply.state = StreamDescriptor::State::ACTIVE;
                } else {
                    LOG(WARNING) << "StreamIn: 'start' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::burst: {
                const int32_t burstBytes = cmd.get<Tag::burst>();

                if (state == StreamDescriptor::State::IDLE  ||
                    state == StreamDescriptor::State::ACTIVE) {
                    // Read from hardware and push to DataMQ
                    if (burstBytes > 0) {
                        buffer.resize((size_t)burstBytes);
                        ssize_t bytesRead = mLegacyStream->read(
                                mLegacyStream, buffer.data(), (size_t)burstBytes);
                        if (bytesRead > 0) {
                            if (mDataMQ->write(buffer.data(), (size_t)bytesRead)) {
                                mFramesRead += (int64_t)(bytesRead / (ssize_t)mFrameSize);
                                reply.fmqByteCount = (int32_t)bytesRead;
                            } else {
                                LOG(WARNING) << "StreamIn: DataMQ write failed (full?)";
                                reply.fmqByteCount = 0;
                            }
                        } else if (bytesRead < 0) {
                            LOG(WARNING) << "StreamIn: legacy read() = " << bytesRead;
                        }
                    }
                    mState = StreamDescriptor::State::ACTIVE;
                    reply.state = StreamDescriptor::State::ACTIVE;

                } else if (state == StreamDescriptor::State::PAUSED) {
                    // Don't read; stay PAUSED.
                    reply.fmqByteCount = 0;
                    reply.state = StreamDescriptor::State::PAUSED;

                } else {
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                reply.observable.frames = mFramesRead;
                reply.observable.timeNs = nowNs();
                reply.hardware = reply.observable;
                break;
            }

            case Tag::standby:
                if (state == StreamDescriptor::State::IDLE   ||
                    state == StreamDescriptor::State::ACTIVE  ||
                    state == StreamDescriptor::State::PAUSED) {
                    mLegacyStream->common.standby(&mLegacyStream->common);
                    mState = StreamDescriptor::State::STANDBY;
                    reply.state = StreamDescriptor::State::STANDBY;
                } else {
                    LOG(WARNING) << "StreamIn: 'standby' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::pause:
                if (state == StreamDescriptor::State::ACTIVE) {
                    mState = StreamDescriptor::State::PAUSED;
                    reply.state = StreamDescriptor::State::PAUSED;
                } else {
                    LOG(WARNING) << "StreamIn: 'pause' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            case Tag::flush:
                if (state == StreamDescriptor::State::ACTIVE ||
                    state == StreamDescriptor::State::PAUSED) {
                    mState = StreamDescriptor::State::IDLE;
                    reply.state = StreamDescriptor::State::IDLE;
                } else {
                    LOG(WARNING) << "StreamIn: 'flush' invalid in state " << (int)state;
                    reply.status = STREAM_STATUS_INVALID_OPERATION;
                }
                break;

            default:
                LOG(WARNING) << "StreamIn: unknown command tag " << (int)cmd.getTag();
                reply.status = STREAM_STATUS_BAD_VALUE;
                break;
        }

        if (!mReplyMQ->writeBlocking(&reply, 1, 500'000'000LL /* 500 ms */)) {
            LOG(WARNING) << "StreamIn: ReplyMQ write timed out";
        }
    }
    LOG(INFO) << "StreamIn worker thread exited";
}

ndk::ScopedAStatus StreamIn::prepareToClose() {
    mStopWorker = true;
    if (mWorker.joinable()) mWorker.join();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamIn::close() {
    if (mClosed.exchange(true)) {
        return ndk::ScopedAStatus::ok();
    }
    prepareToClose();
    if (mLegacyDevice && mLegacyStream) {
        LOG(INFO) << "StreamIn::close: closing legacy input stream";
        mLegacyDevice->close_input_stream(mLegacyDevice, mLegacyStream);
        mLegacyStream = nullptr;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamIn::getStreamCommon(std::shared_ptr<IStreamCommon>* _aidl_return) {
    *_aidl_return = mStreamCommon;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamIn::getActiveMicrophones(std::vector<::aidl::android::media::audio::common::MicrophoneDynamicInfo>* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamIn::getMicrophoneDirection(::aidl::android::hardware::audio::core::IStreamIn::MicrophoneDirection* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamIn::setMicrophoneDirection(::aidl::android::hardware::audio::core::IStreamIn::MicrophoneDirection in_direction) {
    (void)in_direction;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamIn::getMicrophoneFieldDimension(float* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamIn::setMicrophoneFieldDimension(float in_zoom) {
    (void)in_zoom;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamIn::updateMetadata(const ::aidl::android::hardware::audio::common::SinkMetadata& in_sinkMetadata) {
    (void)in_sinkMetadata;
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamIn::getHwGain(std::vector<float>* _aidl_return) {
    (void)_aidl_return;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

ndk::ScopedAStatus StreamIn::setHwGain(const std::vector<float>& in_channelGains) {
    (void)in_channelGains;
    return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
}

}  // namespace aidl::android::hardware::audio::core
