#pragma once
#include <deque>
#include <unordered_map>
#include <vector>

#include "AudioBackend.h"

namespace wave
{
    // 소리를 내지 않는 백엔드. 장치 없이 결정적으로 도는 구현이다.
    //
    // ★ 두 가지를 위해 있다.
    //   ① **degrade 경로.** 오디오 장치가 없는 기계에서 에디터가 통째로 실패하면
    //      안 된다. 장치 열기가 실패하면 여기로 내려와 재생 호출이 전부 성립하되
    //      소리만 없다 — 상위 계층은 분기하지 않는다.
    //   ② **판정 경로.** 보이스 수명·정책·핸들 규약은 소리와 무관한 계약이다.
    //      Null 로 재면 장치·믹서·타이밍이 판정에서 빠져 재현이 흔들리지 않는다.
    //
    // ★★ "아무것도 안 하는 구현" 이 아니다. 상태를 실제로 들고 있어야 상위 계층의
    //   결함이 드러난다 — 정지한 보이스가 계속 playing 으로 보이거나, 없는 클립이
    //   재생되는 것을 Null 이 잡아 준다.
    class NullAudioBackend final : public AudioBackend
    {
    public:
        NullAudioBackend() = default;

        [[nodiscard]] bool Start(const DeviceSettings& settings) override;
        void Stop() override;
        [[nodiscard]] bool IsRunning() const override { return m_running; }

        [[nodiscard]] bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) override;
        void UnloadClip(const ClipKey& key) override;
        [[nodiscard]] bool HasClip(const ClipKey& key) const override;

        [[nodiscard]] BackendVoiceId StartVoice(const PlayRequest& request) override;
        void StopVoice(BackendVoiceId voice) override;
        void SetVoicePaused(BackendVoiceId voice, bool paused) override;
        [[nodiscard]] bool IsVoicePlaying(BackendVoiceId voice) const override;

        void SetVoiceVolume(BackendVoiceId voice, float linearGain) override;
        void SetVoicePitch(BackendVoiceId voice, float pitch) override;
        void SetVoiceTransform(BackendVoiceId voice,
            const math::vector3& position, const math::vector3& velocity) override;

        void SetBusVolume(BusId bus, float linearGain) override;
        void SetListener(const ListenerState& listener) override;

        void Update() override;

        // ── 판정용 관찰 창구 ──────────────────────────────────────────────
        //
        // 게이트가 "백엔드가 실제로 그 값을 받았는가" 를 묻는다. 제품 경로는 부르지
        // 않는다 — 상위 계층이 백엔드 상태를 읽어 판단하기 시작하면 정본이 다시
        // 백엔드로 넘어간다.
        [[nodiscard]] std::size_t PlayingCount() const noexcept;

        // StartVoice 가 몇 번 불렸는지. 성공·실패를 가리지 않고 센다.
        //
        // ★ 왜 필요한가. "없는 클립은 무효 핸들" 만 단정하면 그 절을 **런타임이
        //   막았는지 백엔드가 막았는지** 구분하지 못한다. 실제로 런타임의 가드를
        //   변이로 걷었을 때 Null 이 대신 막아 단정이 통과했다(거짓 초록). 요청이
        //   백엔드에 닿았는지를 세면 그 자리를 정확히 못 박는다.
        [[nodiscard]] std::size_t StartVoiceAttempts() const noexcept { return m_startAttempts; }
        [[nodiscard]] float VoiceVolume(BackendVoiceId voice) const noexcept;
        [[nodiscard]] float VoicePitch(BackendVoiceId voice) const noexcept;
        [[nodiscard]] const ListenerState& Listener() const noexcept { return m_listener; }
        [[nodiscard]] const DeviceSettings& Settings() const noexcept { return m_settings; }

    private:
        struct Voice final
        {
            ClipKey clip;
            BusId bus{};
            float volume{ 1.0f };
            float pitch{ 1.0f };
            math::vector3 position{ 0.0f, 0.0f, 0.0f };
            math::vector3 velocity{ 0.0f, 0.0f, 0.0f };
            bool paused{ false };
            bool playing{ false };
        };

        [[nodiscard]] Voice* Resolve(BackendVoiceId voice) noexcept;
        [[nodiscard]] const Voice* Resolve(BackendVoiceId voice) const noexcept;

        bool m_running{ false };
        DeviceSettings m_settings{};
        ListenerState m_listener{};

        std::unordered_map<ClipKey, std::filesystem::path> m_clips;
        std::unordered_map<std::uint16_t, float> m_busVolumes;

        // id 는 1부터 매긴다 — 0 은 언제나 무효다.
        std::deque<Voice> m_voices;
        std::size_t m_startAttempts{ 0 };
    };
}
