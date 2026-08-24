#pragma once

#include <string>

namespace RenderTest
{
    // GltfImporter(fastgltf) 경로 검증 — 파이프라인의 첫 실제 생산자.
    //
    //   파일 → GltfImporter → ImportedScene → ModelDraft → Model
    //
    // 기준선은 legacy(Assimp) 경로다. 다만 **정점 단위 일치를 기대하면 안 된다** —
    // legacy 는 aiProcessPreset_TargetRealtime_Fast(JoinIdenticalVertices ·
    // GenNormals · CalcTangentSpace 등)를 거치므로 정점 수 자체가 다르다.
    // 그래서 파서에 무관한 의미 신호로 비교한다:
    //   - 삼각형 수 (JoinIdenticalVertices 는 정점만 합치고 삼각형은 보존한다)
    //   - 메시 로컬 AABB (좌표 규약이 같으면 일치해야 한다 — 좌표 변환 검증)
    //   - 노드·재질·본·클립 **이름 집합**
    //   - 클립 길이(초)
    bool RunExperimentGltfImportSelfTest(
        const std::string& modelPath, std::string& outLog);
}
