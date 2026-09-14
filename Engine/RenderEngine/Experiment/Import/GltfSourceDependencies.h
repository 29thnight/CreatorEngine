#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace experiment::importer
{
    // glTF 원본이 상대 URI 로 끌고 오는 사이드카(.bin·이미지) 목록.
    //
    // 왜 임포터(GltfImporter)와 따로 있나: 임포터는 **제자리에서** 여는 쪽이라
    // baseDirectory 만 알면 되지만, 에디터의 import 는 원본을 자산 트리로
    // **복사**한다. 복사 단계는 "이 .gltf 가 어떤 파일을 더 끌고 오는가"를
    // 기하·재질을 해석하기 전에 알아야 한다. 그 질문만 답하는 훑기다.
    //
    // GLB 내장 청크와 data: URI 는 목록에 담기지 않는다 — 옮길 파일이 없다.
    enum class GltfDependencyRejection
    {
        None,
        // 파일을 열지 못했거나 glTF JSON 이 아니다.
        ParseFailed,
        // 스킴이 붙었거나(http:·file://host) 절대 경로다. 임포트 원본은 신뢰
        // 경계 밖이라 원본 폴더 밖을 가리키는 참조는 따라가지 않는다.
        NonLocalUri,
        // 정규화하면 원본 폴더를 벗어난다(`..`).
        EscapingUri,
        // 참조한 파일이 원본 폴더에 없다. 복사해도 깨질 것이 확정이므로
        // 받아들이는 척하지 않고 여기서 끊는다.
        MissingSidecar,
    };

    [[nodiscard]] const char* ToString(GltfDependencyRejection rejection) noexcept;

    struct GltfSourceDependencies final
    {
        // 원본 .gltf 가 있는 폴더 기준 상대 경로. 중복은 제거되어 있고
        // 순서는 glTF 의 등장 순서(buffers 먼저, images 다음)다.
        std::vector<std::filesystem::path> relativePaths{};
        GltfDependencyRejection rejection{ GltfDependencyRejection::None };
        // 거부한 URI 원문 또는 파서 메시지. 사용자에게 이유를 말하는 데 쓴다.
        std::string rejectionDetail{};

        [[nodiscard]] bool Succeeded() const noexcept
        {
            return rejection == GltfDependencyRejection::None;
        }
    };

    // sourcePath 는 .gltf 또는 .glb 여야 한다. 그 밖의 확장자는 빈 성공을
    // 돌려준다(이 훑기가 답할 수 있는 질문이 아니다).
    [[nodiscard]] GltfSourceDependencies ScanGltfSourceDependencies(
        const std::filesystem::path& sourcePath);
}
