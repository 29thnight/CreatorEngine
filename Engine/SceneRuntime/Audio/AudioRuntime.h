#pragma once
#include <unordered_map>
#include <unordered_set>

#include "AudioBackend.h"
#include "AudioService.h"
#include "VoiceTable.h"

namespace wave
{
    // 계약의 구현. 보이스 표와 백엔드 사이에 서서 **정책과 수명**을 소유한다.
    //
    // ★ 백엔드를 생성자에서 참조로 받는다. 만들지 않는다 — 어떤 백엔드를 쓸지는
    //   Host 가 정할 일이고(장치 실패 시 Null 로 degrade 하는 것도 Host 의 판단),
    //   런타임이 그것을 알면 백엔드 선택이 다시 런타임 안으로 들어온다.
    //
    // ★★ 싱글톤이 아니다. 옛 배선은 `SoundManager`·`SoundSystem` 둘 다 전역이라
    //   씬 둘을 동시에 들 수 없었고 테스트가 프로세스 상태를 공유했다. 여기서는
    //   인스턴스를 만들어 쓴다 — 게이트가 한 프로세스에서 여러 개를 세울 수 있는
    //   것이 그 증거다.
    class AudioRuntime final : public AudioService
    {
    public:
        AudioRuntime(AudioBackend& backend, std::size_t voiceCapacity,
            std::uint32_t generationNamespace = 0u);
        ~AudioRuntime() override;

        // Host 가 부른다. 장치를 열지 못하면 false — 그때 Host 가 Null 로 내려간다.
        [[nodiscard]] bool Start(const DeviceSettings& settings);

        // Close producers, retire voices and clip leases, then stop the backend.
        // Backend teardown joins decode workers before releasing mounted sources.
        void Shutdown();

        [[nodiscard]] VoiceHandle Play(const PlayRequest& request) override;
        void Stop(VoiceHandle handle) override;
        void SetPaused(VoiceHandle handle, bool paused) override;
        [[nodiscard]] bool IsAlive(VoiceHandle handle) const override;
        void StopByOwner(std::uint64_t ownerId) override;

        void SetVoiceTransform(VoiceHandle handle,
            const math::vector3& position, const math::vector3& velocity) override;
        void SetVoiceParameters(VoiceHandle handle,
            float volume, float pitch, int priority) override;
        void SetVoiceGain(VoiceHandle handle, float linearGain) override;

        void SetVoiceSettings(VoiceHandle handle, const PlayRequest& request) override;
        void SetLooping(VoiceHandle handle, bool loop) override;
        [[nodiscard]] bool Seek(VoiceHandle handle, std::uint64_t frame) override;
        [[nodiscard]] std::uint64_t GetPlayhead(VoiceHandle handle) const override;
        [[nodiscard]] VoiceState GetVoiceState(VoiceHandle handle) const override;
        void SetBusVolume(BusId bus, float linearGain) override;
        [[nodiscard]] float GetBusVolume(BusId bus) const override;
        void ConfigureBus(BusId bus, std::size_t cap, StealPolicy policy) override;
        void ConfigureConcurrencyGroup(ConcurrencyGroupId group, std::size_t cap,
            StealPolicy policy) override;
        void SetPhysicalVoiceLimit(std::size_t limit) override;
        void SetReverbPreset(ReverbPreset preset) override;
        [[nodiscard]] VoiceMetrics Metrics() const override;
        [[nodiscard]] const std::string& LastError() const override { return m_lastError; }
        void SetListener(const ListenerState& listener) override;

        [[nodiscard]] static float SampleAttenuation(const PlayRequest& request,
            const ListenerState& listener) noexcept;
        [[nodiscard]] static float SampleDoppler(const PlayRequest& request,
            const ListenerState& listener) noexcept;

        [[nodiscard]] bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) override;
        [[nodiscard]] bool LoadCookedClip(
            const experiment::cooked::CookedAudioClipSource& source) override;
        void UnloadClip(const ClipKey& key) override;
        [[nodiscard]] std::vector<ClipKey> ListClipKeys() const override;

        void Update(float deltaSeconds) override;

        // 정책 관찰. 게이트와 프로파일러가 읽는다.
        [[nodiscard]] std::size_t AliveVoiceCount() const noexcept { return m_voices.AliveCount(); }
        [[nodiscard]] std::size_t VoiceCapacity() const noexcept { return m_voices.Capacity(); }
        [[nodiscard]] std::uint64_t FrameIndex() const noexcept { return m_frame; }

    private:
        // 백엔드에 현재 이득을 밀어 넣는다(기본 이득 × 감쇠).
        struct Limit final
        {
            std::size_t cap{ 0u };
            StealPolicy policy{ StealPolicy::Reject };
        };

        void PushGain(const VoiceRecord& record);
        void RefreshGain(VoiceRecord& record);
        void Virtualize(VoiceRecord& record);
        [[nodiscard]] bool MakePhysical(VoiceHandle handle, VoiceRecord& record);
        [[nodiscard]] VoiceHandle FindVictim(BusId bus, ConcurrencyGroupId group,
            StealPolicy policy, bool physicalOnly, VoiceHandle excluded = {}) const;
        [[nodiscard]] bool CanDisplace(const VoiceRecord& incoming,
            const VoiceRecord& victim, StealPolicy policy) const;
        [[nodiscard]] std::size_t PhysicalCount(BusId bus = {}) const;
        void EnforceLimits();

        AudioBackend& m_backend;
        VoiceTable m_voices;

        // 클립 목록의 정본은 런타임이 든다 — 백엔드마다 "무엇을 적재했는가" 를
        // 되물을 수 있어야 할 이유가 없고, 에디터 피커는 백엔드와 무관해야 한다.
        std::unordered_set<ClipKey> m_clips;

        std::unordered_map<std::uint16_t, Limit> m_busLimits;
        std::unordered_map<std::uint32_t, Limit> m_concurrencyLimits;
        std::unordered_map<std::uint16_t, float> m_busVolumes;
        ListenerState m_listener{};
        std::size_t m_physicalLimit{ 0u };
        StealPolicy m_masterPolicy{ StealPolicy::LowestPriority };
        bool m_masterPolicyConfigured{ false };
        VoiceMetrics m_metrics{};
        std::string m_lastError;
        std::uint64_t m_frame{ 0 };
        bool m_started{ false };
    };
}
