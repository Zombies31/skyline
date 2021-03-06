// SPDX-License-Identifier: MPL-2.0
// Copyright © 2020 Skyline Team and Contributors (https://github.com/skyline-emu/)

#include <kernel/types/KProcess.h>
#include "IAudioOut.h"

namespace skyline::service::audio {
    IAudioOut::IAudioOut(const DeviceState &state, ServiceManager &manager, u8 channelCount, u32 sampleRate) : sampleRate(sampleRate), channelCount(channelCount), releaseEvent(std::make_shared<type::KEvent>(state)), BaseService(state, manager) {
        track = state.audio->OpenTrack(channelCount, constant::SampleRate, [this]() { this->releaseEvent->Signal(); });
    }

    IAudioOut::~IAudioOut() {
        state.audio->CloseTrack(track);
    }

    Result IAudioOut::GetAudioOutState(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        response.Push(static_cast<u32>(track->playbackState));
        return {};
    }

    Result IAudioOut::StartAudioOut(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        state.logger->Debug("Start playback");
        track->Start();
        return {};
    }

    Result IAudioOut::StopAudioOut(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        state.logger->Debug("Stop playback");
        track->Stop();
        return {};
    }

    Result IAudioOut::AppendAudioOutBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        struct Data {
            u64 nextBufferPtr;
            u64 sampleBufferPtr;
            u64 sampleCapacity;
            u64 sampleSize;
            u64 sampleOffset;
        } &data{request.inputBuf.at(0).as<Data>()};
        auto tag{request.Pop<u64>()};

        state.logger->Debug("Appending buffer with address: 0x{:X}, size: 0x{:X}", data.sampleBufferPtr, data.sampleSize);

        if (sampleRate != constant::SampleRate) {
            auto resampledBuffer{resampler.ResampleBuffer(span(state.process->GetPointer<i16>(data.sampleBufferPtr), data.sampleSize / sizeof(i16)), static_cast<double>(sampleRate) / constant::SampleRate, channelCount)};
            track->AppendBuffer(tag, resampledBuffer);
        } else {
            track->AppendBuffer(tag, span(state.process->GetPointer<i16>(data.sampleBufferPtr), data.sampleSize / sizeof(i16)));
        }

        return {};
    }

    Result IAudioOut::RegisterBufferEvent(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto handle{state.process->InsertItem(releaseEvent)};
        state.logger->Debug("Buffer Release Event Handle: 0x{:X}", handle);
        response.copyHandles.push_back(handle);
        return {};
    }

    Result IAudioOut::GetReleasedAudioOutBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto maxCount{static_cast<u32>(request.outputBuf.at(0).size() >> 3)};
        std::vector<u64> releasedBuffers{track->GetReleasedBuffers(maxCount)};
        auto count{static_cast<u32>(releasedBuffers.size())};

        // Fill rest of output buffer with zeros
        releasedBuffers.resize(maxCount, 0);
        request.outputBuf.at(0).copy_from(releasedBuffers);

        response.Push<u32>(count);
        return {};
    }

    Result IAudioOut::ContainsAudioOutBuffer(type::KSession &session, ipc::IpcRequest &request, ipc::IpcResponse &response) {
        auto tag{request.Pop<u64>()};

        response.Push(static_cast<u32>(track->ContainsBuffer(tag)));
        return {};
    }
}
