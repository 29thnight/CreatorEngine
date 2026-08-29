#pragma once

#include <string>

namespace RenderTest
{
    // `ResolvingModelDecoder` 의 합성 검사.
    //
    // ★ 이 검사는 **자산을 읽지 않는다.** 가짜 decoder 두 개를 꽂아 호출
    //   순서와 issue 를 본다 — 실자산으로는 "cooked 가 거부되어 source 로
    //   폴백하는" 상황을 만들 수가 없다(거부시키려면 artifact 를 일부러
    //   망가뜨려야 한다).
    //
    // ★ 확인하는 것은 셋이다.
    //   1. preference 대로 **누가 호출되는가** (그리고 안 불려야 할 쪽이
    //      정말 안 불리는가 — 호출 계수로 본다)
    //   2. 폴백이 **관측 가능한가** — Info 한 줄이 반드시 남는가
    //   3. cooked 의 거부 사유가 **지워지지 않는가**
    [[nodiscard]] bool RunExperimentResolverSelfTest(std::string& outLog);
}
