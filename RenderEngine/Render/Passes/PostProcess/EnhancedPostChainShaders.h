#pragma once
#ifndef DYNAMICCPP_EXPORTS

// 포스트 체인 셰이더 (PHASE 3-6).
//
// 헤더로 뺀 이유는 SSGI와 같다 — 셰이더가 길어지면 C++ 로직이 그 사이에
// 파묻혀서, 무엇이 배선이고 무엇이 계산인지 읽어 내기 어려워진다.
namespace PostChainShaders
{
    // 모든 패스가 같은 상수 버퍼를 쓴다. 필드를 나눠 두면 패스마다 구조체가
    // 생기고, 그러면 '어느 패스가 어느 크기를 봤는가'가 갈릴 자리가 는다 —
    // SSGI에서 실제로 필터가 다른 해상도를 본 적이 있다.

    // ── 블룸 임계 ──
    //
    // 임계를 넘는 부분만 남긴다. 딱 잘라내면 경계에서 계단이 보이므로,
    // knee 구간에서 이차식으로 부드럽게 올린다(업계 통용 방식).
    constexpr const char* kThresholdFile = "PostChainThreshold.hlsl";

    // ── 다운샘플 ──
    //
    // 13탭 가중 평균(Call of Duty 발표에서 널리 퍼진 배치). 단순 2x2로
    // 내려가면 단수를 거듭할수록 격자 무늬가 생긴다 — 표본이 축에만
    // 몰려 있기 때문이다.
    constexpr const char* kDownsampleFile = "PostChainDownsample.hlsl";

    // ── 업샘플 + 누적 ──
    //
    // 거친 밉을 텐트 필터로 올려 더한다. 출력에 더하는 것이라 목적지가
    // 이미 담고 있는 값을 읽는다 — UAV 읽기·쓰기가 같은 픽셀이라 안전하다.
    constexpr const char* kUpsampleFile = "PostChainUpsample.hlsl";

    // ── Uber: 블룸 합성 + 톤맵 + 비네트 + 그레이딩 ──
    //
    // 넷 다 그 픽셀 하나만 보고 답이 나오는 연산이라 한 번에 끝낸다.
    // 기존은 이것을 네 패스로 나눠 화면을 네 번 왕복했다.
    //
    // 순서는 기존 체인과 같게 둔다: 블룸 → 톤맵 → 비네트 → 그레이딩.
    // 순서가 바뀌면 그림이 달라지는데(톤맵 뒤의 비네트와 앞의 비네트는
    // 다른 결과다), 그 차이가 이식 실수인지 의도인지 구분되지 않는다.
    constexpr const char* kUberFile = "PostChainUber.hlsl";

    // ── FXAA ──
    //
    // 톤맵 뒤(LDR)에서 돈다. HDR에서 돌리면 밝은 곳의 휘도 대비가 실제
    // 화면보다 훨씬 크게 나와서, 보이지도 않는 경계를 흐리고 정작 보이는
    // 경계는 놓친다 — 기존 체인이 그 순서였다.
    constexpr const char* kFxaaFile = "PostChainFxaa.hlsl";
}

#endif
