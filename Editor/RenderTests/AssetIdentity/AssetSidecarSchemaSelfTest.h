#pragma once

#include <string>
#include <vector>
#include <cstddef>

namespace RenderTest
{
    struct AssetSidecarReport
    {
        std::size_t passed{}, failed{}, models{}, modelsPassed{}, subAssets{}, registry{};
        struct Model
        {
            std::string name;
            bool ok{};
            std::size_t mat{}, matSem{}, matAuth{}, tex{}, texSem{}, texAuth{}, mesh{}, skel{}, anim{};
        };
        std::vector<Model> corpus;
    };
    // MBC2 — identity epoch header·stable key 규칙 엔진·sidecar schema v2 코덱/폐포.
    //
    //   [1] epoch header: CSPRNG seed·YAML 왕복·변조 거부
    //   [2] stable key 문법: 허용 접두 셋, ordinal·빈 값·비NFC·대문자 거부
    //   [3] 규칙 엔진(합성 요소): semantic/authoring 배정, 지문 재결합, 삭제=경고,
    //       변경+무명=오류, exporter id 중복=오류
    //   [4] sidecar v2 코덱: 왕복·다른 키 보존·guid 제거·v1/ordinal/변조 거부·폐포 재유도
    //   [5] 실자산 corpus(assetRoot 아래 glb/gltf/fbx): 임포트 → 배정 → v2 생성 → 폐포 검증,
    //       전 모델 subasset을 한 registry에 넣어 충돌 0, 같은 입력 재배정이 동일 신원.
    //
    // assetRoot가 비어 있으면 [5]를 건너뛰고 그 사실을 로그에 남긴다(조용히 통과하지 않음).
    [[nodiscard]] bool RunAssetSidecarSchemaSelfTest(const std::string& assetRoot,
        std::string& outLog, AssetSidecarReport* report = nullptr);
}
