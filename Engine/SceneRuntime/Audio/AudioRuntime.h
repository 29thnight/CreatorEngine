#pragma once
#include <unordered_map>

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
        AudioRuntime(AudioBackend& backend, std::size_t voiceCapacity);
        ~AudioRuntime() override;

        // Host 가 부른다. 장치를 열지 못하면 false — 그때 Host 가 Null 로 내려간다.
        [[nodiscard]] bool Start(const DeviceSettings& settings);

        // ★ 종료 순서를 여기서 못 박는다: 보이스 정지 → 클립 해제 → 장치 정지.
        //   옛 배선의 종료는 적재 스레드의 플래그를 기다리다 멈췄고, 그 스레드는
        //   이미 해제된 객체를 다시 만질 수 있었다. 기다릴 스레드가 없으면
        //   순서만 지키면 된다.
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

        void SetListener(const ListenerState& listener) override;

        [[nodiscard]] bool LoadClip(const ClipKey& key,
            const std::filesystem::path& source) override;
        void UnloadClip(const ClipKey& key) override;
        [[nodiscard]] std::vector<ClipKey> ListClipKeys() const override;

        void Update(float deltaSeconds) override;

        // 정책 관찰. 게이트와 프로파일러가 읽는다.
        [[nodiscard]] std::size_t AliveVoiceCount() const noexcept { return m_voices.AliveCount(); }
        [[nodiscard]] std::size_t VoiceCapacity() const noexcept { return m_voices.Capacity(); }
        [[nodiscard]] std::uint64_t FrameIndex() const noexcept { return m_frame; }

    private:
        // 백엔드에 현재 이득을 밀어 넣는다(기본 이득 × 감쇠).
        void PushGain(const VoiceRecord& record);

        AudioBackend& m_backend;
        VoiceTable m_voices;

        // 클립 목록의 정본은 런타임이 든다 — 백엔드마다 "무엇을 적재했는가" 를
        // 되물을 수 있어야 할 이유가 없고, 에디터 피커는 백엔드와 무관해야 한다.
        std::unordered_map<ClipKey, std::filesystem::path> m_clips;

        std::uint64_t m_frame{ 0 };
        bool m_started{ false };
    };
}
