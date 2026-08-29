#pragma once

#include "CookedAssetManifest.h"

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace experiment::cooked
{
    // CEMF 하나를 읽어 **GUID 로 묻는** 런타임 경계.
    //
    // ★ 이것이 대체하려는 것은 `DataSystem::LoadAssetCatalog` 다. 지금은 부팅
    //   때 asset root 를 재귀 순회하며 모든 `.meta` 를 YAML 로 파싱해 GUID↔경로
    //   표를 만든다. pak 에 CEMF 가 실려 있으므로 그 스캔은 필요 없다 —
    //   바이너리 하나를 읽으면 같은 표가 나오고, **덤으로 의존까지 나온다.**
    //
    // ★ **경계는 여기까지다.** catalog 는 조회만 한다. 실제 자산 로드는 I5 에서
    //   렌더 경로가 `experiment::Model` 을 직접 소비할 때 붙는다. 그전까지 이
    //   표의 소비자는 게이트 하나이고, 그 사실을 숨기지 않는다.
    //
    // ★ `AssetMetaRegistry` 를 대체하지 **않는다.** 그쪽은 GUID→**원본** 경로,
    //   이쪽은 GUID→**artifact** 경로다. 둘은 다른 표이고, 하나로 합치는 것은
    //   legacy 로더가 은퇴하는 I5/I6 의 일이다. 지금 합치면 어느 쪽이 정본인지
    //   흐려진다.
    class CookedAssetCatalog final
    {
    public:
        CookedAssetCatalog() = default;

        // CEMF 바이트에서 만든다. 실패하면 빈 catalog 를 돌려주고 issues 를
        // 채운다 — **부분적으로 채워진 catalog 를 내놓지 않는다.** 절반만 아는
        // 표는 "없는 자산"과 "아직 안 읽은 자산"을 구별하지 못한다.
        //
        // derivedRoot 는 `Derived/` 의 **부모**다(pak 마운트 루트 또는
        // 스테이징 Assets 디렉터리). artifactPath 가 `Derived/...` 로 시작하므로
        // 그대로 이어 붙이면 실제 파일이 된다.
        [[nodiscard]] static CookedAssetCatalog Load(
            std::span<const std::byte> manifestBytes,
            std::filesystem::path derivedRoot,
            std::vector<AssetManifestIssue>& outIssues);

        [[nodiscard]] bool IsEmpty() const noexcept { return entries_.empty(); }
        [[nodiscard]] std::size_t Size() const noexcept { return entries_.size(); }
        [[nodiscard]] const std::filesystem::path& DerivedRoot() const noexcept
        {
            return derivedRoot_;
        }

        [[nodiscard]] const CookedAssetManifestEntry* Find(
            const AssetId& assetId) const noexcept;

        // artifact 의 실제 파일 경로. 모르는 GUID 면 빈 경로다.
        [[nodiscard]] std::filesystem::path ResolveArtifactPath(
            const AssetId& assetId) const;

        // 이 종류의 entry 만 센다. 진단·게이트용.
        [[nodiscard]] std::size_t CountOfKind(CookedAssetKind kind) const noexcept;

        // ★ root 를 로드하는 데 필요한 전부(자기 포함)를 **위상 순서**로 담는다.
        //   의존이 먼저 오고 root 가 마지막이다 — 로더가 그 순서대로 열면
        //   참조가 항상 이미 준비돼 있다.
        //
        //   순환은 방문 집합으로 자연히 멈춘다(중첩 프리팹이 서로를 품는 저작
        //   오류가 데이터로 존재할 수 있다). 해소되지 않는 의존만 실패다 —
        //   그건 manifest 가 깨졌다는 뜻이고 조용히 넘어가면 런타임에서 빈
        //   자산이 된다.
        [[nodiscard]] bool CollectClosure(const AssetId& root,
            std::vector<AssetId>& outOrdered, std::string& outFailure) const;

    private:
        CookedAssetManifest manifest_{};
        std::vector<CookedAssetManifestEntry> entries_{};
        std::filesystem::path derivedRoot_{};
    };
}
