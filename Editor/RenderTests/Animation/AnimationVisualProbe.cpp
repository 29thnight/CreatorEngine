#include "Animation/AnimationVisualProbe.h"
#include "AnimationJob.h"
#include "Animator.h"
#include "Assets/ModelAssetGeneration.h"
#include "DataSystem.h"
#include "Material.h"
#include "MeshRenderer.h"
#include "ModelSceneInstantiation.h"
#include "RenderScene.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Socket.h"
#include <bit>
#include <cstring>
#include <mathematics/transform.hpp>

namespace RenderTest
{
    bool RunAnimationVisualProbe(const std::string& action, const std::string& modelPath,
        AnimationVisualReport& report, std::string& error)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene || !SceneManagers->m_isGameStart || !SceneManagers->IsGamePaused()
            || SceneManagers->HasPendingSceneStructureChange())
        { error = "Requires committed paused Play mode"; return false; }
        Entity* actor = scene->GetEntity("__AnimationVisualActor");
        Entity* marker = scene->GetEntity("P_Cube");
        Entity* camera = scene->GetEntity("MainCamera");
        if (!marker || !camera || !marker->GetComponent<MeshRenderer>())
        { error = "Requires FT_Primitives camera and P_Cube fixture"; return false; }
        if (action == "setup")
        {
            if (actor) { error = "Visual fixture already exists"; return false; }
            auto generation = DataSystems->LoadModelAssetGenerationByPath(modelPath);
            if (!generation || !generation->Skeleton() || generation->Animations().size() < 2)
            { error = "Requires a model with a skeleton and two clips"; return false; }
            for (auto* mesh : scene->GetRootEntity()->GetComponentsInChildren<MeshRenderer>())
                mesh->SetEnabled(false);
            actor = ModelSceneInstantiation::Instantiate(*scene, generation, {});
            if (!actor) { error = "Model instantiation failed"; return false; }
            scene->RenameEntity(*actor, "__AnimationVisualActor");
            actor->Transform_().SetScale({1.f, 1.f, 1.f});
            actor->Transform_().SetPosition({0.f, 0.f, 0.f});
            actor->Transform_().SetRotation({0.f, 1.f, 0.f, 0.f});
            camera->Transform_().SetPosition({0.f, 0.95f, -3.2f});
            camera->Transform_().SetRotation({0.f, 0.f, 0.f, 1.f});
            auto* animator = actor->GetComponent<Animator>();
            if (!animator) { error = "Instantiated model has no Animator"; return false; }
            std::string hand;
            for (const auto& bone : generation->Skeleton()->bones)
                if (bone.name == "hand_r" || bone.name == "Hand_R" || bone.name == "Bip01-R-Hand"
                    || bone.name == "mixamorig:RightHand")
                    hand = bone.name;
            if (hand.empty()) { error = "Fixture requires a right hand bone"; return false; }
            auto* socket = animator->MakeSocket("VisualMarker", hand, actor);
            if (!socket) { error = "Right hand Scene bone not found"; return false; }
            socket->m_offset = math::translation_matrix(math::vector3{0.f, 0.f, 0.22f});
            socket->AttachObject(marker);
            marker->Transform_().SetScale({0.12f, 0.12f, 0.12f});
            auto* mesh = marker->GetComponent<MeshRenderer>();
            auto material = std::make_shared<Material>(*mesh->m_Material);
            material->UseBaseColorMap(nullptr);
            material->SetBaseColor(0.01f, 1.f, 0.03f);
            material->SetMetallic(0.f);
            material->SetRoughness(0.6f);
            mesh->SetMaterial(std::move(material));
            mesh->SetEnabled(true);
        }
        if (!actor) { error = "Run setup first"; return false; }
        auto* animator = actor->GetComponent<Animator>();
        if (!animator || !animator->m_modelGeneration || animator->socketvec.empty())
        { error = "Visual fixture is incomplete"; return false; }
        const auto generation = animator->m_modelGeneration;
        // Exporters may reorder animation arrays. Choose the intended moving
        // poses by name so Idle/Death cannot silently replace the gait test.
        int walkIndex = -1, runIndex = -1;
        for (std::size_t i = 0; i < generation->Animations().size(); ++i)
        {
            if (generation->Animations()[i].name == "Walk") walkIndex = static_cast<int>(i);
            if (generation->Animations()[i].name == "Run") runIndex = static_cast<int>(i);
        }
        if (walkIndex < 0 || runIndex < 0)
        { error = "Visual fixture requires named Walk and Run clips"; return false; }
        if (action == "hidden" || action == "show")
        {
            actor->SetEnabled(action == "show");
            marker->SetEnabled(action == "show");
        }
        else
        {
            const bool blend = action == "blend0" || action == "blendhalf" || action == "blend1";
            const bool layer = action == "layer" || action == "layerdisabled"
                || action == "masked" || action == "upper";
            if (action != "setup" && action != "a" && action != "b" && action != "next" && !blend && !layer)
            { error = "Unknown visual pose"; return false; }
            animator->ClearControllersAndParams();
            auto first = std::make_shared<AnimationController>();
            first->m_owner = animator;
            first->name = "Base";
            const int clipIndex = action == "next" ? runIndex : walkIndex;
            first->CreateState("Current", clipIndex);
            first->CheckTransition();
            first->m_timeElapsed = static_cast<float>(generation->Animations()[clipIndex].durationTicks
                * (action == "next" ? 0.65 : action == "b" ? 0.7 : 0.15));
            animator->m_animationControllers.push_back(first);
            animator->blendT = 0.f;
            animator->m_isBlend = false;
            if (blend)
            {
                first->CreateState("Next", runIndex);
                auto* transition = first->CreateTransition("Current", "Next");
                transition->hasExitTime = true;
                transition->exitTime = 0.f;
                first->UpdateState();
                if (!first->m_isBlend || first->GetNextAnimationIndex() != runIndex)
                { error = "Visual blend transition did not start"; return false; }
                first->m_nextTimeElapsed = static_cast<float>(generation->Animations()[runIndex].durationTicks * 0.65);
                animator->blendT = action == "blend0" ? 0.f : action == "blend1" ? 1.f : 0.5f;
            }
            if (layer)
            {
                auto second = std::make_shared<AnimationController>();
                second->m_owner = animator;
                second->name = "Overlay";
                second->CreateState("Overlay", runIndex);
                second->CheckTransition();
                second->m_timeElapsed = static_cast<float>(generation->Animations()[runIndex].durationTicks * 0.65);
                second->m_avatarMask = new AvatarMask();
                second->m_avatarMask->useAll = action != "upper" && action != "masked";
                second->m_avatarMask->useUpper = action == "upper";
                second->m_avatarMask->useLower = false;
                second->useController = action != "layerdisabled";
                if (action == "masked") first->m_useLayer = false;
                animator->m_animationControllers.push_back(second);
            }
        }
        // Commandlet poses are frozen. Explicitly use the normal Scene publication
        // boundaries: paused frames intentionally do not resolve spatial writes.
        scene->DrainPendingLifecycle();
        scene->SyncDerivedState();
        if (action != "hidden")
            SceneManagers->GetRenderScene()->GetAnimationJob().Update(0.f);
        scene->SyncDerivedState();
        scene->UpdateRenderData();
        const auto position = animator->socketvec[0]->m_boneMatrix.translation();
        report.socketPosition = {position.x, position.y, position.z};
        report.bone = animator->socketvec[0]->m_ObjectName;
        report.modelId = FileGuid(generation->Identity().modelId).ToString();
        report.markerMeshId = marker->GetComponent<MeshRenderer>()->m_meshAssetId.ToString();
        report.paletteDigest = 2166136261u;
        for (std::size_t b = 0; b < generation->Skeleton()->bones.size() && b < kMaxBones; ++b)
        {
            std::array<float, 16> values;
            std::memcpy(values.data(), &animator->m_FinalTransforms[b], sizeof(values));
            for (const float v : values)
                report.paletteDigest = (report.paletteDigest ^ std::bit_cast<std::uint32_t>(v)) * 16777619u;
        }
        for (auto* mesh : actor->GetComponentsInChildren<MeshRenderer>())
            if (mesh->IsSkinnedMesh()) ++report.skinnedMeshes;
        if (report.skinnedMeshes == 0) { error = "No product skinned meshes"; return false; }
        return true;
    }
}
