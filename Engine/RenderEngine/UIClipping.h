#pragma once

#include <algorithm>
#include <cmath>

// UI 클리핑 계산 (PHASE 3-6에서 공유로 뺐다).
//
// ── 왜 헤더로 옮겼나 ──
//
// 원래 UIRenderProxy.cpp 안의 inline 함수였다. DX12 UI 패스가 같은 계산을
// 해야 하는데 밖에서 못 쓰니, 그쪽에 다시 구현할 참이었다.
//
// 그러면 같은 질문을 두 곳에서 다르게 계산하게 된다. 이 이식에서 이미 두 번
// 물렸다 — 단독 렌더 대상 고르기와 경계 모양 재기에서 각각, 두 진단이 서로
// 다른 식을 쓰는 바람에 결과가 갈렸고 그때는 어느 쪽이 틀렸는지부터
// 알아내야 했다. 계산을 한 곳에 두면 그 실수가 구조적으로 불가능해진다.
//
// DX11 경로와 DX12 경로가 이 함수를 함께 쓴다. 여기가 바뀌면 둘 다 바뀐다 —
// 그것이 이 파일이 존재하는 이유다.

/// 잘린 목적 사각형을 픽셀로 반올림하기 전 상태로 돌려준다.
///
/// 반올림을 호출부로 미루는 이유: 호출부가 여기에 앵커 보정을 더한 뒤
/// 한 번만 반올림해야 한다. 두 번 반올림하면 값이 1픽셀 흔들린다(PHASE 7-6).
struct ClippedDestination
{
    float left, top, right, bottom;
};

/// 텍셀 단위의 원본 구간. DX11은 이것을 RECT로 SpriteBatch에 넘기고,
/// DX12는 텍스처 크기로 나눠 UV로 쓴다.
struct ClippedSource
{
    long left, top, right, bottom;
};

/// percent를 [0,1]로 자르고 방향에 따라 원본·목적 사각형을 만든다.
///
/// 돌려주는 false는 '아무것도 안 그린다'는 뜻이다(잘린 폭이 0).
inline bool CalculateClippedRects(
    ClipDirection dir,
    float percent,
    long texW, long texH,
    float left, float top, float right, float bottom,
    float scaleX, float scaleY,
    ClippedSource& outSrc, ClippedDestination& outDst)
{
    percent = std::clamp(percent, 0.f, 1.f);
    const long cutW = static_cast<long>(std::floor(texW * percent));
    const long cutH = static_cast<long>(std::floor(texH * percent));

    switch (dir)
    {
    case ClipDirection::LeftToRight:
        if (cutW <= 0) return false;
        outSrc = { 0, 0, cutW, texH };
        outDst = { left, top, left + cutW * scaleX, top + texH * scaleY };
        return true;

    case ClipDirection::RightToLeft:
        if (cutW <= 0) return false;
        outSrc = { texW - cutW, 0, texW, texH };
        outDst = { right - cutW * scaleX, top, right, top + texH * scaleY };
        return true;

    case ClipDirection::TopToBottom:
        if (cutH <= 0) return false;
        outSrc = { 0, 0, texW, cutH };
        outDst = { left, top, left + texW * scaleX, top + cutH * scaleY };
        return true;

    case ClipDirection::BottomToTop:
        if (cutH <= 0) return false;
        outSrc = { 0, texH - cutH, texW, texH };
        outDst = { left, bottom - cutH * scaleY, left + texW * scaleX, bottom };
        return true;

    case ClipDirection::None:
    default:
        outSrc = { 0, 0, texW, texH };
        outDst = { left, top, right, bottom };
        return true;
    }
}
