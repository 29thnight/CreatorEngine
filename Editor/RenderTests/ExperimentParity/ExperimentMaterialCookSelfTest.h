#pragma once

#include <string>

namespace RenderTest
{
    // standalone material producer 의 **합성** 검사.
    //
    // ★ 실자산 standalone material 은 둘뿐이고 둘 다 정상이라 거부 경로를
    //   하나도 태우지 못한다. `m_shaderMetaGuid` 누락·비정규, texture GUID
    //   비정규 같은 형태는 합성으로만 만들 수 있다.
    [[nodiscard]] bool RunExperimentMaterialCookSelfTest(std::string& outLog);

    // 실자산 standalone material 하나.
    [[nodiscard]] bool RunExperimentMaterialCookReal(
        const std::string& assetRootPath, const std::string& materialPath,
        std::string& outLog);

    // ★ 모델 쪽 검사다. b2c-3 이 바꾼 것이 여기 있다 —
    //   재질 entry 가 진짜 의존(ShaderMeta + texture)을 갖고, 임베디드 texture
    //   가 Derived artifact 로 뽑히는가.
    //
    //   **모든 재질 의존이 이 product 안에서 해소되는지**까지 본다. 간선만
    //   그려 놓고 노드가 없으면 manifest writer 가 나중에 거부하는데, 그때는
    //   어느 재질이 원인인지 안 보인다.
    [[nodiscard]] bool RunExperimentModelDependencyReal(
        const std::string& assetRootPath, const std::string& modelPath,
        std::string& outLog);
}
