#include "Animation/AnimationBaselineProbe.h"
#include "AnimationScheduler.h"
#include "Animator.h"
#include "BoneComponent.h"
#include "CameraComponent.h"
#include "Assets/ModelAssetGeneration.h"
#include "DataSystem.h"
#include "MeshRenderer.h"
#include "ModelSceneInstantiation.h"
#include "RenderScene.h"
#include "RuntimeSettings.h"
#include "Scene.h"
#include "SceneManager.h"
#include "Socket.h"
#include "ThreadPool.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace RenderTest
{
    static bool RunAnimationProbe(const std::string& modelPath, std::size_t actors,
        int qualityStage, double budgetMs,
        AnimationBaselineReport& report, std::string& error)
    {
        Scene* scene = SceneManagers->GetActiveScene();
        auto* renderScene = SceneManagers->GetRenderScene();
        if (!scene || !renderScene || !SceneManagers->IsPlayCommitted()
            || !SceneManagers->IsGamePaused() || SceneManagers->HasPendingSceneStructureChange())
        { error = "Requires committed paused Play"; return false; }
        auto& job = SceneManagers->GetAnimationScheduler();
        if (job.GetAnimatorCount() || (actors != 10 && actors != 50 && actors != 100)
            || qualityStage < -1 || qualityStage > 7
            || !std::isfinite(budgetMs) || budgetMs < 0.)
        { error = "Requires an empty animation registry and 10, 50 or 100 actors"; return false; }
        auto generation = DataSystems->LoadModelAssetGenerationByPath(modelPath);
        if (!generation || !generation->Skeleton())
        { error = "Requires a skinned model generation"; return false; }
        int walk = -1;
        for (std::size_t i = 0; i < generation->Animations().size(); ++i)
            if (generation->Animations()[i].name == "Walk") walk = static_cast<int>(i);
        if (walk < 0 || generation->Skeleton()->bones.empty() || generation->Skeleton()->bones.size() > kMaxBones)
        { error = "Requires named Walk and 1..512 bones"; return false; }
        std::string hand;
        for (const auto& bone : generation->Skeleton()->bones)
            if (bone.name == "hand_r" || bone.name == "Hand_R") hand = bone.name;
        if (hand.empty()) { error = "Baseline fixture requires a right hand bone"; return false; }
        const AnimationBudgetSettings savedSettings = RuntimeSettings::Get()
            .GetAnimationBudgetSettings();
        struct RestoreBudget final
        {
            AnimationBudgetSettings settings;
            ~RestoreBudget() { RuntimeSettings::Get().SetAnimationBudgetSettings(settings); }
        } restoreBudget{ savedSettings };
        if (budgetMs > 0.)
        {
            AnimationBudgetSettings configured = savedSettings;
            configured.cpuBudgetMs = budgetMs;
            RuntimeSettings::Get().SetAnimationBudgetSettings(configured);
        }
        std::vector<EntityHandle> roots;
        std::vector<Animator*> animators;
        // The commandlet is synchronous on the owner thread. Frames are sealed
        // here so 120 samples cannot accumulate unbounded palette allocations.
        // No render-thread execution or GPU upload time is claimed by this probe.
        const auto discard = []
        {
            auto batch = ProxyCommandQueueController::GetInstance()->CapturePending();
            ProxyCommandQueueController::GetInstance()->MarkSuperseded(batch.size());
        };
        const auto cleanup = [&]
        {
            for (const auto handle : roots)
                if (scene->Resolve(handle)) scene->DestroyEntity(handle.index);
            scene->EndFramePass();
            discard();
        };
        try
        {
            report = {};
            report.m_actors = actors;
            report.m_qualityStage = qualityStage;
            report.m_budgetMs = budgetMs;
            report.m_bones = generation->Skeleton()->bones.size();
            report.m_lowDetailBoneCount = (qualityStage >= 5 || budgetMs > 0.) && report.m_bones > 1
                ? static_cast<std::uint32_t>((std::min)(std::size_t{ 40 }, report.m_bones - 1))
                : 0;
            report.m_workers = ce::get_thread_pool().size();
            report.m_frames.reserve(120);
            for (std::size_t i = 0; i < actors; ++i)
            {
                auto* actor = ModelSceneInstantiation::Instantiate(*scene, generation, {});
                if (!actor) throw std::runtime_error("Model instantiation failed");
                roots.push_back(scene->HandleOf(actor->m_index));
                auto* animator = actor->GetComponent<Animator>();
                if (!animator) throw std::runtime_error("Missing Animator");
                animators.push_back(animator);
                // The model root is implicit; instantiation creates the other bones.
                const auto sceneBones = actor->GetComponentsInChildren<BoneComponent>().size();
                if (sceneBones + 1 != report.m_bones) throw std::runtime_error("Incomplete Scene bone hierarchy");
                report.m_sceneBones += sceneBones;
                animator->ClearControllersAndParams();
                auto controller = std::make_shared<AnimationController>();
                controller->m_owner = animator;
                controller->CreateState("Walk", walk);
                controller->CheckTransition();
                if (controller->GetAnimationIndex() != walk) throw std::runtime_error("Walk controller not active");
                controller->GetPlayback().timeElapsed = static_cast<float>(generation->Animations()[walk].durationTicks * i / actors);
                animator->m_animationControllers.push_back(std::move(controller));
                animator->m_LowDetailBoneCount = report.m_lowDetailBoneCount;
                auto* marker = scene->CreateEntity("AnimationBaselineSocket");
                roots.push_back(scene->HandleOf(marker->m_index));
                auto* socket = animator->MakeSocket("BaselineHand", hand, actor);
                if (!socket) throw std::runtime_error("Socket binding failed");
                socket->AttachObject(marker);
                for (auto* mesh : actor->GetComponentsInChildren<MeshRenderer>())
                    if (mesh->IsSkinnedMesh()) ++report.m_skinnedMeshes;
            }
            if (budgetMs > 0.)
            {
                Entity* camera = scene->CreateEntity("AnimationBudgetCamera");
                roots.push_back(scene->HandleOf(camera->m_index));
                camera->Transform_().SetPosition({ 0.f, 0.f, -400.f });
                camera->AddComponent<CameraComponent>();
            }
            scene->DrainPendingLifecycle();
            scene->SyncDerivedState();
            if (job.GetAnimatorCount() != actors || !report.m_skinnedMeshes)
                throw std::runtime_error("Product animation registry or skin meshes missing");
            // L7 must freeze a valid published pose, not an uninitialized actor.
            if (qualityStage == 7) job.Update(0.f);
            for (auto* animator : animators)
                animator->GetInstance().qualityOverride = qualityStage;
            for (int frame = 0; frame < 150; ++frame)
            {
                AnimationFrameMetrics sample;
                {
                    AnimationMeasurementScope measurement(sample);
                    job.Update(1.f / 60.f);
                    const auto syncBegin = AnimationMeasurementScope::Clock::now();
                    scene->SyncDerivedState();
                    const auto commitBegin = AnimationMeasurementScope::Clock::now();
                    scene->UpdateRenderData();
                    const auto end = AnimationMeasurementScope::Clock::now();
                    sample.m_syncUs = AnimationMeasurementScope::Microseconds(syncBegin, commitBegin);
                    sample.m_renderCommitUs = AnimationMeasurementScope::Microseconds(commitBegin, end);
                }
                discard();
                if (frame < 30) continue;
                const auto chunks = (std::min)(actors,
                    (std::max)(std::size_t{ 1 }, report.m_workers) * 2);
                if (sample.m_jobs != chunks || sample.m_updatePassJobs != chunks
                    || sample.m_executePassJobs != (qualityStage == 7 ? 0 : chunks)
                    || sample.m_evaluatedAnimators != (qualityStage == 7 ? 0 : actors)
                    || (qualityStage != 7 && (!sample.m_localWrites
                        || sample.m_validBones != report.m_sceneBones
                        || (budgetMs > 0.
                            ? (sample.m_paletteCopies == 0
                                || sample.m_paletteCopies > actors
                                || sample.m_paletteBytes != sample.m_paletteCopies
                                    * report.m_bones * sizeof(math::matrix4x4))
                            : (sample.m_paletteCopies != actors
                                || sample.m_paletteBytes != actors * report.m_bones
                                    * sizeof(math::matrix4x4)))))
                    || sample.m_paletteArenaAllocations != 0
                    || sample.m_poseStorageGrowths != 0)
                    throw std::runtime_error("Measured path mismatch: jobs=" + std::to_string(sample.m_jobs)
                        + " update=" + std::to_string(sample.m_updatePassJobs)
                        + " execute=" + std::to_string(sample.m_executePassJobs)
                        + " evaluated=" + std::to_string(sample.m_evaluatedAnimators)
                        + " bones=" + std::to_string(sample.m_validBones)
                        + " writes=" + std::to_string(sample.m_localWrites)
                        + " palettes=" + std::to_string(sample.m_paletteCopies)
                        + " poseGrowths=" + std::to_string(sample.m_poseStorageGrowths));
                for (const auto* animator : animators)
                    for (std::size_t b = 0; b < report.m_bones; ++b)
                        if (!std::isfinite(animator->GetInstance().finalTransforms[b].translation().x))
                            throw std::runtime_error("Non-finite measured pose");
                report.m_frames.push_back(sample);
            }
            cleanup();
            if (job.GetAnimatorCount() != 0) throw std::runtime_error("Animation fixture registry did not drain");
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            cleanup();
            return false;
        }
    }

    bool RunAnimationBaselineProbe(const std::string& modelPath, std::size_t actors,
        int qualityStage, AnimationBaselineReport& report, std::string& error)
    {
        return RunAnimationProbe(modelPath, actors, qualityStage, 0., report, error);
    }

    bool RunAnimationBudgetProbe(const std::string& modelPath, std::size_t actors,
        double budgetMs, AnimationBaselineReport& report, std::string& error)
    {
        return RunAnimationProbe(modelPath, actors, -1, budgetMs, report, error);
    }
}
