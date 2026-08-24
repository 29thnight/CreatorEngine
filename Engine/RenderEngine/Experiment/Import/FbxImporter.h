#pragma once

#include "ImportedScene.h"

// FBX 임포터 (ufbx).
//
// GltfImporter 와 **같은 계약**이다 — Scene·Entity·DataSystem 싱글톤에 손대지
// 않고 완전 소유 ImportedScene 만 돌려준다. 후처리(탄젠트 생성 등)도 같은
// 패스를 공유하므로, 같은 모델이 glTF 로 들어오든 FBX 로 들어오든 결과가 같다.
//
// ★ 좌표 규약은 legacy(Assimp `aiProcess_ConvertToLeftHanded`)를 재현한다.
//   ufbx 는 `target_axes` 로 축 변환을 직접 해 주므로 손으로 z 를 뒤집던
//   glTF 경로와 달리 라이브러리에 맡긴다. **단위는 건드리지 않는다** —
//   legacy 가 cm→m 환산을 하지 않으므로 여기서 하면 100배 어긋난다.
namespace experiment::importer
{
    class FbxImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] bool CanImport(
            const std::filesystem::path& sourcePath) const override;
        [[nodiscard]] ImportResult Import(const ImportRequest& request) override;
    };
}
