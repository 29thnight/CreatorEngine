#pragma once
#include <mathematics/vector3.hpp>

namespace wave
{
    // 듣는 자의 자세. 3D 감쇠와 정위가 전부 이 값을 기준으로 계산된다.
    //
    // ★ 옛 배선에는 이 값을 **세우는 호출부가 하나도 없었다**(`setListenerAttributes`
    //   호출부 0). 그런데 `SoundComponent` 는 반대편 `getListenerPosition` 을 읽어
    //   custom rolloff 감쇠를 계산하고 있었다 — 즉 항상 원점에 선 듣는 자를 기준으로
    //   거리를 재고 있었다. 계약에 넣어 두면 "세우는 자가 없다" 가 컴파일 시점이나
    //   게이트에서 드러난다.
    struct ListenerState final
    {
        math::vector3 position{ 0.0f, 0.0f, 0.0f };
        math::vector3 velocity{ 0.0f, 0.0f, 0.0f };
        math::vector3 forward{ 0.0f, 0.0f, 1.0f };
        math::vector3 up{ 0.0f, 1.0f, 0.0f };
    };
}
