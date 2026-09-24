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
    };

    // miniaudio 백엔드. **이 헤더에 `ma_*` 토큰이 하나도 없다** — 구현은 전부
    // `MiniaudioBackend.cpp` 안에서 끝난다(ThirdParty/miniaudio/PROVENANCE.md 의 통합 규약).
    //
    // ★ 왜 miniaudio 인가. 계획서 §9 가 대안들을 기각한 근거는 그대로다 — 자체
    //   device/decoder 구현은 WASAPI·리샘플러·device-loss 검증 범위가 외부 종속
    //   절감 이익을 압도하고, DLL 배포는 ABI 보장이 없다. 소스 한 벌을 고정한 태그로
    //   들고 구현 TU 하나만 컴파일한다.
    //
    // ★★ FMOD 백엔드는 이 계약 뒤에 **자리만 남긴다.** 같은 인터페이스를 구현하면
    //   되고, 그 구현은 이 슬라이스의 범위가 아니다.
    //
    // ── 이 슬라이스가 구현하지 않은 것 ───────────────────────────────────
    //
    // - **spatialBlend 의 중간값.** 지금은 0 보다 크면 공간화를 켠다. 계획서 §4.2 의
    //   equal-power 2D/3D 쌍(논리 보이스 하나 아래 소스 둘)은 AU5 소관이다. 중간값을
    //   넣어도 오류는 아니지만 **섞이지 않는다** — 게이트도 그것을 단정하지 않는다.
    // - **cap·steal·가상화.** 정책은 상위(`AudioRuntime`)가 갖는다.
    // - **리버브 send.** 요청에는 값이 실려 오지만 여기서 소비하지 않는다(AU6).
    class MiniaudioBackend final : public AudioBackend
    {
    public:
        explicit MiniaudioBackend(bool profileCallbacks = false);
        ~MiniaudioBackend() override;

        [[nodiscard]] bool Start(const DeviceSettings& settings) override;
        void Stop() override;
        [[nodiscard]] bool IsRunning() const override;

        [[nodiscard]] bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) override;
        [[nodiscard]] bool LoadCookedClip(const ClipKey& key,
            const experiment::cooked::CookedAudioClipSource& source) override;
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

    private:
        struct Implementation;
        std::unique_ptr<Implementation> m_implementation;
    };
}
