#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Mathf.h"

// 한 프레임 분량의 카메라 렌더 입력 (PHASE 3-2).
//
// 게임 스레드가 프레임을 밀봉할 때 통째로 채우고, 렌더 스레드는 통째로 읽는다.
// 개별 필드를 따로 갱신하지 않는 것이 핵심이다 — 절반만 갱신된 상태를 렌더가
// 보는 일이 구조적으로 불가능해야 배리어 없이도 성립한다.
//
// 여기 없는 카메라 값을 렌더 스레드가 읽고 있다면 그건 아직 이식되지 않은
// 경로다. 새로 필요해지면 이 구조체에 추가하고 호출부를 스냅샷으로 돌린다.
struct FrameCameraSnapshot
{
    Mathf::xMatrix view{};
    Mathf::xMatrix projection{};

    // 역행렬을 미리 담는 이유는 호출부가 XMMatrixInverse를 다시 부르지 않게
    // 하기 위해서다 — 같은 값을 여러 패스가 매 프레임 각자 다시 구하고 있었다.
    Mathf::xMatrix inverseView{};
    Mathf::xMatrix inverseProjection{};

    Mathf::xVector eyePosition{};
    Mathf::xVector forward{};
    Mathf::xVector right{};
    Mathf::xVector up{};

    float fov{ 0.f };
    float nearPlane{ 0.f };
    float farPlane{ 0.f };
    bool  isOrthographic{ false };
};

#endif // !DYNAMICCPP_EXPORTS
