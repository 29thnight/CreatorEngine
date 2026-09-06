#include "ModelSceneInstantiation.h"

#include "Scene.h"
#include "Animator.h"
#include "BoneComponent.h"
#include "DataSystem.h"
#include "Entity.h"
#include "ExperimentMaterialMigration.h"
#include "Material.h"
#include "MeshCollider.h"
#include "MeshRenderer.h"
#include "ModelConsumptionDiagnostics.h"
#include "RigidBodyComponent.h"
#include "Assets/ModelAssetGeneration.h"
#include "Experiment/ModelData.h"

#include <algorithm>
#include <limits>
#include <map>
#include <unordered_map>
#include <vector>

namespace ModelSceneInstantiation
{
    struct PendingInstance::Impl
    {
        struct ObjectRecipe
        {
            std::string name;
            std::size_t parent{ 0 };
            std::uint32_t mesh{ assets::kInvalidModelAssetIndex };
            math::matrix4x4 transform{ math::matrix4x4::identity() };
            bool writeTransform{ false };
            bool bone{ false };
            GameObjectType type{ GameObjectType::Mesh };
        };
        std::shared_ptr<const assets::ModelAssetGeneration> generation;
        Options options;
        std::vector<ObjectRecipe> objects;
        std::vector<std::shared_ptr<Material>> materials;
        std::vector<std::shared_ptr<const experiment::Material>> authored;
        std::vector<std::uint32_t> meshMaterials;
        std::vector<EntityHandle> handles;
        std::vector<std::size_t> renderers;
        std::size_t created{ 0 };
        std::size_t activated{ 0 };
        bool animatorReady{ false };
        bool hasBones{ false };
        std::uint32_t constructionScene{ 0 };
        Status status{ Status::Building };
    };

