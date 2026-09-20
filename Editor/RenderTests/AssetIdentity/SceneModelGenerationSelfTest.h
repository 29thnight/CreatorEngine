#pragma once

#include <string>
#include <cstdint>

namespace RenderTest
{
    struct SceneModelReport
    {
        std::uint64_t renderers{};
        std::uint64_t generationBound{};
        std::uint64_t unbound{};
        std::uint64_t handleInvalid{};
        std::uint64_t rhiView{};
        std::uint64_t meshIdPersisted{};
        std::uint64_t textureProps{};
        std::uint64_t embeddedProps{};
        std::uint64_t generationTextures{};
        std::uint64_t otherTextures{};
        std::uint64_t missingTextures{};
        std::uint64_t robotRenderers{};
        std::uint64_t robotEmbedded{};
        std::uint64_t textures{};
        std::uint64_t reused{};
        std::uint64_t created{};
        std::uint64_t missing{};
        std::uint64_t retired{};
        bool reload{}, sameAggregate{};
    };
    // PHASE 3.75 MBC7 — 활성 씬의 MeshRenderer가 typed generation handle로 서 있고,
    // 재질의 embedded texture owner가 전역 등록부·이름 폴백이 아니라 그 모델의
    // generation closure(DataSystem::ResolveModelGenerationTexture)에서 왔는지를
    // 씬 전수로 잰다. Robot가 있으면 §6.2 폐쇄 조건(renderer ≥ 4·renderer당 embedded property 4)을
    // 함께 단정한다. stdout 한 줄(`[CLI] assets.scenemodel pass|fail ...`)이 게이트
    // 관측 창구다.
    [[nodiscard]] bool RunSceneModelGenerationSelfTest(std::string& outLog, SceneModelReport* report = nullptr);
    [[nodiscard]] bool RunIncrementalModelCancellationSelfTest(const std::string& guardPath);

    // A newer ContentReload replaces texture owners; a duplicate notification for
    // the same immutable generation preserves aggregate and texture identities.
    [[nodiscard]] bool RunSceneModelGenerationReloadSelfTest(
        const std::string& modelName, std::string& outLog, SceneModelReport* report = nullptr);
}
