#pragma once

#include <string>

namespace RenderTest
{
    // 쿠킹 포맷의 **합성** 검사. 자산을 읽지 않는다.
    //
    // ★ 실자산 왕복만으로는 부족하다. 실자산이 우연히 안 쓰는 형태 — 빈 이름,
    //   스켈레톤 없음, material property 의 9개 대안 전부, 유니코드 경로, 메시
    //   0개 — 가 바로 나중에 조용히 깨지는 자리다. 여기서는 그 형태를 코드로
    //   만들어 굽고 되읽어 **모든 값이 같은지** 본다.
    //
    // ★ 그리고 거부가 실제로 되는지 본다. 버전 규약은 "적어 두었다"가 아니라
    //   "안 맞으면 실제로 거부한다"여야 의미가 있고, 더 중요하게는 **거부했을 때
    //   draft 가 나오지 않아야** 한다. 거부해 놓고 빈 모델을 돌려주면 legacy 의
    //   조용한 오독을 이름만 바꿔 물려받는 것이다.
    [[nodiscard]] bool RunExperimentCookedSelfTest(std::string& outLog);

    // 실자산 왕복 — 임포트한 draft 를 굽고 되읽어 원본과 대조한다.
    // 합성 검사가 못 보는 규모(정점 수만·키 수만)에서의 범위 계산을 본다.
    [[nodiscard]] bool RunExperimentCookedRoundTrip(
        const std::string& modelPath, std::string& outLog);
}
