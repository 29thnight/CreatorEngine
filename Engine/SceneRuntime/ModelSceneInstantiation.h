#pragma once
// PHASE 3.75 MBC9 — 모델 자산의 씬 인스턴스화. legacy `Model`/`ModelLoader`
// (Assimp 산물·역브리지)와 experiment 핸들이 은퇴하고, 씬에 세우는 정본 입력은
// immutable `assets::ModelAssetGeneration` 하나다.
//
// 규약은 구 ModelSceneBridge를 그대로 승계한다:
//   - 메시 N개 노드는 노드 자신 대신 메시 엔티티 N개를 사슬로 만든다(i번째가
//     i-1번째의 자식). 자식 노드는 사슬 끝에 붙는다.
//   - 메시 0개 비루트 노드만 자기 엔티티를 만들고 본 이름 대조 목록에 든다.
//   - 루트 트랜스폼은 단일 노드·단일 메시 특례에서만 기록한다.
//   - 본 계층은 이 모델이 만든 엔티티 안에서만 이름을 대조한다(씬 전역 검색 금지).
// 게시 계약(parent < index) 덕에 노드·본 모두 단일 순회다. 계약을 어기는
// generation이면 아무것도 만들지 않고 nullptr — 반쪽 시공은 없다.

#include <memory>
#include <string>
#include <chrono>
#include "EntityHandle.h"

class Entity;
class Scene;
namespace assets { class ModelAssetGeneration; }

namespace ModelSceneInstantiation
{
    struct Options final
    {
        // 메시 엔티티마다 RigidBody + MeshCollider를 붙인다(sidecar
        // ModelImporter.CreateMeshCollider — DataSystem::ReadModelCreateMeshCollider).
        bool createMeshCollider{ false };
    };

    // Prepare는 씬을 건드리지 않는다. 호출자는 작업 스레드에 COM을 초기화하고,
    // 작업이 끝날 때까지 DataSystem을 살려 둔다. Advance/Cancel은 씬 구조 변경
    // 경계에서만 호출한다. 한 작업은 한 인스턴스의 재질과 생성 상태를 소유한다.
    class PendingInstance final
    {
    public:
        enum class Status { Building, Complete, Failed };
        static std::unique_ptr<PendingInstance> Prepare(
            std::shared_ptr<const assets::ModelAssetGeneration> generation,
            const Options& options);
        ~PendingInstance();
        Status Advance(Scene& scene, std::size_t maxSteps = 16,
            std::chrono::microseconds budget = std::chrono::milliseconds(2),
            std::size_t maxRendererActivations = 2);
        void Cancel(Scene& scene);
        EntityHandle Root() const;
        std::size_t CompletedSteps() const;
        std::size_t TotalSteps() const;

    private:
        struct Impl;
        explicit PendingInstance(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> m_impl;
    };

    // 성공하면 루트 엔티티. 관측은 ModelConsumptionDiagnostics 계수(읽기 전용)로만
    // 남긴다 — stdout 토큰은 MBC10에서 은퇴했다.
    Entity* Instantiate(Scene& scene,
        const std::shared_ptr<const assets::ModelAssetGeneration>& generation,
        const Options& options);
}
