#pragma once

#include "ImportedScene.h"

namespace experiment::importer
{
    // fastgltf 기반 glTF 2.0 / GLB 임포터.
    //
    // ★ 좌표 규약: glTF 는 오른손 Y-up 이고 엔진은 왼손이다. legacy 가 Assimp 에
    //   준 `aiProcess_ConvertToLeftHanded`(= MakeLeftHanded + FlipUVs +
    //   FlipWindingOrder)와 **같은 변환**을 여기서 적용해 ImportedScene 이
    //   이미 엔진 관례인 값만 담게 한다. 하류는 포맷별 관례를 모른다.
    //     - 위치·법선·탄젠트: z 부호 반전
    //     - 회전 쿼터니언: x·y 부호 반전
    //     - 이동 키: z 부호 반전
    //     - UV: v = 1 - v
    //     - 인덱스: 삼각형 감김 순서 뒤집기
    //
    // 미구현(전부 ImportNote 로 계수한다 — 조용히 넘어가지 않는다):
    //   법선·탄젠트 생성(mikktspace), Draco, 모프 타깃, 카메라·라이트,
    //   KHR_texture_transform, sparse accessor.
    class GltfImporter final : public IAssetImporter
    {
    public:
        [[nodiscard]] bool CanImport(
            const std::filesystem::path& sourcePath) const override;
        [[nodiscard]] ImportResult Import(const ImportRequest& request) override;
    };
}
