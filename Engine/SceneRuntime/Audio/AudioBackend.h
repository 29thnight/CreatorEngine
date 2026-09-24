#pragma once
#include <filesystem>

#include "ListenerState.h"
#include "PlayRequest.h"

namespace experiment::cooked { class CookedAudioClipSource; }

namespace wave
{
    // 백엔드가 열 장치의 요청값. 실제로 열린 값은 백엔드가 되돌려 준다.
    struct DeviceSettings final
    {
        std::uint32_t sampleRate{ 48000u };
        std::uint32_t channels{ 2u };
    };

    // 오디오 백엔드 계약.
    //
    // ★ `AudioService` 와 무엇이 다른가. Service 는 **소비자가 요구하는 것**이고
    //   Backend 는 **소리를 내는 장치가 할 수 있는 것**이다. 그 사이에 엔진 정책
    //   (보이스 표 · cap · steal · 가상화 · 감쇠 계산)이 들어간다. 그래서 여기에는
    //   `VoiceHandle` 이 없다 — 백엔드는 자기 자원을 `BackendVoiceId` 로만 가리키고,
    //   논리 보이스와의 짝짓기는 상위가 한다.
    //
    // ★★ 정책을 내려보내지 않는다. priority · maxVoices · steal 은 이 인터페이스에
    //   없다. 옛 배선이 그것을 백엔드에 맡겨(FMOD ChannelGroup 열거 + getAudibility)
    //   판정이 구현에 묶였고, 그래서 백엔드를 바꾸면 정책이 함께 바뀌었다.
    //
    // ★★★ 구현체는 이 헤더를 넘어 vendor 타입을 노출하지 않는다. `ma_*` 든 `FMOD::*`
    //   든 구현 TU 안에서 끝난다 — 그 규약이 지켜지는지는 게이트가 토큰 수로 잰다.
    class AudioBackend
    {
    public:
        virtual ~AudioBackend() = default;

        AudioBackend(const AudioBackend&) = delete;
        AudioBackend& operator=(const AudioBackend&) = delete;

        // ── 장치 수명 ─────────────────────────────────────────────────────
        //
        // 실패는 예외가 아니라 false 다. 장치가 없는 기계에서 에디터 전체가 죽지
        // 않아야 한다 — 그때는 Null 구현으로 명시적으로 degrade 한다.
        [[nodiscard]] virtual bool Start(const DeviceSettings& settings) = 0;
        virtual void Stop() = 0;
        [[nodiscard]] virtual bool IsRunning() const = 0;

        // ── 클립 자원 ─────────────────────────────────────────────────────
        [[nodiscard]] virtual bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) = 0;
        [[nodiscard]] virtual bool LoadCookedClip(const ClipKey& key,
            const experiment::cooked::CookedAudioClipSource& source)
        {
            (void)key;
            (void)source;
            return false;
        }
        virtual void UnloadClip(const ClipKey& key) = 0;
        [[nodiscard]] virtual bool HasClip(const ClipKey& key) const = 0;

        // ── 보이스 ────────────────────────────────────────────────────────
        //
        // 요청을 그대로 받는다. 상위가 이미 정책을 통과시킨 뒤라, 백엔드는
        // "이 소리를 이 값으로 내라" 만 하면 된다. 못 내면 무효 id 를 돌려준다.
        [[nodiscard]] virtual BackendVoiceId StartVoice(const PlayRequest& request) = 0;
        virtual void StopVoice(BackendVoiceId voice) = 0;
        virtual void SetVoicePaused(BackendVoiceId voice, bool paused) = 0;
        [[nodiscard]] virtual bool IsVoicePlaying(BackendVoiceId voice) const = 0;

        virtual void SetVoiceVolume(BackendVoiceId voice, float linearGain) = 0;
        virtual void SetVoicePitch(BackendVoiceId voice, float pitch) = 0;
        virtual void SetVoiceTransform(BackendVoiceId voice,
            const math::vector3& position, const math::vector3& velocity) = 0;

        // ── 믹스 ──────────────────────────────────────────────────────────
        virtual void SetBusVolume(BusId bus, float linearGain) = 0;
        virtual void SetListener(const ListenerState& listener) = 0;

        // 백엔드가 자기 정리(끝난 보이스 회수 등)를 할 기회. 게임 스레드에서 불린다.
        virtual void Update() = 0;

    protected:
        AudioBackend() = default;
    };
}
