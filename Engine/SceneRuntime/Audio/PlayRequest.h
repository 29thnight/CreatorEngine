#pragma once
#include <mathematics/vector3.hpp>

#include "AudioValues.h"

namespace wave
{
    // 재생 한 번에 필요한 입력 전부를 담은 값.
    //
    // ★ 왜 값 하나로 받는가. 옛 배선은 하위 계층이 상위 타입을 통째로 받아
    //   (`SoundManager::playFromSourceBlended(const SoundComponent&)`) 컴포넌트의
    //   열 필드를 직접 읽었다. 계층이 뒤집혀 있어서 오디오를 컴포넌트 없이 검증할
    //   수 없었고, 컴포넌트 필드 하나가 바뀔 때마다 하위 계층이 흔들렸다.
    //
    //   요청을 값으로 받으면 서비스는 컴포넌트를 모른다. 에디터 프리뷰도, 스크립트
    //   호출도, 테스트 fixture 도 같은 문으로 들어온다.
    struct PlayRequest final
    {
        ClipKey clip;
        BusId bus{};

        float volume{ 1.0f };
        float pitch{ 1.0f };
        int priority{ 128 };
        bool loop{ false };

        // 0 = 2D, 1 = 3D. 중간값은 equal-power 로 섞는다.
        //
        // ★ 구현이 백엔드 소스 둘을 쓰더라도 **논리 보이스는 하나**다. cap 과 steal
        //   판정이 채널 수를 세면 blend 중간값 한 소리가 둘로 계산돼 정책이 왜곡된다.
        float spatialBlend{ 0.0f };

        math::vector3 position{ 0.0f, 0.0f, 0.0f };
        math::vector3 velocity{ 0.0f, 0.0f, 0.0f };
        float minimumDistance{ 1.0f };
        float maximumDistance{ 50.0f };
        RolloffKind rolloff{ RolloffKind::Inverse };

        // ★ 리버브를 요청에 싣는 이유. 옛 배선에서 리버브는 Inspector 람다만이
        //   채널에 썼고 `SoundComponent::Play()` 는 그 값을 읽지 않아, **재생할
        //   때마다 설정이 소실**됐다. 재생 입력에 들어 있으면 그 결함이 구조적으로
        //   생길 수 없다.
        bool useReverbSend{ false };
        float reverbSendDecibels{ 0.0f };

        // 누가 낸 소리인지. 컴포넌트 파괴·씬 전환에서 자기 것만 골라 멈추는 데 쓴다.
        //
        // ★ 옛 배선은 `void*` 를 백엔드 채널의 userData 에 넣고 **버스 다섯 개의 전
        //   채널을 훑어** 비교했다. 표를 엔진이 들면 소유자별 정지는 표 순회로 끝난다.
        std::uint64_t ownerId{ 0 };
    };
}
