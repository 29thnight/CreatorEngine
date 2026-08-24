#pragma once

#include <string>

namespace RenderTest
{
    // 보간 방식(Linear/Step)의 **합성** 검사. 자산을 읽지 않는다.
    //
    // ★ 존재 이유: 실자산(Gunner)의 Step 트랙 455개가 전부 상수라
    //   Step 샘플러가 판별력 있는 입력으로 한 번도 실행되지 않는다. 계단
    //   계산이 틀려도 experiment.gltf 는 "455 보존, 위반 0"으로 초록이다.
    //   값이 달라지는 키를 코드로 만들어 그 구멍을 닫는다.
    [[nodiscard]] bool RunExperimentSamplerSelfTest(std::string& outLog);
}
