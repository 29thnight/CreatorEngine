#pragma once

#include <string>

namespace RenderTest
{
    // PHASE 3.75 MBC7 — 활성 씬의 MeshRenderer가 typed generation handle로 서 있고,
    // 재질의 embedded texture owner가 전역 등록부·이름 폴백이 아니라 그 모델의
    // generation closure(DataSystem::ResolveModelGenerationTexture)에서 왔는지를
    // 씬 전수로 잰다. Gunner가 있으면 §6.2 폐쇄 조건(renderer ≥ 2·embedded 6)을
    // 함께 단정한다. stdout 한 줄(`[CLI] assets.scenemodel pass|fail ...`)이 게이트
    // 관측 창구다.
    [[nodiscard]] bool RunSceneModelGenerationSelfTest(std::string& outLog);
    [[nodiscard]] bool RunIncrementalModelCancellationSelfTest(const std::string& guardPath);

    // MBC7 — 모델 reimport(ContentReload) 뒤 이전 texture generation owner가
    // 재사용되지 않는가(§6.2 마지막 조건). 같은 프로세스에서 generation을 은퇴시키고
    // 다시 게시해 texture owner의 포인터 신원을 전후 대조한다.
    [[nodiscard]] bool RunSceneModelGenerationReloadSelfTest(
        const std::string& modelName, std::string& outLog);
}
