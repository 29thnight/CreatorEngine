#pragma once
// reflgen 다리 카나리아 (reflgen 도입 파일럿) — 다리(ReflgenBridge.h)가 옮기는 모양을 한 타입에 모았다:
// reflgen 속성(range·display_name·hidden·readonly), 엔진 속성(creator::…), 빠지는 필드(ignore), 파라미터가 있는
// 메서드와 없는 메서드. ReflgenBridgeSelfTest.cpp 가 같은 타입을 손으로 쓴 레시피와 대조한다. 엔진 기능은 이
// 타입을 쓰지 않는다.
namespace reflgen_bridge_canary
{
    struct [[reflgen::reflect]] Canary
    {
        int m_count = 0;
        [[reflgen::range(0.0f, 1.0f), reflgen::display_name("Ranged")]] float m_ranged = 0.5f;
        [[creator::units("m"), creator::wide]] float m_distance = 1.0f;
        [[reflgen::hidden]] int m_hidden = 0;
        [[reflgen::readonly, creator::debug_only]] int m_instanceID = 0;
        [[reflgen::ignore]] int m_cache = 0;

        [[reflgen::reflect]] void Fire(int shots) { m_count = shots; }
        [[reflgen::reflect]] bool IsEmpty() { return m_count == 0; }
    };
}