    PendingInstance::PendingInstance(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
    PendingInstance::~PendingInstance() = default;

    std::unique_ptr<PendingInstance> PendingInstance::Prepare(
        std::shared_ptr<const assets::ModelAssetGeneration> generation, const Options& options)
    {
        const auto reject = []() -> std::unique_ptr<PendingInstance>
        {
            ModelConsumptionDiagnostics::NoteInstantiateRejected();
            return {};
        };
        if (!generation) return reject();
        const auto nodes = generation->Nodes();
        const auto meshes = generation->Meshes();
        const auto materials = generation->Materials();
        if (nodes.empty() || nodes[0].parent != assets::kInvalidModelAssetIndex)
            return reject();

        auto impl = std::make_unique<Impl>();
        impl->generation = std::move(generation);
        impl->options = options;
        const auto* skeleton = impl->generation->Skeleton();
        impl->hasBones = skeleton && !skeleton->bones.empty()
            && skeleton->rootBone < skeleton->bones.size();
        std::map<Uuid::Uuid16, std::uint32_t> meshIndices;
        std::map<Uuid::Uuid16, std::uint32_t> materialIndices;
        for (std::uint32_t i = 0; i < meshes.size(); ++i) meshIndices.emplace(meshes[i].meshId, i);
        for (std::uint32_t i = 0; i < materials.size(); ++i) materialIndices.emplace(materials[i].materialId, i);
        impl->meshMaterials.resize(meshes.size(), assets::kInvalidModelAssetIndex);
        for (std::size_t i = 0; i < meshes.size(); ++i)
            if (auto found = materialIndices.find(meshes[i].materialId); found != materialIndices.end())
                impl->meshMaterials[i] = found->second;

        // 기존 계층 규약을 작업 스레드에서 평탄화한다. 부모는 항상 먼저 만들어진다.
        impl->objects.push_back({ impl->generation->Name() });
        std::unordered_map<std::string, std::size_t> boneCandidates;
        boneCandidates.emplace(impl->generation->Name(), 0);
        std::vector<std::size_t> attachPoints(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& node = nodes[i];
            if (i != 0 && node.parent >= i) return reject();
            std::size_t parent = i == 0 ? 0 : attachPoints[node.parent];
            for (const auto& meshId : node.meshes)
            {
                auto mesh = meshIndices.find(meshId);
                if (mesh == meshIndices.end()) return reject();
                if (nodes.size() == 1 && node.meshes.size() == 1)
                {
                    auto& root = impl->objects[0];
                    root.mesh = mesh->second;
                    root.transform = node.localTransform;
                    root.writeTransform = true;
                }
                else
                {
                    impl->objects.push_back({ node.name, parent, mesh->second,
                        node.localTransform, true });
                    parent = impl->objects.size() - 1;
                }
            }
            if (i != 0 && node.meshes.empty())
            {
                impl->objects.push_back({ node.name, parent, assets::kInvalidModelAssetIndex,
                    node.localTransform, true });
                parent = impl->objects.size() - 1;
                boneCandidates.try_emplace(node.name, parent);
            }
            attachPoints[i] = parent;
        }
        if (impl->hasBones)
        {
            std::vector<std::size_t> boneAttach(skeleton->bones.size());
            for (std::size_t i = 0; i < skeleton->bones.size(); ++i)
            {
                if (i == skeleton->rootBone) continue;
                const auto& bone = skeleton->bones[i];
                if (bone.parent != assets::kInvalidModelAssetIndex && bone.parent >= i)
                    return reject();
                const auto found = boneCandidates.find(bone.name);
                std::size_t object;
                if (found != boneCandidates.end()) object = found->second;
                else
                {
                    object = impl->objects.size();
                    const std::size_t parent = bone.parent < boneAttach.size() ? boneAttach[bone.parent] : 0;
                    impl->objects.push_back({ bone.name, parent, assets::kInvalidModelAssetIndex,
                        math::matrix4x4::identity(), false, true, GameObjectType::Bone });
                }
                impl->objects[object].bone = true;
                boneAttach[i] = object;
            }
        }
        for (std::size_t i = 0; i < impl->objects.size(); ++i)
        {
            if (impl->objects[i].name.empty()) return reject();
            if (impl->objects[i].mesh != assets::kInvalidModelAssetIndex) impl->renderers.push_back(i);
        }

        // 재질 변환, 외부 이미지 디코드/압축, embedded 이미지 owner 생성을 모두
        // 씬 생성 전에 끝낸다. mutable Material은 이 인스턴스만 소유한다.
        impl->materials.resize(materials.size());
        impl->authored.resize(materials.size());
        for (const std::size_t objectIndex : impl->renderers)
        {
            const auto index = impl->meshMaterials[impl->objects[objectIndex].mesh];
            if (index >= materials.size() || impl->materials[index]) continue;
            auto converted = std::make_shared<experiment::Material>();
            ExperimentMaterialMigration::ConvertModelMaterialAsset(materials[index], *impl->generation, *converted);
            auto material = std::make_shared<Material>();
            std::string error;
            if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(*converted, nullptr, *material, error))
                return reject();
            DataSystems->FinalizeMaterialRuntime(*material);
            DataSystems->BindModelGenerationTextures(*material, *impl->generation);
            impl->materials[index] = std::move(material);
            if (!materials[index].shaderAssetId.IsNil()) impl->authored[index] = std::move(converted);
        }
        impl->handles.resize(impl->objects.size());
        return std::unique_ptr<PendingInstance>(new PendingInstance(std::move(impl)));
    }

