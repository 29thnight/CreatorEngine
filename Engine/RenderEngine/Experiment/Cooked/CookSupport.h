#pragma once

#include "../AssetIdentity.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // ★ `ModelCookProducer.cpp` 의 익명 namespace 에서 꺼낸 것들이다.
    //
    //   producer 가 model 하나에서 texture/ShaderMeta/material/scene/prefab 으로
    //   늘어나는데, 이 헬퍼들을 TU 마다 복제하면 **MSVC 유니티 빌드가 같은
    //   namespace 의 익명 namespace 를 합치면서 곧바로 재정의 오류**가 된다.
    //   (`VertexWelding.cpp` 와 `VertexCacheOptimization.cpp` 가 같은 이름의
    //   `ElapsedMs` 로 이미 한 번 깨졌다.)
    //
    //   이름을 서로 다르게 붙여 피하는 것보다 정본을 하나 두는 쪽이 낫다 —
    //   `.meta` GUID 판독 규칙이 producer 마다 갈라지면 어떤 자산은 느슨하게
    //   통과한다.

    [[nodiscard]] bool ReadTextFile(const std::filesystem::path& path,
        std::string& out);

    [[nodiscard]] bool ReadBinaryFile(const std::filesystem::path& path,
        std::vector<std::byte>& out);

    // child 가 root 아래에 있는가. `..` 세그먼트가 하나라도 있으면 거짓이다.
    [[nodiscard]] bool IsContainedPath(const std::filesystem::path& root,
        const std::filesystem::path& child);

    // sidecar `.meta` 하나에서 최상위 asset GUID 를 읽는다. 정규 소문자
    // UUIDv4 만 통과한다(`AssetIdentity.h` 의 판정을 그대로 쓴다).
    [[nodiscard]] bool ReadMetaAssetId(const std::filesystem::path& path,
        AssetId& outId, std::string& outFailure);
}
