#include "CookedAssetCatalog.h"

#include <algorithm>
#include <set>
#include <utility>
#include <vector>

namespace experiment::cooked
{
    CookedAssetCatalog CookedAssetCatalog::Load(
        std::span<const std::byte> manifestBytes,
        std::filesystem::path derivedRoot,
        std::vector<AssetManifestIssue>& outIssues)
    {
        CookedAssetCatalog catalog;

        CookedAssetManifest manifest;
        if (!ReadAssetManifest(manifestBytes, manifest, outIssues))
        {
            // ★ 빈 catalog 를 돌려준다. 부분적으로 채워 두면 "없는 자산"과
            //   "아직 안 읽은 자산"을 구별할 수 없다.
            return catalog;
        }
        if (manifest.entries.empty())
        {
            outIssues.push_back(AssetManifestIssue{ "catalog",
                "빈 manifest로는 catalog를 세우지 않는다." });
            return catalog;
        }
        if (derivedRoot.empty())
        {
            outIssues.push_back(AssetManifestIssue{ "catalog.derivedRoot",
                "derived root가 비어 있다." });
            return catalog;
        }

        catalog.entries_ = manifest.entries;
        catalog.manifest_ = std::move(manifest);
        catalog.derivedRoot_ = std::move(derivedRoot);
        return catalog;
    }

    const CookedAssetManifestEntry* CookedAssetCatalog::Find(
        const AssetId& assetId) const noexcept
    {
        return manifest_.Find(assetId);
    }

    std::filesystem::path CookedAssetCatalog::ResolveArtifactPath(
        const AssetId& assetId) const
    {
        const CookedAssetManifestEntry* entry = Find(assetId);
        if (!entry || entry->artifactPath.empty()) return {};
        // artifactPath 는 `Derived/...` 로 시작하는 normalized virtual path 다
        // (manifest 계약이 강제한다). 그대로 이어 붙인다.
        return derivedRoot_ / std::filesystem::path(entry->artifactPath);
    }

    std::filesystem::path CookedAssetCatalog::ResolveSourcePath(
        const AssetId& assetId) const
    {
        const AssetSourceManifestEntry* entry = manifest_.FindSource(assetId);
        if (!entry) return {};
        const auto* first = reinterpret_cast<const char8_t*>(entry->sourcePath.data());
        const std::u8string utf8Path(first, first + entry->sourcePath.size());
        return (derivedRoot_ / std::filesystem::path(utf8Path))
            .lexically_normal();
    }

    std::size_t CookedAssetCatalog::CountOfKind(
        CookedAssetKind kind) const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(entries_,
            [kind](const CookedAssetManifestEntry& entry)
            {
                return entry.kind == kind;
            }));
    }

    bool CookedAssetCatalog::CollectClosure(const AssetId& root,
        std::vector<AssetId>& outOrdered, std::string& outFailure) const
    {
        outOrdered.clear();
        outFailure.clear();

        if (!Find(root))
        {
            outFailure = "root GUID가 catalog에 없다: "
                + Uuid::ToString(root.value);
            return false;
        }

        // 재귀 대신 명시 스택. 자산 그래프 깊이는 얕지만, 깊이를 데이터가
        // 정하는 순회를 재귀로 두면 저작 실수 하나가 스택을 넘긴다.
        //
        // 후위 순회로 위상 순서를 만든다: 의존을 모두 낸 뒤에 자기를 낸다.
        enum class State : std::uint8_t { Enter, Emit };
        struct Frame final { AssetId id; State state; };

        std::set<AssetId> visited;
        std::set<AssetId> emitted;
        std::vector<Frame> stack;
        stack.push_back({ root, State::Enter });

        while (!stack.empty())
        {
            const Frame frame = stack.back();
            stack.pop_back();

            if (frame.state == State::Emit)
            {
                if (emitted.insert(frame.id).second)
                    outOrdered.push_back(frame.id);
                continue;
            }

            // ★ 순환은 여기서 자연히 멈춘다. 중첩 프리팹이 서로를 품는
            //   저작 오류가 데이터로 존재할 수 있고, 그때도 로드는 되어야 한다.
            if (!visited.insert(frame.id).second) continue;

            const CookedAssetManifestEntry* entry = Find(frame.id);
            if (!entry)
            {
                // ★ **여기는 도달할 수 없다.** 그런데도 두는 이유는 UB 때문이지
                //   방어 때문이 아니다 — 아래에서 `entry->dependencies` 를
                //   역참조하므로 null 검사 자체는 있어야 한다.
                //
                //   도달 불가인 근거: `ReadAssetManifest` 가 `ValidateManifest` 를
                //   부르고, 그것이 **모든 dependency 가 manifest entry 로
                //   해석되는지** 이미 검사한다. 즉 catalog 에는 폐포가 닫힌
                //   manifest 만 들어온다. root 는 위에서 따로 확인했으므로
                //   여기 오는 id 는 전부 누군가의 dependency 다.
                //
                //   변이로 확인했다: 이 분기를 `continue` 로 바꿔도 게이트가
                //   초록이다(태울 데이터가 없다). "혹시 모르니"로 남긴 것이
                //   아니라 도달 불가를 확인한 뒤 남긴 것이고, 그 사실을 여기
                //   적어 둔다.
                outFailure = "해소되지 않는 dependency: "
                    + Uuid::ToString(frame.id.value);
                outOrdered.clear();
                return false;
            }

            stack.push_back({ frame.id, State::Emit });
            // 역순으로 넣어 원래 dependency 순서대로 처리되게 한다.
            for (auto it = entry->dependencies.rbegin();
                it != entry->dependencies.rend(); ++it)
            {
                stack.push_back({ *it, State::Enter });
            }
        }

        return true;
    }
}
