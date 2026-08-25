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

    // 탄젠트 생성(mikktspace)의 **합성** 검사. 자산을 읽지 않는다.
    //
    // ★ 실자산 비교만으로는 "생성됐다"까지만 알 수 있고 **방향이 맞는지**는
    //   모른다. UV 를 손으로 정한 사각형은 정답이 해석적으로 나오므로,
    //   그것과 비교해 축·부호가 뒤집혔는지까지 잡는다.
    [[nodiscard]] bool RunExperimentTangentSelfTest(std::string& outLog);

    // 법선 생성(평면)의 **합성** 검사. 자산을 읽지 않는다.
    //
    // ★ 실자산 중 법선이 없는 것이 하나도 없다 — 이 경로는 실자산 게이트가
    //   영원히 밟지 않는다. 감김에서 법선 방향이 해석적으로 나오는 삼각형으로
    //   축·부호·평면화·퇴화 처리를 확인한다.
    [[nodiscard]] bool RunExperimentNormalSelfTest(std::string& outLog);
}
