#pragma once
#include <filesystem>
#include <vector>

#include "ListenerState.h"
#include "PlayRequest.h"

namespace experiment::cooked { class CookedAudioClipSource; }

namespace wave
{
    // 오디오 서비스 계약. **이 헤더에 vendor 타입이 없다.**
    //
    // ★ 표면을 백엔드 API 에서 옮겨 온 것이 아니라 **소비자가 요구하는 것에서 역산**
    //   했다. `SoundComponent` 의 17 필드와 5 메서드, Editor Inspector, C# 12 함수가
    //   실제로 서비스에 요구하는 것은 여섯 가지뿐이다.
    //
    //     ① 값 하나로 재생을 시작하고 손잡이를 받는다
    //     ② 그 손잡이로 정지·일시정지·생존 확인
    //     ③ 재생 중인 것에만 위치·속도를 밀어넣는다
    //     ④ 값 변경(volume/pitch/priority) 반영
    //     ⑤ 계산된 감쇠 이득 반영
    //     ⑥ 클립 목록 조회(에디터 피커)
    //
    //   그 여섯에 듣는 자 갱신과 틱을 더한 것이 아래 전부다.
    //
    // ★★ 보이스 풀이 없다. 옛 배선의 `playOneShotPooled` · `configureVoicePool` ·
    //   `clearVoicePool` · `poolKey` 는 "엔진이 보이스 표를 안 들고 있어서" 필요했던
    //   우회였다 — 클립별 슬롯을 돌려 쓰며 이전 채널을 직접 끊었다. 표와 cap 정책이
    //   있으면 one-shot 은 **손잡이를 보관하지 않는 재생**일 뿐이라 별도 문이 필요 없다.
    class AudioService
    {
    public:
        virtual ~AudioService() = default;

        AudioService(const AudioService&) = delete;
        AudioService& operator=(const AudioService&) = delete;

        // ① 자리가 없으면 무효 핸들을 돌려준다. 보이스 부족은 오류가 아니라 정상 상태다.
        [[nodiscard]] virtual VoiceHandle Play(const PlayRequest& request) = 0;

        // ② 낡은 핸들은 조용히 무시된다 — 부르는 쪽이 생존을 먼저 묻지 않아도 안전하다.
        virtual void Stop(VoiceHandle handle) = 0;
        virtual void SetPaused(VoiceHandle handle, bool paused) = 0;
        [[nodiscard]] virtual bool IsAlive(VoiceHandle handle) const = 0;

        // 소유자별 정지. 컴포넌트 파괴·씬 전환이 쓴다.
        //
        // ★ 옛 배선은 이것을 **버스 다섯 개의 전 채널을 훑어** userData 를 비교해
        //   처리했다. 표가 있으면 표 순회로 끝난다.
        virtual void StopByOwner(std::uint64_t ownerId) = 0;

        // ③ 매 프레임 들어오는 갱신. 최신 값만 의미가 있다.
        virtual void SetVoiceTransform(VoiceHandle handle,
            const math::vector3& position, const math::vector3& velocity) = 0;

        // ④ 값 편집(Inspector · 스크립트)이 살아 있는 보이스에 닿는 유일한 문.
        virtual void SetVoiceParameters(VoiceHandle handle,
            float volume, float pitch, int priority) = 0;

        // ⑤ 엔진이 계산한 감쇠를 반영한다(custom rolloff 등).
        virtual void SetVoiceGain(VoiceHandle handle, float linearGain) = 0;

        // 듣는 자. 세우는 자가 반드시 있어야 3D 가 의미를 갖는다.
        virtual void SetListener(const ListenerState& listener) = 0;

        // 클립 적재는 **명시 호출**이다.
        //
        // ★★★ 옛 배선은 detach 된 스레드가 1초마다 디렉터리를 재귀 순회해 파일 개수가
        //   바뀌면 전부 다시 읽었다. 그 스레드는 종료 수단이 없었고(`while (true)`),
        //   폴더가 없으면 매초 예외를 삼키며 공회전했으며, 소멸자는 그 스레드가 한 번은
        //   적재를 끝내야 통과하는 플래그를 기다려 **폴더가 없으면 영원히 멈췄다**.
        //   적재를 부르는 자가 명시적이면 그 셋이 한꺼번에 사라진다.
        virtual bool LoadClip(const ClipKey& key, const std::filesystem::path& source) = 0;
        // Cooked clips use their manifest GUID. The source owns its mounted
        // byte reader; backends must not turn it into an authoring file path.
        virtual bool LoadCookedClip(
            const experiment::cooked::CookedAudioClipSource& source) = 0;
        virtual void UnloadClip(const ClipKey& key) = 0;

        // ⑥ 에디터 클립 피커 전용. 런타임 경로는 이걸 부르지 않는다.
        [[nodiscard]] virtual std::vector<ClipKey> ListClipKeys() const = 0;

        virtual void Update(float deltaSeconds) = 0;

    protected:
        AudioService() = default;
    };
}