    PendingInstance::Status PendingInstance::Advance(Scene& scene, std::size_t maxSteps,
        std::chrono::microseconds budget, std::size_t maxRendererActivations)
    {
        auto& state = *m_impl;
        if (state.status != Status::Building) return state.status;
        if (!state.constructionScene)
        {
            scene.BeginIncrementalConstruction();
            state.constructionScene = scene.GetSceneId();
        }
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::size_t steps = 0;
        std::size_t activations = 0;
        const auto fail = [&]()
        {
            Cancel(scene);
            state.status = Status::Failed;
            ModelConsumptionDiagnostics::NoteInstantiateRejected();
            return state.status;
        };
        if (state.created && (!scene.Resolve(Root()) || scene.Resolve(Root())->IsDestroyMark())) return fail();
        while (steps < maxSteps && (steps == 0 || std::chrono::steady_clock::now() < deadline))
        {
            if (state.created < state.objects.size())
            {
                const std::size_t index = state.created;
                const auto& recipe = state.objects[index];
                Entity* parent = index == 0 ? nullptr : scene.Resolve(state.handles[recipe.parent]);
                if (index != 0 && (!parent || parent->IsDestroyMark())) return fail();
                Entity* object = scene.CreateEntity(recipe.name, recipe.type,
                    parent ? parent->m_index : Entity::kSceneRootIndex);
                if (!object) return fail();
                state.handles[index] = scene.HandleOf(object->m_index);
                ++state.created; // 이후 단계가 실패해도 생성된 범위를 회수한다.
                if (recipe.writeTransform)
                    object->Transform_().SetLocalMatrix(recipe.transform, TransformWriteReason::ModelImport);
                if (recipe.bone)
                {
                    object->AddComponent<BoneComponent>();
                    object->SetRootIndex(state.handles[0].index);
                }
                if (recipe.mesh != assets::kInvalidModelAssetIndex)
                {
                    auto* renderer = object->AddComponent<MeshRenderer>();
                    renderer->SetEnabled(false);
                    renderer->m_isSkinnedMesh = state.hasBones;
                    // 새 renderer는 아직 재질이 없다. 텍스처 owner가 준비된 재질을
                    // 뒤에 붙여, 이 스레드에서 이미지 캐시 miss를 처리하지 않는다.
                    if (!renderer->BindModelGeneration(state.generation, recipe.mesh)) return fail();
                    const auto materialIndex = state.meshMaterials[recipe.mesh];
                    if (materialIndex < state.materials.size())
                    {
                        renderer->SetMaterial(state.materials[materialIndex]);
                        renderer->SetExperimentMaterialBase(state.authored[materialIndex]);
                    }
                }
            }
            else if (!state.animatorReady)
            {
                if (state.hasBones)
                {
                    auto* animator = scene.Resolve(Root())->AddComponent<Animator>();
                    animator->m_Motion = FileGuid(state.generation->Identity().modelId);
                    animator->BindModelGeneration(state.generation);
                    animator->SetEnabled(true);
                }
                state.animatorReady = true;
            }
            else if (state.activated < state.renderers.size())
            {
                if (activations >= maxRendererActivations) break;
                auto* object = scene.Resolve(state.handles[state.renderers[state.activated]]);
                if (!object || object->IsDestroyMark()) return fail();
                if (state.options.createMeshCollider)
                {
                    object->AddComponent<RigidBodyComponent>();
                    auto* collider = object->AddComponent<MeshColliderComponent>();
                    collider->SetDensity(0);
                    collider->SetDynamicFriction(0);
                    collider->SetStaticFriction(0);
                    collider->SetRestitution(0);
                }
                object->GetComponent<MeshRenderer>()->SetEnabled(true);
                ++state.activated;
                ++activations;
            }
            else
            {
                scene.EndIncrementalConstruction();
                state.constructionScene = 0;
                state.status = Status::Complete;
                ModelConsumptionDiagnostics::NoteInstantiated(state.generation->Name());
                break;
            }
            ++steps;
        }
        return state.status;
    }

    void PendingInstance::Cancel(Scene& scene)
    {
        // 핸들 세대 검증으로 삭제/슬롯 재사용 이후의 다른 오브젝트를 건드리지 않는다.
        for (std::size_t i = m_impl->created; i > 0; --i)
            if (auto* object = scene.Resolve(m_impl->handles[i - 1]); object && !object->IsDestroyMark())
                scene.DestroyEntity(object->m_index);
        if (m_impl->constructionScene == scene.GetSceneId())
        {
            scene.EndIncrementalConstruction();
            m_impl->constructionScene = 0;
        }
        m_impl->status = Status::Failed;
    }
    EntityHandle PendingInstance::Root() const { return m_impl->created ? m_impl->handles[0] : EntityHandle{}; }
    std::size_t PendingInstance::CompletedSteps() const
    { return m_impl->created + (m_impl->animatorReady ? 1 : 0) + m_impl->activated; }
    std::size_t PendingInstance::TotalSteps() const
    { return m_impl->objects.size() + 1 + m_impl->renderers.size(); }

    Entity* Instantiate(Scene& scene,
        const std::shared_ptr<const assets::ModelAssetGeneration>& generation, const Options& options)
    {
        auto pending = PendingInstance::Prepare(generation, options);
        if (!pending) return nullptr;
        try
        {
            if (pending->Advance(scene, (std::numeric_limits<std::size_t>::max)(),
                std::chrono::hours(24), (std::numeric_limits<std::size_t>::max)())
                != PendingInstance::Status::Complete)
            {
                pending->Cancel(scene);
                return nullptr;
            }
        }
        catch (...)
        {
            pending->Cancel(scene);
            throw;
        }
        return scene.Resolve(pending->Root());
    }
}
