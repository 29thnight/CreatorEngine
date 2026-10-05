#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "AudioBackend.h"

namespace wave
{
    struct AudioCallbackMetrics final
    {
        std::uint64_t count{ 0 };
        std::uint64_t overHalfPeriod{ 0 };
        std::uint64_t overFullPeriod{ 0 };
        std::uint64_t meanNanoseconds{ 0 };
        std::uint64_t p99UpperNanoseconds{ 0 };
        std::uint64_t maxNanoseconds{ 0 };
        std::uint32_t minimumFrames{ 0 };
        std::uint32_t maximumFrames{ 0 };
    };

    struct AudioDeviceDiagnostics final
    {
        std::string backend;
        std::string deviceName;
        std::uint32_t periodFrames{ 0 };
        std::uint32_t bufferFrames{ 0 };
        bool outputInterrupted{ false };
        std::uint64_t rerouteCount{ 0u };
        std::uint64_t restartAttempts{ 0u };
        std::uint64_t successfulRestarts{ 0u };
    };

    // The backend owns the device, bounded decode workers, immutable mounted
    // sources, equal-power source pairs, and the private room-reverb node.
    class MiniaudioBackend final : public AudioBackend
    {
    public:
        explicit MiniaudioBackend(bool profileCallbacks = false);
        ~MiniaudioBackend() override;

        [[nodiscard]] bool Start(const DeviceSettings& settings) override;
        void Stop() override;
        [[nodiscard]] bool IsRunning() const override;
        [[nodiscard]] bool IsOutputAvailable() const override;

        [[nodiscard]] bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) override;
        [[nodiscard]] bool LoadCookedClip(const ClipKey& key,
            const experiment::cooked::CookedAudioClipSource& source) override;
        [[nodiscard]] BackendClipId RetainClip(const ClipKey& key) override;
        void ReleaseClip(BackendClipId clip) override;
        void UnloadClip(const ClipKey& key) override;
        [[nodiscard]] bool HasClip(const ClipKey& key) const override;

        [[nodiscard]] BackendVoiceId StartVoice(const PlayRequest& request) override;
        [[nodiscard]] BackendVoiceId StartVoice(const PlayRequest& request, BackendClipId clip) override;
        void StopVoice(BackendVoiceId voice) override;
        void SetVoicePaused(BackendVoiceId voice, bool paused) override;
        [[nodiscard]] bool IsVoicePlaying(BackendVoiceId voice) const override;

        void SetVoiceVolume(BackendVoiceId voice, float linearGain) override;
        void SetVoicePitch(BackendVoiceId voice, float pitch) override;
        void SetVoiceTransform(BackendVoiceId voice,
            const math::vector3& position, const math::vector3& velocity) override;

        void SetVoiceSettings(BackendVoiceId voice, const PlayRequest& request) override;
        void SetVoiceLooping(BackendVoiceId voice, bool loop) override;
        [[nodiscard]] bool SeekVoice(BackendVoiceId voice, std::uint64_t frame) override;
        [[nodiscard]] std::uint64_t VoicePlayhead(BackendVoiceId voice) const override;
        [[nodiscard]] ClipInfo GetClipInfo(const ClipKey& key) const override;
        void SetReverbPreset(ReverbPreset preset) override;
        void SetBusVolume(BusId bus, float linearGain) override;
        void SetListener(const ListenerState& listener) override;

        void Update() override;
        // Safe main-thread recovery request. The graph and voice IDs survive.
        void RequestDeviceRestart();

        // 마지막 실패 사유. 벤더 오류 코드를 그대로 흘리지 않고 문장으로 번역해 둔다.
        //
        // ★ 실패를 로그로만 흘리면 부르는 쪽이 "왜 안 되는지" 를 값으로 받을 수 없다.
        //   장치 없음과 클립 손상은 다른 처분이 필요하다.
        [[nodiscard]] const std::string& LastError() const noexcept;

        // 실제로 열린 장치 설정. 요청값과 다를 수 있다.
        [[nodiscard]] DeviceSettings ActualSettings() const noexcept;

        // Opt-in local profiling. Callback counters use fixed atomic storage;
        // the audio thread does not allocate, log, or take a lock here.
        [[nodiscard]] AudioCallbackMetrics CallbackMetrics() const noexcept;
        [[nodiscard]] AudioDeviceDiagnostics DeviceDiagnostics() const;

        // Offline capture is valid only when Start selected noDevice explicitly.
        [[nodiscard]] bool Render(float* interleaved, std::uint32_t frames);
        [[nodiscard]] std::uint64_t StreamReadFailures() const noexcept;
        [[nodiscard]] std::uint64_t StreamBytesRead() const noexcept;

    private:
        struct Implementation;
        std::unique_ptr<Implementation> m_implementation;
    };
}
