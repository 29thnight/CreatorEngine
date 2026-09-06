#include "EditorDiagnostics.h"
#include "EditorObjectOperations.h"
#include "Scene.h"
#include "Animator.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>
#include <unordered_set>
namespace EditorDiagnostics
{
    CommandCore::CommandResult ValidateHierarchy(Scene* scene)
    {

        // scene.hierarchycheck — 계층 표기의 불변식을 잰다.
        //
        // 재는 불변식은 하나다:
        //
        //     자식이 부모의 m_childrenIndices에 실려 있다  <=>  자식의 m_parentIndex가 그 부모다
        //
        // 이 쌍이 깨지면 순회(m_Entities[0]->m_childrenIndices에서만 내려간다)가
        // 서브트리를 통째로 빠뜨리는데 에러도 로그도 없다 — 뼈 61개가 그렇게
        // 순회 밖에 있었다.
        //
        // 최상위 오브젝트의 표기가 갈려 있는 것이 그 뿌리다(SceneGraphRedesignPlan
        // 트랙 E). 같은 뜻인데 두 값이 쓰인다:
        //   · Entity::AddChild            -> m_parentIndex = 부모 인덱스(루트면 0)
        //   · Scene::AttachExistingEntity / DDOL 이탈 -> INVALID_INDEX(-1)
        // 둘 다 씬 루트의 children에는 들어가므로, "-1인데 루트 children에 있음"이
        // 정상처럼 보인다. 그 상태를 세는 것이 topLevelInvalid다.
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "활성 씬이 없다");
        }

        const auto& objects = scene->m_Entities;

        size_t total = 0;
        size_t topLevelRoot = 0;      // 최상위인데 m_parentIndex == 0 (쌍이 맞는 표기)
        size_t topLevelInvalid = 0;   // 최상위인데 m_parentIndex == INVALID (쌍이 어긋난 표기)
        size_t pairMismatch = 0;      // 부모의 children에 있는데 m_parentIndex가 그 부모가 아님
        size_t orphan = 0;            // 아무의 children에도 없음(씬 루트 제외)
        size_t unreachable = 0;       // 씬 루트에서 children만 따라 내려가 닿지 못함

        // 어느 부모의 children에 실려 있는지 역인덱스를 만든다.
        std::unordered_map<Entity::Index, Entity::Index> listedUnder;
        for (const auto& obj : objects)
        {
            if (!obj) continue;
            for (Entity::Index childIdx : obj->GetChildrenIndices())
            {
                listedUnder[childIdx] = obj->m_index;
            }
        }

        // 씬 루트에서 children만 따라 내려가 닿는 집합.
        std::unordered_set<Entity::Index> reached;
        if (!objects.empty() && objects[0])
        {
            std::vector<Entity::Index> stack{ objects[0]->m_index };
            reached.insert(objects[0]->m_index);
            while (!stack.empty())
            {
                const Entity::Index cur = stack.back();
                stack.pop_back();
                const auto& node = scene->TryGetEntity(cur);
                if (!node) continue;
                for (Entity::Index childIdx : node->GetChildrenIndices())
                {
                    if (reached.insert(childIdx).second) stack.push_back(childIdx);
                }
            }
        }

        for (const auto& obj : objects)
        {
            if (!obj) continue;
            ++total;
            if (Entity::kSceneRootIndex == obj->m_index) continue;   // 씬 루트 자신은 제외

            auto it = listedUnder.find(obj->m_index);
            if (it == listedUnder.end())
            {
                ++orphan;
            }
            else if (it->second == Entity::kSceneRootIndex)
            {
                if (Entity::IsInvalidIndex(obj->GetParentIndex())) ++topLevelInvalid;
                else if (Entity::kSceneRootIndex == obj->GetParentIndex()) ++topLevelRoot;
                else ++pairMismatch;
            }
            else if (it->second != obj->GetParentIndex())
            {
                ++pairMismatch;
            }

            if (reached.find(obj->m_index) == reached.end()) ++unreachable;
        }

        const size_t storeMismatch = scene->CountHierarchyStoreMismatches();
        std::printf("[scene.hierarchycheck] 오브젝트 %zu · 최상위(0표기) %zu · 최상위(-1표기) %zu"
            " · 쌍불일치 %zu · 고아 %zu · 순회미도달 %zu · Store불일치 %zu\n",
            total, topLevelRoot, topLevelInvalid, pairMismatch, orphan, unreachable, storeMismatch);

        // ★ **이 명령에는 진짜 판정이 있다.** 쌍불일치·고아·순회미도달·Store불일치는
        //   전부 "순회가 서브트리를 통째로 빠뜨리는" 상태이고, 그것이 뼈 61 개를
        //   순회 밖에 두었던 결함이다(위 주석). 지금까지는 그 수를 찍기만 하고
        //   프로세스는 0 으로 끝났다 — 세어 놓고 판정하지 않고 있었다.
        //
        //   최상위(-1표기)는 세되 **판정에 넣지 않는다.** 그것은 같은 뜻의 표기가
        //   둘이라는 이미 알려진 상태이고(트랙 E), 순회를 깨뜨리지는 않는다.
        const size_t broken = pairMismatch + orphan + unreachable + storeMismatch;

        CommandCore::CommandData data = CommandCore::CommandData::Object();
        data.Set("objects", CommandCore::CommandData::Int(static_cast<int64_t>(total)));
        data.Set("topLevelRoot", CommandCore::CommandData::Int(static_cast<int64_t>(topLevelRoot)));
        data.Set("topLevelInvalid", CommandCore::CommandData::Int(static_cast<int64_t>(topLevelInvalid)));
        data.Set("pairMismatch", CommandCore::CommandData::Int(static_cast<int64_t>(pairMismatch)));
        data.Set("orphan", CommandCore::CommandData::Int(static_cast<int64_t>(orphan)));
        data.Set("unreachable", CommandCore::CommandData::Int(static_cast<int64_t>(unreachable)));
        data.Set("storeMismatch", CommandCore::CommandData::Int(static_cast<int64_t>(storeMismatch)));
        if (broken > 0)
        {
            return CommandCore::Fail("scene.hierarchy_broken",
                "계층 불변식 위반 " + std::to_string(broken) + "건", std::move(data));
        }
        return CommandCore::Ok("계층 불변식 통과", std::move(data));
    }

    CommandCore::CommandResult AnimatorStatus(Scene* scene)
    {
        if (nullptr == scene)
        {
            std::printf("[CLI] animator.status fail 활성 씬 없음\n");
            return CommandCore::PreconditionFailed("scene.none", "No active scene");
        }
        using CommandCore::CommandData;
        CommandData data = CommandData::Object(), entries = CommandData::Array();
        std::size_t animatorCount = 0;
        for (const auto& object : scene->m_Entities)
        {
            if (!object || object->IsDestroyMark()) continue;
            Animator* animator = object->GetComponent<Animator>();
            if (nullptr == animator) continue;
            if (0 == animator->GetSkeletonSerial()) continue;

            ++animatorCount;
            std::uint32_t digest = 2166136261u;
            const std::size_t bones =
                (std::min)(animator->GetBoneCount(), (std::size_t)MAX_BONES);
            for (std::size_t bone = 0; bone < bones; ++bone)
            {
                const float* values = &animator->m_FinalTransforms[bone].m[0][0];
                for (int element = 0; element < 16; ++element)
                {
                    const std::int32_t quantized = static_cast<std::int32_t>(
                        std::lround(static_cast<double>(values[element]) * 4096.0));
                    std::uint32_t bits = static_cast<std::uint32_t>(quantized);
                    for (int byte = 0; byte < 4; ++byte)
                    {
                        digest ^= (bits >> (byte * 8)) & 0xFFu;
                        digest *= 16777619u;
                    }
                }
            }
            // 루프 판정과 클립 길이를 함께 찍는다 — elapsed만 보면 "끝에서
            // 멈춘 것"과 "긴 클립을 지나는 중"을 구별할 수 없다(실측으로
            // 그 둘을 혼동할 뻔했다).
            const int clipIndex = static_cast<int>(animator->m_AnimIndexChosen);
            const double duration = animator->GetClipDuration(clipIndex);
            // 팔레트가 갱신되는데 화면이 안 움직이면 그 다음 구간이다 —
            // Scene::PublishAnimatorPose가 팔레트를 씬 packed storage와
            // 부착 오브젝트 Transform으로 commit하는 자리. MBC10: 진단이 publish를
            // **다시 부르지 않는다**(상태 변경) — 제품 barrier가 남긴 마지막 메트릭을
            // 읽기 전용 스냅샷으로 읽는다. source=none이면 아직 barrier가 한 번도
            // 안 돌았거나 애니메이터가 barrier 대상이 아니었던 것이다.
            AnimatorPoseUploadMetrics publish{};
            const bool hasPublish =
                scene->TryGetLastAnimatorPoseMetrics(*animator, publish);
            CommandData entry = CommandData::Object(), publication = CommandData::Object();
            entry.Set("id", CommandData::String(EditorObjectOperations::ObjectId(scene->HandleOf(object->m_index))));
            entry.Set("name", CommandData::String(object->GetHashedName().ToString()));
            entry.Set("path", CommandData::String(animator->TypedSkeleton() ? "generation" : "none"));
            entry.Set("enabled", CommandData::Bool(animator->IsEnabled()));
            entry.Set("clip", CommandData::Int(clipIndex));
            entry.Set("elapsed", CommandData::Double(animator->m_TimeElapsed));
            entry.Set("duration", CommandData::Double(duration));
            entry.Set("loop", CommandData::Bool(animator->IsClipLooping(clipIndex)));
            entry.Set("clips", CommandData::Int(animator->GetClipCount()));
            entry.Set("bones", CommandData::Int(bones));
            char palette[9]{};
            std::snprintf(palette, sizeof(palette), "%08X", digest);
            entry.Set("palette", CommandData::String(palette));
            publication.Set("source", CommandData::String(hasPublish ? "product" : "none"));
            publication.Set("uploaded", CommandData::Bool(publish.uploaded));
            publication.Set("packed", CommandData::Bool(publish.packed));
            publication.Set("legacyFallback", CommandData::Bool(publish.legacyFallback));
            publication.Set("rebound", CommandData::Bool(publish.rebound));
            publication.Set("disabled", CommandData::Bool(publish.disabled));
            publication.Set("staleOwner", CommandData::Bool(publish.staleOwner));
            publication.Set("skeletonMissing", CommandData::Bool(publish.skeletonMissing));
            publication.Set("bindLookups", CommandData::Int(publish.bindLookups));
            publication.Set("validBones", CommandData::Int(publish.validBones));
            publication.Set("invalidBones", CommandData::Int(publish.invalidBones));
            publication.Set("localWrites", CommandData::Int(publish.localWrites));
            publication.Set("queuedRoots", CommandData::Int(publish.queuedRoots));
            publication.Set("paletteDirty", CommandData::Int(publish.paletteDirty));
            entry.Set("publication", std::move(publication));
            entries.Append(std::move(entry));
            std::printf("[CLI] animator.status publish %s source=%s uploaded=%d "
                "packed=%d legacyFallback=%d rebound=%d disabled=%d "
                "staleOwner=%d skeletonMissing=%d bindLookups=%llu "
                "validBones=%llu invalidBones=%llu localWrites=%llu "
                "queuedRoots=%llu paletteDirty=%llu\n",
                object->GetHashedName().ToString().c_str(),
                hasPublish ? "product" : "none",
                publish.uploaded ? 1 : 0, publish.packed ? 1 : 0,
                publish.legacyFallback ? 1 : 0, publish.rebound ? 1 : 0,
                publish.disabled ? 1 : 0, publish.staleOwner ? 1 : 0,
                publish.skeletonMissing ? 1 : 0,
                (unsigned long long)publish.bindLookups,
                (unsigned long long)publish.validBones,
                (unsigned long long)publish.invalidBones,
                (unsigned long long)publish.localWrites,
                (unsigned long long)publish.queuedRoots,
                (unsigned long long)publish.paletteDirty);
            std::printf("[CLI] animator.status %s path=%s enabled=%d "
                "clip=%u elapsed=%.4f duration=%.4f loop=%d clips=%zu "
                "bones=%zu palette=%08X\n",
                object->GetHashedName().ToString().c_str(),
                animator->TypedSkeleton() ? "generation" : "none",
                animator->IsEnabled() ? 1 : 0,
                animator->m_AnimIndexChosen, animator->m_TimeElapsed,
                duration, animator->IsClipLooping(clipIndex) ? 1 : 0,
                animator->GetClipCount(), bones, digest);
        }
        // 프록시 커밋 누계 — 팔레트가 렌더로 가려면 프록시가 다시 만들어져야
        // 한다. 시간을 두고 두 번 부를 때 committed가 늘지 않으면 최신 팔레트가
        // 렌더에 도달하지 않는다(그 상태가 "메시는 나오는데 안 움직인다"다).
        const RenderProxyCommitMetrics proxy = scene->GetRenderProxyCommitMetrics();
        std::printf("[CLI] animator.status done animators=%zu "
            "proxyCommitted=%llu proxyPending=%llu proxyPublishCalls=%llu\n",
            animatorCount,
            (unsigned long long)proxy.committed,
            (unsigned long long)proxy.pending,
            (unsigned long long)proxy.publishCalls);
        data.Set("animators", std::move(entries));
        data.Set("count", CommandData::Int(animatorCount));
        data.Set("proxyCommitted", CommandData::Int(proxy.committed));
        data.Set("proxyPending", CommandData::Int(proxy.pending));
        data.Set("proxyPublishCalls", CommandData::Int(proxy.publishCalls));
        return CommandCore::Ok("Animator status", std::move(data));
    }
}
