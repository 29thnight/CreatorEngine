#include "Animation/AnimationVisualProbe.h"
#include "AnimationScheduler.h"
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
        const bool unobserved = action == "unobservedA" || action == "unobservedB";
        if (!animator || !animator->m_modelGeneration
            || (animator->socketvec.empty() && !unobserved))
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
        const bool ikPose = action == "ik-base" || action == "ik-full"
            || action == "ik-down1" || action == "ik-down2" || action == "ik-down3"
            || action == "ik-up1" || action == "ik-up2" || action == "ik-up3";
        if (ikPose)
        {
            // Every capture samples the same Walk time. Only the camera-selected
            // quality stage and retained IK fade state change between frames.
            animator->ClearControllersAndParams();
            auto controller = std::make_shared<AnimationController>();
            controller->m_owner = animator;
            controller->name = "IKBase";
            controller->CreateState("Current", walkIndex);
            controller->CheckTransition();
            controller->GetPlayback().timeElapsed = static_cast<float>(
                generation->Animations()[walkIndex].durationTicks * .15);
            animator->m_animationControllers.push_back(controller);
            animator->m_QualityRadius = action.starts_with("ik-down") ? .17f : 1.f;
            animator->GetInstance().qualityOverride = -1;
            if (action == "ik-base") animator->m_TwoBoneIKConstraints.clear();
            else if (animator->m_TwoBoneIKConstraints.size() != 1)
            { error = "Run ik-base before the IK transition"; return false; }
        }
        else if (action == "hidden" || action == "show")
        {
            actor->SetEnabled(action == "show");
            marker->SetEnabled(action == "show");
        }
        else
        {
            if (action == "unobservedA")
            {
                for (Socket* socket : animator->socketvec)
                {
                    if (socket) { socket->DetachAllObject(); delete socket; }
                }
                animator->socketvec.clear();
                marker->SetEnabled(false);
            }
            const bool blend = action == "blend0" || action == "blendhalf" || action == "blend1";
            const bool layer = action == "layer" || action == "layerdisabled"
                || action == "masked" || action == "upper" || action == "additive";
            if (action != "setup" && action != "a" && action != "b" && action != "next"
                && !unobserved && !blend && !layer)
            { error = "Unknown visual pose"; return false; }
            animator->ClearControllersAndParams();
            auto first = std::make_shared<AnimationController>();
            first->m_owner = animator;
            first->name = "Base";
            const int clipIndex = action == "next" ? runIndex : walkIndex;
            first->CreateState("Current", clipIndex);
            first->CheckTransition();
            first->GetPlayback().timeElapsed = static_cast<float>(generation->Animations()[clipIndex].durationTicks
                * (action == "next" ? 0.65 : action == "b" || action == "unobservedB" ? 0.7 : 0.15));
            animator->m_animationControllers.push_back(first);
            animator->GetPlaybackControl().blendT = 0.f;
            animator->GetPlaybackControl().isBlending = false;
            if (blend)
            {
                first->CreateState("Next", runIndex);
                auto* transition = first->CreateTransition("Current", "Next");
                transition->hasExitTime = true;
                transition->exitTime = 0.f;
                first->UpdateState();
	if (!first->IsBlending() || first->GetNextAnimationIndex() != runIndex)
                { error = "Visual blend transition did not start"; return false; }
                first->GetPlayback().nextTimeElapsed = static_cast<float>(generation->Animations()[runIndex].durationTicks * 0.65);
            animator->GetPlaybackControl().blendT = action == "blend0" ? 0.f : action == "blend1" ? 1.f : 0.5f;
            }
            if (layer)
            {
                auto second = std::make_shared<AnimationController>();
                second->m_owner = animator;
                second->name = "Overlay";
                second->CreateState("Overlay", runIndex);
                second->CheckTransition();
                second->GetPlayback().timeElapsed = static_cast<float>(generation->Animations()[runIndex].durationTicks * 0.65);
                second->m_avatarMask = new AvatarMask();
                second->m_avatarMask->useAll = action != "upper" && action != "masked";
                second->m_avatarMask->useUpper = action == "upper";
                second->m_avatarMask->useLower = false;
                second->useController = action != "layerdisabled";
                second->m_additive = action == "additive";
                if (action == "masked") first->m_useLayer = false;
                animator->m_animationControllers.push_back(second);
            }
        }
        // Commandlet poses are frozen. Explicitly use the normal Scene publication
        // boundaries: paused frames intentionally do not resolve spatial writes.
        scene->DrainPendingLifecycle();
        scene->SyncDerivedState();
        if (action != "hidden")
            SceneManagers->GetAnimationScheduler().Update(ikPose ? .05f : 0.f);
        if (action == "ik-base")
        {
            const auto& skeleton = *generation->Skeleton();
            const int end = animator->ResolveBoneIndex(animator->socketvec[0]->m_ObjectName);
            if (end < 0 || static_cast<std::size_t>(end) >= skeleton.bones.size())
            { error = "IK marker bone is missing"; return false; }
            const auto middle = skeleton.bones[end].parent;
            if (middle >= skeleton.bones.size())
            { error = "IK marker has no middle bone"; return false; }
            const auto start = skeleton.bones[middle].parent;
            const auto tracks = generation->AnimationTracks(walkIndex);
            if (start >= skeleton.bones.size() || end >= kMaxBones
                || tracks.size() <= static_cast<std::size_t>(end)
                || !tracks[start] || !tracks[middle] || !tracks[end])
            { error = "IK marker chain needs three tracked bones"; return false; }
            const auto& globals = animator->GetInstance().poseGlobals;
            const math::vector3 root = globals[start].translation();
            const math::vector3 elbow = globals[middle].translation();
            const math::vector3 hand = globals[end].translation();
            if (math::length(elbow - root) <= .01f
                || math::length(hand - elbow) <= .01f)
            { error = "IK marker chain has zero reach"; return false; }
            const math::matrix4x4 world = actor->Transform_().GetWorldMatrix();
            TwoBoneIKConstraint constraint{};
            constraint.StartBone = skeleton.bones[start].name;
            constraint.MiddleBone = skeleton.bones[middle].name;
            constraint.EndBone = skeleton.bones[end].name;
            constraint.TargetWorld = math::transform_point(
                root + (hand - root) * .6f + math::vector3{ 0.f, .12f, .08f }, world);
            constraint.PoleWorld = math::transform_point(
                elbow + math::vector3{ .1f, .2f, .3f }, world);
            constraint.Enabled = true;
            animator->m_TwoBoneIKConstraints.push_back(std::move(constraint));
        }
        scene->SyncDerivedState();
        scene->UpdateRenderData();
        if (!unobserved)
        {
            const auto position = animator->socketvec[0]->m_boneMatrix.translation();
            report.socketPosition = {position.x, position.y, position.z};
            report.bone = animator->socketvec[0]->m_ObjectName;
        }
        AnimatorPoseUploadMetrics publication{};
        if (scene->TryGetLastAnimatorPoseMetrics(*animator, publication))
        {
            report.observedBones = publication.observedBones;
            report.projectedBones = publication.projectedBones;
            report.localWrites = publication.localWrites;
            report.paletteDirty = publication.paletteDirty;
            report.paletteChanged = publication.paletteChanged;
        }
        report.modelId = FileGuid(generation->Identity().modelId).ToString();
        auto& instance = animator->GetInstance();
        report.qualityStage = static_cast<int>(instance.qualityStage);
        report.optionalIKWeight = instance.optionalIKWeight;
        report.projectedHeight = instance.projectedHeight;
        instance.taskList.for_each_reachable([&](const animation::task& item)
        {
            if (item.kind == animation::task_kind::two_bone_ik) ++report.ikTasks;
        });
        report.markerMeshId = marker->GetComponent<MeshRenderer>()->m_meshAssetId.ToString();
        report.paletteDigest = 2166136261u;
        for (std::size_t b = 0; b < generation->Skeleton()->bones.size() && b < kMaxBones; ++b)
        {
            std::array<float, 16> values;
            std::memcpy(values.data(), &animator->GetInstance().finalTransforms[b], sizeof(values));
            for (const float v : values)
                report.paletteDigest = (report.paletteDigest ^ std::bit_cast<std::uint32_t>(v)) * 16777619u;
        }
        for (auto* mesh : actor->GetComponentsInChildren<MeshRenderer>())
            if (mesh->IsSkinnedMesh()) ++report.skinnedMeshes;
        if (report.skinnedMeshes == 0) { error = "No product skinned meshes"; return false; }
        return true;
    }
}
