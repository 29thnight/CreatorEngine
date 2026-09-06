// experiment 합성 계약 probe — 에디터 없이 도는 단독 실행 파일
//
// ★ 여기 있는 검사들은 원래 `experiment.*` CLI 명령이었다. 명령이 아니어야 하는
//   이유는 하나다 — **엔진 프로세스가 필요 없다.** 씬도, 디바이스도, registry 도
//   건드리지 않고 합성 입력을 만들어 단정만 한다. 그런 것은 유닛 테스트지
//   에디터 조작이 아닌데, 돌릴 러너가 없어서 명령 registry 를 빌려 입고 있었다.
//
//   명령으로 두는 값이 공짜가 아니다. 골든 행 · descriptor · help 줄 · 서명 이행
//   대상이 되어 영구 유지 비용이 붙고, PHASE 14.5 §18 은 폐기 조항 없이 "모든
//   command 가 terminal CommandResult 를 만든다" 를 요구한다.
//
// ★ 선례를 따랐다. `mathematics_contract_probe.cpp` · `hashing_string_contract_probe.cpp`
//   · `authoring_base64_contract_probe.cpp` 셋이 이미 이 모양이다 — cl.exe 로 엔진
//   헤더에 직접 컴파일하고 명령 registry 를 거치지 않는다. **새로 만든 기전이 없다.**
//
// ★★ 검사 하나에 게이트 하나를 두지 않는다. 명령 여덟을 게이트 여덟으로 바꾸면
//   줄이려던 그 실수를 이름만 바꿔 되풀이하는 것이다. 실행 파일 하나가 전부를
//   돌리고, 인자로 이름을 주면 그것만 돌린다.
//
// 빌드는 `Tools/regression/verify-experiment-contract.ps1` 이 한다.

// ★★★ 2026-09-06 — 나머지 일곱이 들어왔다. **엔진 정적 라이브러리를 링크한다.**
//
//   `cacheopt` · `vertexlayout` · `texcook` · `matcodec` · `matinstance` ·
//   `matseal` · `smcook` 은 `Mesh` · `MeshOptimizer` · `Core::TimeSystem` ·
//   `Material` · `Texture` · rapidyaml 의 **정의**를 요구한다. 그 .cpp 를 하나씩
//   끌어오는 대신 `RenderEngine.lib` · `SceneRuntime.lib` · `Utility_Framework.lib`
//   을 링크한다.
//
//   그 대가로 이 probe 는 더 이상 "엔진 빌드 없이 도는" 것이 아니다 — 엔진
//   라이브러리와 그 DLL 이 있어야 돈다. 선례 셋(mathematics·hashing_string·
//   authoring_base64)과 다른 성질이므로 게이트 주석에 그 차이를 적어 두었다.
//   **얻는 것은 그대로다**: 에디터 프로세스도, 디바이스도, 씬도 없이 돈다.
//
//   부수 효과 하나가 좋다 — `VertexCacheOptimization` 을 직접 컴파일하지 않으므로
//   `"meshoptimizer.h"` 가 엔진의 `MeshOptimizer.h` 로 풀리던 대소문자 충돌을
//   아예 만나지 않는다. 라이브러리 안의 그 TU 는 엔진의 include 순서로 이미
//   올바르게 지어져 있다.

#include "ExperimentParity/ExperimentCacheOptSelfTest.h"
#include "ExperimentParity/ExperimentMaterialCodecSelfTest.h"
#include "ExperimentParity/ExperimentMaterialInstanceSelfTest.h"
#include "ExperimentParity/ExperimentMaterialSealSelfTest.h"
#include "ExperimentParity/ExperimentResolverSelfTest.h"
#include "ExperimentParity/ExperimentSamplerSelfTest.h"
#include "ExperimentParity/ExperimentShaderMetaCookSelfTest.h"
#include "ExperimentParity/ExperimentTextureCookSelfTest.h"
#include "ExperimentParity/ExperimentVertexLayoutSelfTest.h"
#include "ExperimentParity/ExperimentWeldSelfTest.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Contract
    {
        const char* name;
        bool (*run)(std::string&);
    };

    // ★ `sampler` · `tangent` · `normal` 은 registry 에서 명령 셋이었는데 구현은
    //   파일 하나(790줄)를 나눠 쓰고 있었다. 셋이 각자 18줄짜리 같은 래퍼를 달고
    //   있었다는 뜻이다. 여기서는 진입점 그대로 셋을 유지한다 — 단정 집합이 실제로
    //   다르기 때문이다. 다만 이름이 셋이라는 사실이 registry 밖으로 나왔다.
    constexpr Contract kContracts[] = {
        { "cacheopt",     &RenderTest::RunExperimentCacheOptSelfTest },
        { "matcodec",     &RenderTest::RunExperimentMaterialCodecSelfTest },
        { "matinstance",  &RenderTest::RunExperimentMaterialInstanceSelfTest },
        { "matseal",      &RenderTest::RunExperimentMaterialSealSelfTest },
        { "normal",       &RenderTest::RunExperimentNormalSelfTest },
        { "resolver",     &RenderTest::RunExperimentResolverSelfTest },
        { "sampler",      &RenderTest::RunExperimentSamplerSelfTest },
        { "smcook",       &RenderTest::RunExperimentShaderMetaCookSelfTest },
        { "tangent",      &RenderTest::RunExperimentTangentSelfTest },
        { "texcook",      &RenderTest::RunExperimentTextureCookSelfTest },
        { "vertexlayout", &RenderTest::RunExperimentVertexLayoutSelfTest },
        { "weld",         &RenderTest::RunExperimentWeldSelfTest },
    };
}

int main(int argc, char** argv)
{
    // 인자를 주면 그 이름만 돌린다. 주지 않으면 전부 돈다.
    const char* filter = (argc > 1) ? argv[1] : nullptr;

    int ran = 0;
    int failed = 0;
    std::vector<std::string> failedNames;

    for (const Contract& contract : kContracts)
    {
        if (nullptr != filter && 0 != std::strcmp(filter, contract.name))
        {
            continue;
        }

        std::string log;
        const bool ok = contract.run(log);
        ++ran;

        std::printf("=== experiment.%s ===\n", contract.name);
        std::fputs(log.c_str(), stdout);
        std::printf("[%s] %s\n\n", ok ? "PASS" : "FAIL", contract.name);

        if (!ok)
        {
            ++failed;
            failedNames.emplace_back(contract.name);
        }
    }

    // ★ 필터가 아무것도 고르지 않으면 **실패다.** 0 건을 돌고 초록을 내면 이름을
    //   틀리게 적은 게이트가 영원히 통과한다 — 아무것도 안 보는 검사는 없는 것만
    //   못하다. 이 저장소가 같은 실수를 이미 한 번 했다(무인증 경로 검사가 Yoda
    //   비교를 못 찾아 라우트 0 개를 보고 통과했다).
    if (0 == ran)
    {
        std::fprintf(stderr, "[EXPERIMENT CONTRACT] 이름이 하나도 맞지 않았다: %s\n",
                     (nullptr != filter) ? filter : "(없음)");
        return 2;
    }

    std::printf("[EXPERIMENT CONTRACT] %d건 중 통과 %d · 실패 %d\n",
                ran, ran - failed, failed);
    for (const std::string& name : failedNames)
    {
        std::fprintf(stderr, "[EXPERIMENT CONTRACT] 실패: %s\n", name.c_str());
    }

    return (0 == failed) ? 0 : 1;
}
