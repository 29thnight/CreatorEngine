#include "Animation/AnimationPlaybackSelfTest.h"
#include "AnimationScheduler.h"
#include "AnimationBudget.h"
#include "AnimationDiagnostics.h"
#include "Animator.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelAnimationSampler.h"
#include "BoneComponent.h"
#include "CameraComponent.h"
#include "ClrHost.h"
#include "DataSystem.h"
#include "Object.h"
#include "RenderScene.h"
#include "RuntimeSettings.h"
#include "ReflectionYml.h"
#include "RuntimeFrame.h"
#include "AuthoringNodeViewAccess.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ScriptComponent.h"
#include "Socket.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <mathematics/transform.hpp>

namespace RenderTest
{
    namespace animation_probe
    {
        bool Near(const math::matrix4x4& a, const math::matrix4x4& b)
        {
            const auto* x = reinterpret_cast<const float*>(&a);
            const auto* y = reinterpret_cast<const float*>(&b);
            for (int i = 0; i < 16; ++i)
                if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || std::abs(x[i] - y[i]) > 0.002f)
                    return false;
            return true;
        }

        math::matrix4x4 ReferenceBlend(const math::matrix4x4& current,
            const math::matrix4x4& next, float alpha)
        {
            const auto a = math::decompose(current);
            const auto b = math::decompose(next);
            if (!a || !b) return current;
            return math::compose(math::lerp(a->scale, b->scale, alpha),
                math::slerp(a->rotation, b->rotation, alpha),
                math::lerp(a->translation, b->translation, alpha));
        }

        int Field(ScriptComponent& script, const char* name)
        {
            auto& clr = ClrHost::Get();
            for (int i = 0; i < clr.GetFieldCount(script.GetInstanceId()); ++i)
                if (clr.GetFieldName(script.GetInstanceId(), i) == name) return i;
            throw std::runtime_error(std::string("Managed probe field missing: ") + name);
        }

        struct Instance
        {
            Animator* animator{};
            ScriptComponent* script{};
            Entity* bone{};
            Entity* attachment{};
            int count{}, wrongThread{}, order{};
        };
    }

    bool RunAnimationPlaybackSelfTest(const std::string& modelPath, std::string& log)
    {
        using namespace animation_probe;
        Scene* scene = SceneManagers->GetActiveScene();
        auto* renderScene = SceneManagers->GetRenderScene();
        auto& clr = ClrHost::Get();
        if (!scene || !renderScene || !clr.IsReady() || !SceneManagers->m_isGameStart
            || SceneManagers->HasPendingSceneStructureChange() || SceneManagers->IsGamePaused())
        {
            log = "Requires committed, unpaused Play mode and ready CLR";
            return false;
        }
        const auto generation = DataSystems->LoadModelAssetGenerationByPath(modelPath);
        if (!generation || !generation->Skeleton() || generation->Animations().size() < 2)
        {
            log = "Requires a cached skinned model with at least two clips";
            return false;
        }
        auto& job = SceneManagers->GetAnimationScheduler();
        if (job.GetAnimatorCount() != 0)
        {
            log = "Requires an empty animation registry (use scene.new first)";
            return false;
        }
        const auto& skeleton = *generation->Skeleton();
        const auto& clip = generation->Animations()[0];
        const std::size_t boneCount = skeleton.bones.size();
        if (boneCount == 0 || boneCount > kMaxBones || clip.tracks.empty()
            || clip.durationTicks <= 0 || clip.ticksPerSecond <= 0)
        {
            log = "Unsupported model fixture";
            return false;
        }
        const auto observedBone = clip.tracks.back().bone;
        std::vector<EntityHandle> roots;
        std::vector<Instance> instances;
        Entity* ddolRoot{};
        int checks = 0;
        const auto require = [&](bool ok, const std::string& name)
        {
            if (!ok) throw std::runtime_error(name);
            ++checks;
        };
        const AnimationBudgetSettings savedBudget = RuntimeSettings::Get()
            .GetAnimationBudgetSettings();
        struct RestoreBudget final
        {
            AnimationBudgetSettings settings;
            ~RestoreBudget() { RuntimeSettings::Get().SetAnimationBudgetSettings(settings); }
        } restoreBudget{ savedBudget };
        const auto cleanup = [&]
        {
            Scene* active = SceneManagers->GetActiveScene();
            if (active)
            {
                if (scene == active)
                    for (const auto handle : roots)
                        if (active->Resolve(handle)) active->DestroyEntity(handle.index);
                if (ddolRoot && active->GetEntity("AnimationDdolProbe") == ddolRoot)
                {
                    SceneManagers->RemoveDontDestroyOnLoad(ddolRoot);
                    active->DestroyEntity(ddolRoot->m_index);
                    ddolRoot = nullptr;
                }
                active->EndFramePass();
            }
            clr.FlushRegistrations();
        };
        try
        {
        {
            animation::task_list recipe;
            const auto sample = recipe.append(animation::make_task(
                animation::task_kind::sample_clip));
            auto ik = animation::make_task(animation::task_kind::two_bone_ik);
            ik.dependency_a = sample;
            const auto correction = recipe.append(ik);
            auto output = animation::make_task(animation::task_kind::output);
            output.dependency_a = correction;
            recipe.set_output(recipe.append(output));
            animation::task_cost_model costs;
            costs.observe(animation::task_kind::sample_clip, 100., 100);
            costs.observe(animation::task_kind::two_bone_ik, 20., 100);
            costs.observe(animation::task_kind::output, 5., 100);
            const auto predict = [&](animation::quality_stage stage)
            { return costs.recipe_cost(recipe, stage, 100, 40, false, false, 2); };
            require(std::abs(predict(animation::quality_stage::l0) - 125.) < .001
                && std::abs(predict(animation::quality_stage::l1) - 105.) < .001,
                "Task-kind costs predict the surviving L1 recipe");
            animation::task_cost_model spiky;
            for (int frame = 0; frame < 10; ++frame)
                spiky.observe(animation::task_kind::sample_clip, 100., 100);
            spiky.observe(animation::task_kind::sample_clip, 100000., 100);
            require(spiky.estimate(animation::task_kind::sample_clip, 100, 100) < 110.,
                "A descheduled worker sample cannot multiply the next-frame EMA");
            std::uint16_t grace{};
            const auto tight = animation::choose_budget_stage(animation::quality_stage::l1,
                animation::quality_stage::l1, 110., grace, 2, .05, predict);
            const auto firstWide = animation::choose_budget_stage(animation::quality_stage::l1,
                tight.stage, 200., grace, 2, .05, predict);
            const auto secondWide = animation::choose_budget_stage(animation::quality_stage::l1,
                firstWide.stage, 200., grace, 2, .05, predict);
            require(tight.stage == animation::quality_stage::l1
                && firstWide.stage == animation::quality_stage::l1
                && secondWide.stage == animation::quality_stage::l0,
                "A larger budget promotes quality only after the grace interval");
            recipe.bypass_corrections();
            std::size_t reachableCorrections{};
            recipe.for_each_reachable([&](const animation::task& item)
            { reachableCorrections += item.kind == animation::task_kind::two_bone_ik; });
            require(reachableCorrections == 0,
                "Completed optional correction fade removes the IK task");
            require(animation::choose_quality_hysteretic({ .119f, true, true, false },
                    animation::quality_stage::l0, .05) == animation::quality_stage::l0
                && animation::choose_quality_hysteretic({ .10f, true, true, false },
                    animation::quality_stage::l0, .05) == animation::quality_stage::l1
                && animation::choose_quality_hysteretic({ .121f, true, true, false },
                    animation::quality_stage::l1, .05) == animation::quality_stage::l1
                && animation::choose_quality_hysteretic({ .13f, true, true, false },
                    animation::quality_stage::l1, .05) == animation::quality_stage::l0,
                "Projected-size boundaries use separate entry and exit thresholds");
            require(animation::choose_quality_hysteretic({ .121f, true, true, false },
                    animation::quality_stage::l6, .05) == animation::quality_stage::l0,
                "A newly near camera promotes across multiple quality stages immediately");
        }
            animation::task_list recipe;
            auto unused = animation::make_task(animation::task_kind::sample_clip);
            unused.clip_index = 11;
            (void)recipe.append(unused);
            auto selected = animation::make_task(animation::task_kind::sample_clip);
            selected.clip_index = 22;
            const auto selectedIndex = recipe.append(selected);
            auto nextSample = animation::make_task(animation::task_kind::sample_clip);
            nextSample.dependency_a = selectedIndex;
            nextSample.clip_index = 33;
            const auto nextIndex = recipe.append(nextSample);
            auto blendTask = animation::make_task(animation::task_kind::blend);
            blendTask.dependency_a = selectedIndex;
            blendTask.dependency_b = nextIndex;
            const auto blendIndex = recipe.append(blendTask);
            auto output = animation::make_task(animation::task_kind::output);
            output.dependency_a = blendIndex;
            recipe.set_output(recipe.append(output));
            std::vector<int> executedClips;
            recipe.for_each_reachable([&](const animation::task& task)
            {
                if (task.kind == animation::task_kind::sample_clip)
                    executedClips.push_back(task.clip_index);
            });
            require(executedClips == std::vector<int>{ 22, 33 },
                "Both blend inputs execute in order; unreachable sample does not");
            bool rejectedForwardDependency = false;
            try
            {
                auto invalid = animation::make_task(animation::task_kind::sample_clip);
                invalid.dependency_a = static_cast<std::uint32_t>(recipe.size());
                (void)recipe.append(invalid);
            }
            catch (const std::logic_error&)
            {
                rejectedForwardDependency = true;
            }
            require(rejectedForwardDependency,
                "Animation task dependency must precede its consumer");

            // Synthetic tracks pin the sampler policy independently of the real
            // model: empty defaults, key boundaries, Step and authored 3-axis scale.
            assets::ModelAnimationTrack synthetic;
            require(assets::animation::SampleLocalTransform(synthetic, 0.0).ToMatrix()
                == math::matrix4x4::identity(), "Empty TRS channel defaults to identity");
            synthetic.translations = { { 0.0, { 0.f, 0.f, 0.f } }, { 1.0, { 4.f, 6.f, 8.f } } };
            synthetic.scales = { { 0.0, { 1.f, 7.f, 9.f } }, { 1.0, { 3.f, 8.f, 10.f } } };
            require(Near(assets::animation::SampleLocalTransform(synthetic, 0.5).ToMatrix(),
                math::compose({ 2.f, 7.5f, 9.5f }, { 0.f, 0.f, 0.f, 1.f }, { 2.f, 3.f, 4.f })),
                "TRS linear sampler preserves authored 3-axis scale and translation");
            synthetic.translationInterpolation = assets::ModelInterpolationMode::Step;
            synthetic.scaleInterpolation = assets::ModelInterpolationMode::Step;
            require(assets::animation::SampleLocalTransform(synthetic, 0.999).ToMatrix()
                == math::compose({ 1.f, 7.f, 9.f }, { 0.f, 0.f, 0.f, 1.f }, {}),
                "TRS Step holds all scale axes before key boundary");
            require(Near(assets::animation::SampleLocalTransform(synthetic, 1.0).ToMatrix(),
                math::compose({ 3.f, 8.f, 10.f }, { 0.f, 0.f, 0.f, 1.f }, { 4.f, 6.f, 8.f })),
                "TRS Step selects exact terminal key");
            const auto verifyCachedTrack = [&](const assets::ModelAnimationTrack& track, double duration)
            {
                Animation::TrackKeyCursor cursor;
                const auto compare = [&](double time)
                {
                    const auto cached = assets::animation::SampleLocalTransform(track, time, cursor).ToMatrix();
                    const auto reference = assets::animation::SampleLocal(track, time);
                    require(std::memcmp(&cached, &reference, sizeof(cached)) == 0,
                        "Cursor sample is bit-identical to linear reference");
                };
                for (double progress : { 0.0, 0.1, 0.9, 0.5, 1.0, 0.0, 0.8, 0.2, 0.2 })
                    compare(progress * duration);
                const auto boundaries = [&](const auto& keys)
                {
                    for (const auto& key : keys)
                    {
                        compare(std::nextafter(key.time, -INFINITY));
                        compare(key.time);
                        compare(std::nextafter(key.time, INFINITY));
                    }
                };
                boundaries(track.translations);
                boundaries(track.rotations);
                boundaries(track.scales);
            };
            verifyCachedTrack({}, 1.0);
            synthetic.translations = { { 0.0, { 0.f, 0.f, 0.f } }, { 1.0, { 4.f, 6.f, 8.f } },
                { 3.0, { -2.f, 3.f, 5.f } }, { 6.0, { 9.f, 7.f, 2.f } } };
            synthetic.rotations = { { 0.0, { 0.f, 0.f, 0.f, 1.f } }, { 0.5, { 0.f, 0.f, 1.f, 0.f } },
                { 2.0, { 0.f, 0.f, 0.f, -1.f } }, { 6.0, { 0.f, 1.f, 0.f, 0.f } } };
            synthetic.scales = { { 0.0, { 1.f, 7.f, 9.f } }, { 2.0, { 0.f, 8.f, 10.f } },
                { 4.0, { -1.f, 4.f, 3.f } }, { 6.0, { 3.f, 3.f, 3.f } } };
            for (const auto mode : { assets::ModelInterpolationMode::Linear, assets::ModelInterpolationMode::Step })
            {
                synthetic.translationInterpolation = synthetic.rotationInterpolation = synthetic.scaleInterpolation = mode;
                verifyCachedTrack(synthetic, 6.0);
            }
            require(generation->AnimationTracks(-1).empty(), "Negative clip has no cached channels");
            require(generation->AnimationTracks(static_cast<int>(generation->Animations().size())).empty(),
                "Unavailable clip has no cached channels");
            const auto retired = AnimatorSystems->CreateInstance();
            const auto survivor = AnimatorSystems->CreateInstance();
            const AnimInstance* survivorRecord = AnimatorSystems->ResolveInstance(survivor);
            require(AnimatorSystems->ResolveInstance(retired) != nullptr
                && AnimatorSystems->ResolveInstance(survivor) != nullptr,
                "System instance handles resolve independently");
            AnimatorSystems->ResolveInstance(survivor)->timeElapsed = 3.5f;
            AnimatorSystems->ResolveInstance(survivor)->selectedClipIndex = 5;
            auto& survivorControl = AnimatorSystems->ResolveInstance(survivor)->control;
            survivorControl.blendT = 0.4f;
            survivorControl.nextClipIndex = 7;
            survivorControl.isBlending = true;
            survivorControl.stopTimer = 1.5f;
            survivorControl.stoppedDuration = 0.3f;
            AnimatorSystems->DestroyInstance(retired);
            require(AnimatorSystems->ResolveInstance(retired) == nullptr
                && AnimatorSystems->ResolveInstance(survivor) == survivorRecord
                && AnimatorSystems->ResolveInstance(survivor)->timeElapsed == 3.5f
                && AnimatorSystems->ResolveInstance(survivor)->selectedClipIndex == 5
                && AnimatorSystems->ResolveInstance(survivor)->control.blendT == 0.4f
                && AnimatorSystems->ResolveInstance(survivor)->control.nextClipIndex == 7
                && AnimatorSystems->ResolveInstance(survivor)->control.isBlending
                && AnimatorSystems->ResolveInstance(survivor)->control.stopTimer == 1.5f
                && AnimatorSystems->ResolveInstance(survivor)->control.stoppedDuration == 0.3f,
                "Dense compaction preserves the survivor's playback state");
            const auto reused = AnimatorSystems->CreateInstance();
            require(reused.slot == retired.slot && reused.generation != retired.generation
                && AnimatorSystems->ResolveInstance(retired) == nullptr
                && AnimatorSystems->ResolveInstance(reused)->timeElapsed == 0.f
                && AnimatorSystems->ResolveInstance(reused)->selectedClipIndex == 0
                && AnimatorSystems->ResolveInstance(reused)->control.blendT == 0.f
                && AnimatorSystems->ResolveInstance(reused)->control.nextClipIndex == -1
                && !AnimatorSystems->ResolveInstance(reused)->control.isBlending
                && AnimatorSystems->ResolveInstance(reused)->control.stopTimer == 0.f
                && AnimatorSystems->ResolveInstance(reused)->control.stoppedDuration == 0.f,
                "Recycled slots reject stale generations");
            AnimatorSystems->DestroyInstance(reused);
            AnimatorSystems->DestroyInstance(survivor);
            {
                Animator source;
                source.m_AnimIndex = 1;
                source.SetSelectedClipIndex(2);
                TwoBoneIKConstraint savedIK{};
                savedIK.StartBone = "UpperArm";
                savedIK.MiddleBone = "LowerArm";
                savedIK.EndBone = "Hand";
                savedIK.TargetWorld = { 1.f, 2.f, 3.f };
                savedIK.PoleWorld = { 0.f, 0.f, 1.f };
                savedIK.Weight = .6f;
                savedIK.Enabled = true;
                source.m_TwoBoneIKConstraints.push_back(savedIK);
                require(source.m_AnimIndexChosen == 0,
                    "Runtime clip selection does not use the serialized compatibility mirror");
                auto savedDocument = Meta::SerializeDocument(&source);
                const auto saved = savedDocument.Root().Read();
                require(saved["m_AnimIndexChosen"].As<int>() == 2
                    && saved["m_AnimIndex"].As<int>() == 1,
                    "Existing clip-selection YAML keys retain their values");
                Animator restored;
                Meta::Deserialize(&restored, saved);
                restored.OnDeserialized(Authoring::NodeViewAccess::Make(saved));
                require(restored.GetSelectedClipIndex() == 2 && restored.m_AnimIndex == 1,
                    "Legacy clip selection restores runtime and authored values independently");
                require(restored.m_TwoBoneIKConstraints.size() == 1
                    && restored.m_TwoBoneIKConstraints[0].StartBone == "UpperArm"
                    && restored.m_TwoBoneIKConstraints[0].EndBone == "Hand"
                    && restored.m_TwoBoneIKConstraints[0].Enabled
                    && restored.m_TwoBoneIKConstraints[0].Weight == .6f,
                    "Authored two-bone IK settings survive scene serialization");
                restored.m_AnimIndexChosen = 3;
                restored.OnPropertyChanged("m_AnimIndexChosen", Meta::PropertyChangeSource::Reflection);
                require(restored.GetSelectedClipIndex() == 3,
                    "Reflected clip selection updates instance playback");
                restored.SetSelectedClipIndex(4);
                auto roundtripDocument = Meta::SerializeDocument(&restored);
                require(roundtripDocument.Root().Read()["m_AnimIndexChosen"].As<int>() == 4,
                    "Serialization refreshes the compatibility mirror from live playback");
            }
            {
                Animator layerOwner;
                std::vector<std::shared_ptr<AnimationController>> layers;
                std::vector<std::uintptr_t> slotAddresses;
                layers.reserve(10);
                slotAddresses.reserve(10);
                for (int index = 0; index < 10; ++index)
                {
                    auto layer = std::make_shared<AnimationController>();
                    layer->m_owner = &layerOwner;
                    auto& playback = layer->GetPlayback();
                    playback.timeElapsed = static_cast<float>(index) + 0.5f;
                    playback.nextAnimationIndex = index;
                    layers.push_back(std::move(layer));
                    slotAddresses.push_back(reinterpret_cast<std::uintptr_t>(
                        layerOwner.GetInstance().controllerSlots.back()));
                }
                ControllerPlayback* firstLayer = &layers.front()->GetPlayback();
                ControllerPlayback* lastLayer = &layers.back()->GetPlayback();
                std::sort(slotAddresses.begin(), slotAddresses.end());
                std::size_t adjacentSlots = 0;
                for (std::size_t index = 1; index < slotAddresses.size(); ++index)
                    adjacentSlots += slotAddresses[index] - slotAddresses[index - 1]
                        == sizeof(AnimInstance::ControllerSlot);
                require(adjacentSlots >= 7 && &layers.front()->GetPlayback() == firstLayer
                    && &layers.back()->GetPlayback() == lastLayer,
                    "Controller playback records grow in contiguous pages without relocating live layers");
                ControllerPlayback* retiredLayer = &layers[1]->GetPlayback();
                layers[1].reset();
                layerOwner.GetInstance().PruneExpiredControllerPlayback();
                require(layerOwner.GetInstance().controllerSlots.size() == 9
                    && &layers.front()->GetPlayback() == firstLayer
                    && &layers.back()->GetPlayback() == lastLayer
                    && firstLayer->timeElapsed == 0.5f
                    && lastLayer->timeElapsed == 9.5f,
                    "Controller slot compaction preserves surviving playback addresses and state");
                auto replacement = std::make_shared<AnimationController>();
                replacement->m_owner = &layerOwner;
                auto& replacementPlayback = replacement->GetPlayback();
                require(&replacementPlayback == retiredLayer
                    && replacementPlayback.timeElapsed == 0.f
                    && replacementPlayback.nextAnimationIndex == -1,
                    "Recycled controller page record resets pending playback state");
                layers.clear();
                replacement.reset();
                layerOwner.GetInstance().PruneExpiredControllerPlayback();
                require(layerOwner.GetInstance().controllerSlots.empty()
                    && layerOwner.GetInstance().controllerSlotById.empty(),
                    "Expired controller playback releases every dense lookup entry");
            }
            AnimInstance oversized;
            oversized.ResizePose(kMaxBones + 3);
            require(oversized.pose.GetCount() == kMaxBones + 3
                && oversized.finalTransforms.size() == kMaxBones,
                "Pose retains all bones beyond the current skinning palette limit");
            for (std::size_t clipIndex = 0; clipIndex < generation->Animations().size(); ++clipIndex)
            {
                const auto tracks = generation->AnimationTracks(static_cast<int>(clipIndex));
                require(tracks.size() == boneCount, "Cached table uses this generation's bone count");
                std::vector<const assets::ModelAnimationTrack*> reference;
                assets::animation::BuildTrackTable(generation->Animations()[clipIndex], boneCount, reference);
                for (std::size_t bone = 0; bone < boneCount; ++bone)
                    require(tracks[bone] == reference[bone], "Cached clip/bone mapping matches source channels");
                for (const auto& track : generation->Animations()[clipIndex].tracks)
                    verifyCachedTrack(track, generation->Animations()[clipIndex].durationTicks);
            }
            // A skin palette advances even when no Scene bone is exposed.
            // Direct reads, explicit pins, editor selection and attachments
            // then promote only the required Transform projection.
            {
                Entity* owner = scene->CreateEntity("UnobservedAnimationProbe");
                Animator* animator = owner->AddComponent<Animator>();
                animator->m_Motion.m_guid = generation->Identity().modelId;
                animator->BindModelGeneration(generation);
                Entity* boneEntity = scene->CreateEntity(skeleton.bones[observedBone].name,
                    GameObjectType::Empty, owner->m_index);
                BoneComponent* bone = boneEntity->AddComponent<BoneComponent>();
                const std::size_t hiddenBone = (observedBone + 1) % boneCount;
                Entity* hiddenEntity = scene->CreateEntity(skeleton.bones[hiddenBone].name,
                    GameObjectType::Empty, boneEntity->m_index);
                hiddenEntity->AddComponent<BoneComponent>();
                Runtime::TickSimulationFrame(0.f);
                auto& pose = animator->GetInstance();
                pose.localTransforms[observedBone] = math::translation_matrix({ 1.f, 0.f, 0.f });
                pose.finalTransforms[observedBone] = math::translation_matrix({ 1.f, 0.f, 0.f });
                const auto first = scene->PublishAnimatorPose(*animator);
                require(first.validBones == 2 && first.observedBones == 0
                    && first.projectedBones == 0 && first.localWrites == 0
                    && first.paletteChanged, "Unobserved bone skips Scene projection but changes palette");
                pose.localTransforms[observedBone] = math::translation_matrix({ 2.f, 0.f, 0.f });
                pose.finalTransforms[observedBone] = math::translation_matrix({ 2.f, 0.f, 0.f });
                const auto second = scene->PublishAnimatorPose(*animator);
                require(second.localWrites == 0 && second.paletteChanged,
                    "Palette change is independent of bone local writes");
                const auto world = bone->GetWorldTransform();
                const auto noScaleWorld = boneEntity->Transform_().GetWorldMatrix_NoScale();
                const auto expectedWorld = pose.localTransforms[observedBone]
                    * owner->Transform_().GetWorldMatrix();
                AnimatorPoseUploadMetrics readPublish{};
                scene->TryGetLastAnimatorPoseMetrics(*animator, readPublish);
                const auto& pull = scene->GetLastSpatialPullMetrics();
                require(bone->m_runtimeObserved
                    && Near(world, expectedWorld)
                    && Near(noScaleWorld, expectedWorld)
                    && readPublish.observedBones == 0,
                    "Hidden bone read materializes its current world transform: promoted="
                    + std::to_string(bone->m_runtimeObserved)
                    + " actual=" + std::to_string(world.translation().x)
                    + " expected=" + std::to_string(expectedWorld.translation().x)
                    + " projected=" + std::to_string(readPublish.projectedBones)
                    + " writes=" + std::to_string(readPublish.localWrites)
                    + " fallback=" + std::to_string(readPublish.legacyFallback)
                    + " pullFallback=" + std::to_string(pull.legacyFallback)
                    + " pullWorldWrites=" + std::to_string(pull.worldWrites));
                bone->m_runtimeObserved = false;
                bone->m_bPinned = true;
                scene->SyncDerivedState();
                pose.localTransforms[observedBone] = math::translation_matrix({ 3.f, 0.f, 0.f });
                const auto pinned = scene->PublishAnimatorPose(*animator);
                require(pinned.observedBones == 1 && pinned.projectedBones == 1,
                    "Explicit bone pin enables projection");
                scene->SyncDerivedState();
                require(scene->GetLastSpatialResolveMetrics().worldWrites == 1,
                    "Observed parent resolve does not write hidden descendant world");
                bone->m_bPinned = false;
                scene->AddSelectedEntity(boneEntity);
                require(scene->PublishAnimatorPose(*animator).observedBones == 0,
                    "Live selection is not read before the locked capture");
                scene->CaptureSelectionForSimulation();
                require(scene->PublishAnimatorPose(*animator).observedBones == 1,
                    "Selected bone enables projection");
                scene->RemoveSelectedEntity(boneEntity);
                scene->CaptureSelectionForSimulation();
                scene->CreateEntity("ObservedAttachment", GameObjectType::Empty,
                    boneEntity->m_index);
                scene->SyncDerivedState();
                require(scene->PublishAnimatorPose(*animator).observedBones == 1,
                    "Non-bone child enables projection after topology change");
                const auto frozenPalette = pose.finalTransforms;
                const float beforeFreeze = pose.timeElapsed;
                pose.qualityOverride = 7;
                job.RequestHudCapture(animator->GetInstanceID());
                job.Update(0.1f);
                const auto frozenHud = job.GetHudSnapshot();
                require(frozenHud && frozenHud->registered == 1
                    && frozenHud->evaluated == 0 && frozenHud->degraded == 1
                    && frozenHud->stages[7] == 1
                    && frozenHud->animators.size() == 1
                    && frozenHud->animators[0].reason == "Quality override",
                    "S7 HUD exposes live degradation and its reason");
                require(pose.qualityStage == animation::quality_stage::l7
                    && pose.executedPoseSamples == 0
                    && pose.timeElapsed != beforeFreeze
                    && pose.finalTransforms == frozenPalette,
                    "L7 advances playback but freezes pose evaluation");
                pose.qualityOverride = -1;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.executedPoseSamples > 0,
                    "Revisible character evaluates L0 in its first frame");
                pose.qualityOverride = 7;
                animator->m_ForceFullQuality = true;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.executedPoseSamples > 0,
                    "Gameplay full-quality pin overrides offscreen freeze");
                const auto activeTracks = generation->AnimationTracks(0);
                std::size_t ikEnd = boneCount;
                for (std::size_t candidate = 0; candidate < boneCount; ++candidate)
                {
                    const std::uint32_t middle = skeleton.bones[candidate].parent;
                    if (middle >= boneCount) continue;
                    const std::uint32_t start = skeleton.bones[middle].parent;
                    if (start >= boneCount || !activeTracks[start]
                        || !activeTracks[middle] || !activeTracks[candidate]) continue;
                    const auto a = pose.poseGlobals[start].translation();
                    const auto b = pose.poseGlobals[middle].translation();
                    const auto c = pose.poseGlobals[candidate].translation();
                    if (math::length(b - a) < .01f || math::length(c - b) < .01f
                        || math::length(c - a) < .05f) continue;
                    ikEnd = candidate;
                    break;
                }
                require(ikEnd < boneCount, "Fixture has a tracked two-bone chain");
                const auto ikMiddle = skeleton.bones[ikEnd].parent;
                const auto ikStart = skeleton.bones[ikMiddle].parent;
                const math::vector3 originalEnd = pose.poseGlobals[ikEnd].translation();
                const math::vector3 chainStart = pose.poseGlobals[ikStart].translation();
                const math::vector3 chainMiddle = pose.poseGlobals[ikMiddle].translation();
                const math::vector3 target = chainStart + (originalEnd - chainStart) * .8f;
                const float beforeIK = math::length(originalEnd - target);
                TwoBoneIKConstraint constraint{};
                constraint.StartBone = skeleton.bones[ikStart].name;
                constraint.MiddleBone = skeleton.bones[ikMiddle].name;
                constraint.EndBone = skeleton.bones[ikEnd].name;
                const auto ownerWorld = owner->Transform_().GetWorldMatrix();
                constraint.TargetWorld = math::transform_point(target, ownerWorld);
                constraint.PoleWorld = math::transform_point(
                    chainMiddle + math::vector3{ .1f, .2f, .3f }, ownerWorld);
                constraint.Enabled = true;
                animator->m_TwoBoneIKConstraints.push_back(constraint);
                auto* ikSocket = new Socket();
                ikSocket->m_ObjectName = constraint.EndBone;
                ikSocket->AttachObject(scene->CreateEntity("IKSocketAttachment",
                    GameObjectType::Empty, owner->m_index));
                animator->socketvec.push_back(ikSocket);
                job.Update(0.f);
                const auto solvedEnd = pose.poseGlobals[ikEnd].translation();
                require(math::length(solvedEnd - target) < beforeIK * .8f,
                    "Two-bone IK moves the end toward its target: before="
                    + std::to_string(beforeIK) + " after="
                    + std::to_string(math::length(solvedEnd - target)));
                require(Near(pose.finalTransforms[ikEnd],
                    skeleton.bones[ikEnd].inverseBindMatrix * pose.poseGlobals[ikEnd]
                        * skeleton.globalInverseTransform)
                    && Near(ikSocket->m_boneMatrix,
                        pose.poseGlobals[ikEnd] * ownerWorld),
                    "IK skin palette and socket share the corrected pose");
                animator->m_TwoBoneIKConstraints[0].Required = true;
                pose.qualityOverride = 7;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && math::length(pose.poseGlobals[ikEnd].translation() - target)
                        < beforeIK * .8f,
                    "Gameplay-required IK keeps full quality outside the camera policy");
                animator->m_TwoBoneIKConstraints[0].Required = false;
                animator->m_ForceFullQuality = false;
                const float fadeSampleTime = pose.timeElapsed;
                pose.qualityOverride = 0;
                pose.timeElapsed = fadeSampleTime;
                job.Update(.05f);
                std::array<math::vector3, 4> fadeOut{};
                fadeOut[0] = pose.poseGlobals[ikEnd].translation();
                pose.qualityOverride = 1;
                for (int step = 1; step <= 3; ++step)
                {
                    pose.timeElapsed = fadeSampleTime;
                    job.Update(.05f);
                    int ikTasks = 0;
                    pose.taskList.for_each_reachable([&](const animation::task& task)
                    {
                        if (task.kind == animation::task_kind::two_bone_ik) ++ikTasks;
                    });
                    require(pose.qualityStage == animation::quality_stage::l1
                        && std::abs(pose.optionalIKWeight - (1.f - step / 3.f)) < .001f
                        && ikTasks == (step < 3 ? 1 : 0)
                        && pose.ikSnapshots.size() == (step < 3 ? 1u : 0u),
                        "L1 fades optional IK and removes its task only at zero weight");
                    fadeOut[step] = pose.poseGlobals[ikEnd].translation();
                }
                const float fadeTravel = math::length(fadeOut[0] - fadeOut[3]);
                require(fadeTravel > .01f, "L1 IK fade exercises a visible correction");
                for (int step = 1; step <= 3; ++step)
                    require(math::length(fadeOut[step] - fadeOut[step - 1])
                        < fadeTravel * .75f + .01f,
                        "L1 IK fade has no single-frame end-bone snap");
                animator->m_TwoBoneIKConstraints[0].Required = true;
                pose.qualityOverride = 7;
                pose.timeElapsed = fadeSampleTime;
                job.Update(.05f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.ikSnapshots.size() == 1
                    && pose.ikSnapshots[0].weight == 1.f,
                    "Required IK bypasses an optional L1 fade and offscreen override");
                animator->m_TwoBoneIKConstraints[0].Required = false;
                pose.qualityOverride = 1;
                pose.timeElapsed = fadeSampleTime;
                job.Update(.05f);
                require(pose.ikSnapshots.empty(),
                    "Optional IK remains omitted after the required pin is released");
                pose.qualityOverride = 0;
                for (int step = 1; step <= 3; ++step)
                {
                    pose.timeElapsed = fadeSampleTime;
                    job.Update(.05f);
                    require(std::abs(pose.optionalIKWeight - step / 3.f) < .001f
                        && pose.ikSnapshots.size() == 1,
                        "L0 restores optional IK over three frames");
                }
                require(math::length(pose.poseGlobals[ikEnd].translation() - fadeOut[0])
                    < .01f, "L0 returns to the same fully corrected pose");
                pose.qualityOverride = -1;
                animator->m_TwoBoneIKConstraints[0].Enabled = false;
                pose.timeElapsed = fadeSampleTime;
                job.Update(0.f);
                require(math::length(pose.poseGlobals[ikEnd].translation() - originalEnd)
                    < .01f, "Disabled IK restores the sampled pose");
                // Keep the S4 camera-policy regression at its maximum allowed
                // degradation while S5's separate cost/budget checks exercise
                // promotion under a larger budget.
                AnimationBudgetSettings constrained = savedBudget;
                constrained.cpuBudgetMs = .001;
                RuntimeSettings::Get().SetAnimationBudgetSettings(constrained);
                pose.qualityOverride = -1;
                Entity* farCamera = scene->CreateEntity("SignificanceFarCamera");
                farCamera->Transform_().SetPosition({ 0.f, 0.f, -50.f });
                farCamera->AddComponent<CameraComponent>();
                Runtime::TickSimulationFrame(0.f); // OnAddedToScene registers new cameras.
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l3
                    && pose.projectedHeight > 0.f,
                    "Distant visible character drops optional overlay recipes: stage="
                    + std::to_string(static_cast<int>(pose.qualityStage))
                    + " height=" + std::to_string(pose.projectedHeight)
                    + " cameras=" + std::to_string(scene->Cameras().GetCount()));
                animator->m_TwoBoneIKConstraints[0].Enabled = true;
                for (int frame = 0; frame < 3; ++frame) job.Update(.05f);
                require(pose.qualityStage == animation::quality_stage::l3
                    && pose.optionalIKWeight == 0.f && pose.ikSnapshots.empty(),
                    "Camera-selected quality removes optional IK after its fade");
                animator->m_TwoBoneIKConstraints[0].Enabled = false;
                Entity* nearCamera = scene->CreateEntity("SignificanceNearCamera");
                nearCamera->Transform_().SetPosition({ 0.f, 0.f, -10.f });
                auto* nearCameraComponent = nearCamera->AddComponent<CameraComponent>();
                Runtime::TickSimulationFrame(0.f);
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0,
                    "Multiple cameras retain the highest projected significance");
                nearCamera->Transform_().SetPosition({ 0.f, 0.f, 10.f });
                farCamera->Transform_().SetPosition({ 0.f, 0.f, 50.f });
                scene->SyncDerivedState();
                job.Update(0.1f);
                require(pose.qualityStage == animation::quality_stage::l7
                    && pose.executedPoseSamples == 0,
                    "Character behind all cameras freezes its pose: stage="
                    + std::to_string(static_cast<int>(pose.qualityStage))
                    + " height=" + std::to_string(pose.projectedHeight)
                    + " nearZ=" + std::to_string(nearCameraComponent->CaptureFrameSnapshot().eyePosition.z)
                    + " cameras=" + std::to_string(scene->Cameras().GetCount()));
                nearCamera->Transform_().SetPosition({ 0.f, 0.f, -10.f });
                scene->SyncDerivedState();
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.executedPoseSamples > 0,
                    "Actual camera revisibility restores L0 in its first frame");
                nearCameraComponent->GetCamera()->m_isOrthographic = true;
                nearCameraComponent->GetCamera()->m_viewWidth = 2.f;
                nearCameraComponent->GetCamera()->m_viewHeight = 2.f;
                nearCamera->Transform_().SetPosition({ 10.f, 0.f, -10.f });
                scene->SyncDerivedState();
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l7,
                    "Orthographic side clipping freezes an offscreen pose");
                nearCamera->Transform_().SetPosition({ 0.f, 0.f, -10.f });
                scene->SyncDerivedState();
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.executedPoseSamples > 0,
                    "Orthographic revisibility restores full pose evaluation");
                nearCameraComponent->GetCamera()->m_isOrthographic = false;
                nearCamera->Transform_().SetPosition({ 0.f, 0.f, -400.f });
                farCamera->Transform_().SetPosition({ 0.f, 0.f, -400.f });
                animator->m_LowDetailBoneCount = observedBone;
                scene->SyncDerivedState();
                const auto beforeLowDetail = pose.localTransforms[observedBone];
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l5
                    && pose.blendRecoveryActive
                    && Near(pose.localTransforms[observedBone], beforeLowDetail),
                    "Automatic L6 entry bridges into the bind-local L5 pose first");
                for (int frame = 0; frame < 3; ++frame) job.Update(.05f);
                require(!pose.blendRecoveryActive,
                    "Automatic L5 entry finishes its 150 ms pose bridge");
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l6
                    && pose.projectedHeight < .005f
                    && pose.l6Interval == 4 && pose.executedPoseSamples == 1,
                    "Tiny visible character selects L6 with a four-frame interval");
                for (int frame = 0; frame < 3; ++frame)
                {
                    job.Update(0.f);
                    require(pose.executedPoseSamples == 0,
                        "L6 four-frame interval skips intermediate samples");
                }
                job.Update(0.f);
                require(pose.executedPoseSamples == 1,
                    "L6 four-frame interval reevaluates the fourth tick");
                scene->DestroyEntity(nearCamera->m_index);
                scene->DestroyEntity(farCamera->m_index);
                scene->DestroyEntity(owner->m_index);
                scene->EndFramePass();
                require(job.GetAnimatorCount() == 0,
                    "Isolated observation fixture unregisters before product actors");
                RuntimeSettings::Get().SetAnimationBudgetSettings(savedBudget);
            }
            const AnimInstance* firstRecord = nullptr;
            for (int i = 0; i < 100; ++i)
            {
                auto* owner = scene->CreateEntity("AnimationPlayback_" + std::to_string(i));
                roots.push_back(scene->HandleOf(owner->m_index));
                Instance instance;
                instance.animator = owner->AddComponent<Animator>();
                auto& animator = *instance.animator;
                if (i == 0) firstRecord = &animator.GetInstance();
                animator.m_Motion.m_guid = generation->Identity().modelId;
                animator.BindModelGeneration(generation);
                auto controller = std::make_shared<AnimationController>();
                controller->m_owner = &animator;
                controller->CreateState("Any", -1, true);
                controller->CreateState("Current", 0);
                controller->CheckTransition();
                require(controller->GetCurrentState() && controller->GetAnimationIndex() == 0,
                    "AnyState must not become the initial playable state");
                animator.m_animationControllers.push_back(controller);
                instance.script = owner->AddComponent<ScriptComponent>();
                instance.script->m_scriptType = "AnimationPlaybackProbe";
                instance.script->EnsureInstance();
                require(instance.script->HasInstance(), "Managed instance creation");
                instance.count = Field(*instance.script, "Count");
                instance.wrongThread = Field(*instance.script, "Wrong Thread");
                instance.order = Field(*instance.script, "Order");
                auto& events = animator.EnsureClipOverride(0).events;
                KeyFrameEvent first, second;
                first.key = 0.25f; first.m_funName = "Quarter";
                second.key = 0.75f; second.m_funName = "ThreeQuarter";
                events = { second, first }; // authored order differs from playback order
                instance.bone = scene->CreateEntity(skeleton.bones[observedBone].name,
                    GameObjectType::Empty, owner->m_index);
                instance.bone->AddComponent<BoneComponent>();
                instance.attachment = scene->CreateEntity("Attachment", GameObjectType::Empty, owner->m_index);
                auto* socket = new Socket();
                socket->m_ObjectName = skeleton.bones[observedBone].name;
                socket->AttachObject(instance.attachment);
                animator.socketvec.push_back(socket);
                instances.push_back(instance);
            }
            std::vector<std::uintptr_t> recordAddresses;
            recordAddresses.reserve(instances.size());
            for (const auto& item : instances)
                recordAddresses.push_back(reinterpret_cast<std::uintptr_t>(&item.animator->GetInstance()));
            std::sort(recordAddresses.begin(), recordAddresses.end());
            std::size_t adjacentRecords = 0;
            for (std::size_t index = 1; index < recordAddresses.size(); ++index)
                adjacentRecords += recordAddresses[index] - recordAddresses[index - 1] == sizeof(AnimInstance);
            require(adjacentRecords >= 63
                && &instances.front().animator->GetInstance() == firstRecord,
                "Hundred animator records occupy contiguous pages without relocating the first record");
            Runtime::TickSimulationFrame(0.f);
            require(job.GetAnimatorCount() == 100, "All 100 animators registered through lifecycle");
            instances[0].animator->SetAnimation(1);
            require(instances[0].animator->GetSelectedClipIndex() == 1
                && instances[0].animator->m_AnimIndex == 1
                && instances[0].animator->m_AnimIndexChosen == 0,
                "Authored clip selection updates instance playback before serialization");
            instances[0].animator->SetAnimation(0);

            const auto resetMessages = [&]
            {
                for (const auto& item : instances)
                {
                    clr.SetFieldInt32(item.script->GetInstanceId(), item.count, 0);
                    clr.SetFieldString(item.script->GetInstanceId(), item.order, "");
                }
            };
            const auto messages = [&](const std::string& expected)
            {
                for (const auto& item : instances)
                {
                    const auto id = item.script->GetInstanceId();
                    require(clr.GetFieldInt32(id, item.count) == static_cast<int>(expected.size()),
                        "Managed callback count expected=" + std::to_string(expected.size())
                        + " actual=" + std::to_string(clr.GetFieldInt32(id, item.count)));
                    require(clr.GetFieldString(id, item.order) == expected, "Managed callback order");
                    require(clr.GetFieldInt32(id, item.wrongThread) == 0, "Managed callback thread");
                }
            };
            const auto setup = [&](double progress, float speed, bool loop)
            {
                resetMessages();
                for (const auto& item : instances)
                {
                    auto& controller = *item.animator->m_animationControllers[0];
                    controller.GetPlayback().timeElapsed = static_cast<float>(progress * clip.durationTicks);
                    controller.GetCurrentState()->animationSpeed = speed;
                    item.animator->SetClipLooping(0, loop);
                }
            };
            const auto seconds = [&](double cycles)
            { return static_cast<float>(cycles * clip.durationTicks / clip.ticksPerSecond); };

            setup(0, 1, true);
            job.RequestHudCapture(instances[0].animator->GetInstanceID());
            job.Update(seconds(0.5));
            const auto hud = job.GetHudSnapshot();
            require(hud && hud->registered == 100 && hud->evaluated == 100
                && hud->degraded == 0 && hud->stages[0] == 100
                && hud->animators.size() == 100
                && hud->selectedAnimatorId == instances[0].animator->GetInstanceID(),
                "S7 HUD reports the live 100-animator frame and selected instance");
            require(!hud->tasks.empty() && hud->workerPosePool != 0
                && hud->workerCurrentStorage != 0 && hud->instancePoseStorage != 0
                && hud->workerPoseBuffers >= 1 && hud->instancePoseBuffers == 1,
                "S7 task snapshot records recipe and actual pose buffer ownership");
            std::uint32_t executedTasks = 0;
            std::uint32_t executedSamples = 0;
            for (const auto& task : hud->tasks)
            {
                require(!task.bufferOwner.empty(), "S7 task snapshot names buffer ownership");
                if (task.executionOrder == animation::invalid_task)
                {
                    require(!task.reachable, "S7 reachable task must have an execution record");
                    continue;
                }
                require(task.reachable && task.executionOrder == executedTasks++,
                    "S7 execution order is recorded by the worker in recipe order");
                executedSamples += task.kind == animation::task_kind::sample_clip ? 1 : 0;
            }
            require(executedTasks > 0 && executedSamples == 1,
                "S7 snapshot proves the inactive AnyState branch was not sampled");
            for (const auto& item : instances)
            {
                const auto& instance = item.animator->GetInstance();
                require(instance.localTransforms.size() == boneCount
                    && instance.finalTransforms.size() == boneCount
                    && instance.pose.GetCount() == boneCount,
                    "System pose buffers use actual skeleton bone count");
                require(instance.executedPoseSamples == 1,
                    "Inactive AnyState branch executes no clip sample");
            }
            messages(""); // workers must only enqueue, even though they have finished
            Runtime::TickSimulationFrame(0.f); // actual RuntimeFrame post-physics flush
            messages("A");
            setup(0, 1, true);
            Runtime::TickSimulationFrame(seconds(3.5));
            messages("ABABABA");
            setup(0.9, -1, true);
            Runtime::TickSimulationFrame(seconds(2.5));
            messages("BABAB");
            setup(0.4, 0, true);
            Runtime::TickSimulationFrame(seconds(3));
            messages("");
            setup(0, 1, false);
            Runtime::TickSimulationFrame(seconds(4));
            messages("AB");
            Runtime::TickSimulationFrame(seconds(1));
            messages("AB"); // terminal event cannot repeat on subsequent frames

            // Shared immutable generation, distinct per-instance times. Compare the
            // parallel product palette with sequential samples of the same clip.
            setup(0, 1, true);
            std::array<math::matrix4x4, kMaxBones> expected;
            assets::animation::Pose referencePose;
            std::vector<const math::matrix4x4*> scratchStorage;
            std::vector<const void*> outputStorage;
            std::vector<const Animation::TrackKeyCursor*> keyStorage;
            for (const auto& item : instances)
            {
                scratchStorage.push_back(item.animator->GetInstance().poseGlobals.data());
                const auto& instance = item.animator->GetInstance();
                require(instance.GetAllocatedPoseBufferCount() == 1
                    && instance.workerPoseBufferCount >= 1
                    && instance.workerPoseBufferCount <= 3
                    && instance.workerPosePoolIdentity != 0,
                    "A worker owns sample scratch while the instance retains only its output pose");
                outputStorage.push_back(instance.pose.GetTranslations().data());
                require(instance.workerCurrentStorage[0] != nullptr
                    && instance.workerCurrentStorage[1] != nullptr
                    && instance.workerCurrentStorage[2] != nullptr,
                    "Output transfer returns the old instance storage to its worker");
                keyStorage.push_back(item.animator->GetInstance().samplingCursors[0][0].GetTracks().data());
            }
            for (int round = 0; round < 12; ++round)
            {
                for (std::size_t i = 0; i < instances.size(); ++i)
                {
                    instances[i].animator->m_animationControllers[0]->GetPlayback().timeElapsed =
                        static_cast<float>(clip.durationTicks * ((round % 2 ? 99 - i : i) + round * 0.31) / 120.0);
                    instances[i].animator->m_animationControllers[0]->GetCurrentState()->animationSpeed =
                        round % 4 == 1 ? -1.f : round % 4 == 2 ? 0.f : 1.f;
                }
                job.Update(seconds(0.01));
                for (std::size_t i = 0; i < instances.size(); ++i)
                {
                    require(instances[i].animator->GetInstance().poseGlobals.data() == scratchStorage[i],
                        "Warmed evaluation reuses its instance's globals storage");
                    const auto& instance = instances[i].animator->GetInstance();
                    const void* published = instance.pose.GetTranslations().data();
                    require(published != outputStorage[i]
                        && instance.workerCurrentStorage[0] == outputStorage[i],
                        "Output task transfers worker TRS ownership and recycles old output");
                    outputStorage[i] = published;
                    require(instances[i].animator->GetInstance().samplingCursors[0][0].GetTracks().data() == keyStorage[i],
                        "Warmed playback reuses key cursor storage");
                }
                for (const auto& item : instances)
                {
                    auto& animator = *item.animator;
                    const auto savedCursor = animator.GetInstance().samplingCursors[0][0].GetTracks()[observedBone];
                    const auto savedComposite = animator.GetInstance().pose.GetTransform(observedBone).ToMatrix();
                    const auto savedSelection = animator.GetSelectedClipIndex();
                    require(job.EvaluateGenerationPose(animator, *generation, 0,
                        animator.m_animationControllers[0]->GetPlayback().timeElapsed, expected.data()), "Sequential sample");
                    require(animator.GetSelectedClipIndex() == savedSelection,
                        "Diagnostic sample restores the live clip selection");
                    require(animator.GetInstance().pose.GetTransform(observedBone).ToMatrix() == savedComposite,
                        "Diagnostic sample does not change the live composite pose");
                    const auto liveCursor = animator.GetInstance().samplingCursors[0][0].GetTracks()[observedBone];
                    require(savedCursor.m_translation == liveCursor.m_translation
                        && savedCursor.m_rotation == liveCursor.m_rotation && savedCursor.m_scale == liveCursor.m_scale,
                        "Diagnostic evaluation leaves live key cursors unchanged");
                    // Independent sampler still builds its own table. Comparing only
                    // two calls through the cached product path would hide a bad bake.
                    assets::animation::EvaluatePose(skeleton, clip,
                        animator.m_animationControllers[0]->GetPlayback().timeElapsed, referencePose);
                    for (std::size_t b = 0; b < boneCount; ++b)
                    {
                        require(Near(expected[b], animator.GetInstance().finalTransforms[b]), "Parallel palette isolation");
                        if (referencePose.hasTrack[b])
                            require(Near(referencePose.finals[b], animator.GetInstance().finalTransforms[b]),
                                "Cached product pose matches uncached reference");
                    }
                    require(Near(item.bone->Transform_().GetLocalMatrix(), animator.GetInstance().localTransforms[observedBone]),
                        "Scene bone publication");
                }
            }
            clr.FlushScriptMessages();

            auto& animator = *instances[0].animator;
            // Restrict later checks to one actor without removing registry entries.
            for (std::size_t i = 1; i < instances.size(); ++i) instances[i].animator->SetEnabled(false);
            auto first = animator.m_animationControllers[0];
            {
                auto& pose = animator.GetInstance();
                auto& playback = first->GetPlayback();
                const auto* currentTrack = generation->AnimationTracks(0)[observedBone];
                const auto* nextTrack = generation->AnimationTracks(1)[observedBone];
                require(currentTrack && nextTrack, "L4 fixture has both transition channels");
                const auto currentLocal = assets::animation::SampleLocalTransform(*currentTrack, 0.0);
                const auto nextLocal = assets::animation::SampleLocalTransform(*nextTrack, 0.0);
                const float savedSpeed = first->GetCurrentState()->animationSpeed;
                first->GetCurrentState()->animationSpeed = 0.f;
                playback.timeElapsed = 0.f;
                playback.nextTimeElapsed = 0.f;
                playback.nextAnimationIndex = 1;
                playback.isBlending = true;
                pose.control.blendT = .25f;
                pose.qualityOverride = 4;
                job.Update(0.f);
                int l4Samples = 0;
                int l4Blends = 0;
                pose.taskList.for_each_reachable([&](const animation::task& task)
                {
                    l4Samples += task.kind == animation::task_kind::sample_clip;
                    l4Blends += task.kind == animation::task_kind::blend;
                });
                require(pose.qualityStage == animation::quality_stage::l4
                    && pose.executedPoseSamples == 1
                    && l4Samples == 1 && l4Blends == 0
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(), currentLocal.ToMatrix()),
                    "L4 keeps only the current sample below the transition midpoint");
                pose.control.blendT = .75f;
                job.Update(0.f);
                require(pose.executedPoseSamples == 1
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(), nextLocal.ToMatrix()),
                    "L4 keeps only the next sample above the transition midpoint");
                const auto snapped = pose.pose.GetTransform(observedBone).ToMatrix();
                pose.qualityOverride = 3;
                job.Update(0.f);
                const auto targetLocal = Animation::Blend(currentLocal, nextLocal, .75f);
                const float startError = math::length(
                    pose.pose.GetTransform(observedBone).m_translation - targetLocal.m_translation);
                require(pose.executedPoseSamples == 2
                    && pose.blendRecoveryActive
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(), snapped),
                    "L4 promotion starts from the published snapped pose");
                for (int step = 0; step < 3; ++step)
                {
                    playback.nextTimeElapsed = 0.f;
                    job.Update(.05f);
                    if (step == 0 && startError > .001f)
                    {
                        const float error = math::length(
                            pose.pose.GetTransform(observedBone).m_translation
                                - targetLocal.m_translation);
                        require(error < startError && error > 0.f,
                            "L4 promotion moves partway toward the full crossfade");
                    }
                }
                require(!pose.blendRecoveryActive
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(),
                        targetLocal.ToMatrix()),
                    "L4 promotion converges to the full crossfade in 150 ms");
                playback.isBlending = false;
                playback.nextAnimationIndex = -1;
                pose.qualityOverride = 4;
                job.Update(0.f);
                require(!pose.l4SnappedBlend && pose.executedPoseSamples == 1,
                    "L4 without a transition keeps the ordinary single sample");
                pose.qualityOverride = 3;
                job.Update(0.f);
                require(!pose.blendRecoveryActive,
                    "L4 without a snapped blend needs no promotion recovery");
                first->GetCurrentState()->animationSpeed = savedSpeed;
                pose.qualityOverride = -1;
                job.Update(0.f);
                require(observedBone > 0, "L5 fixture needs a nonempty bone prefix");
                const std::uint32_t fullBoneSamples = pose.evaluatedBoneSamples;
                animator.m_LowDetailBoneCount = observedBone;
                first->GetCurrentState()->animationSpeed = 0.f;
                playback.timeElapsed = static_cast<float>(clip.durationTicks * .15);
                pose.qualityOverride = 5;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l5
                    && pose.activeBoneCount == observedBone
                    && pose.evaluatedBoneSamples < fullBoneSamples
                    && pose.bindLocals.size() == boneCount
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(),
                        pose.bindLocals[observedBone].ToMatrix())
                    && pose.poseGlobals.size() == boneCount,
                    "L5 samples only the authored prefix and keeps full bind-local FK");
                const auto lowDetailLocal = pose.pose.GetTransform(observedBone).ToMatrix();
                pose.qualityOverride = 4;
                job.Update(0.f);
                require(pose.blendRecoveryActive
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(), lowDetailLocal),
                    "L5 promotion begins at the published bind-local pose");
                for (int step = 0; step < 3; ++step) job.Update(.05f);
                require(!pose.blendRecoveryActive
                    && Near(pose.pose.GetTransform(observedBone).ToMatrix(),
                        assets::animation::SampleLocalTransform(*currentTrack,
                            playback.timeElapsed).ToMatrix()),
                    "L5 promotion reaches the authored animated bone in 150 ms");
                std::uint32_t movingBone = observedBone;
                const auto l6Tracks = generation->AnimationTracks(0);
                for (std::uint32_t bone = 0; bone < observedBone; ++bone)
                {
                    const auto* track = l6Tracks[bone];
                    if (track && !Near(
                        assets::animation::SampleLocalTransform(*track, 0.).ToMatrix(),
                        assets::animation::SampleLocalTransform(*track,
                            clip.durationTicks * .2).ToMatrix()))
                    {
                        movingBone = bone;
                        break;
                    }
                }
                require(movingBone < observedBone,
                    "L6 fixture has an animated bone in the authored prefix");
                first->GetCurrentState()->animationSpeed = 1.f;
                playback.timeElapsed = 0.f;
                pose.qualityOverride = 6;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l6
                    && pose.l6Interval == 2 && pose.executedPoseSamples == 1,
                    "L6 seeds a full pose on its first tick");
                const auto l6StartMatrix = pose.localTransforms[movingBone];
                job.Update(seconds(.1));
                require(pose.executedPoseSamples == 0
                    && Near(pose.localTransforms[movingBone], l6StartMatrix)
                    && std::abs(playback.timeElapsed - clip.durationTicks * .1) < .001,
                    "L6 advances playback without evaluating on an intermediate tick");
                job.Update(seconds(.1));
                const auto l6End = assets::animation::SampleLocalTransform(
                    *l6Tracks[movingBone], clip.durationTicks * .2);
                math::matrix4x4 l6HalfMatrix{};
                const auto l6EndMatrix = l6End.ToMatrix();
                for (int row = 0; row < 4; ++row)
                    for (int column = 0; column < 4; ++column)
                        l6HalfMatrix(row, column) = (l6StartMatrix(row, column)
                            + l6EndMatrix(row, column)) * .5f;
                require(pose.executedPoseSamples == 1
                    && Near(pose.localTransforms[movingBone], l6HalfMatrix),
                    "L6 interpolates the two evaluated local matrices");
                job.Update(seconds(.1));
                require(pose.executedPoseSamples == 0
                    && Near(pose.localTransforms[movingBone], l6EndMatrix)
                    && std::abs(playback.timeElapsed - clip.durationTicks * .3) < .001,
                    "L6 publishes the latest evaluated pose on the skipped tick");
                pose.qualityOverride = 5;
                playback.timeElapsed = 0.f;
                job.Update(0.f);
                pose.qualityOverride = 6;
                job.Update(0.f);
                job.Update(seconds(.1));
                job.Update(seconds(.1));
                const auto l6PublishedMidpoint = pose.localTransforms[movingBone];
                pose.qualityOverride = 0;
                first->GetCurrentState()->animationSpeed = 0.f;
                job.Update(0.f);
                require(Near(pose.localTransforms[movingBone], l6PublishedMidpoint),
                    "L6 promotion starts at the interpolated visible pose");
                for (int step = 0; step < 3; ++step) job.Update(.05f);
                require(!pose.blendRecoveryActive
                    && std::abs(pose.optionalIKWeight - 1.f) < .001f,
                    "Full-quality pose and optional correction weight recover: active="
                    + std::to_string(pose.blendRecoveryActive)
                    + " weight=" + std::to_string(pose.optionalIKWeight));
                const auto uncorrected = pose.pose.GetTransform(movingBone);
                BoneTransformConstraint correction{};
                correction.Bone = skeleton.bones[movingBone].name;
                correction.TranslationOffset = { 0.f, .2f, 0.f };
                correction.Enabled = true;
                animator.m_BoneTransformConstraints.push_back(correction);
                job.Update(0.f);
                require(pose.boneTransformSnapshots.size() == 1
                    && std::abs(pose.pose.GetTransform(movingBone).m_translation.y
                        - uncorrected.m_translation.y - .2f) < .002f,
                    "L0 BoneTransform corrects the authored local bone");
                pose.qualityOverride = 1;
                for (int step = 0; step < 3; ++step) job.Update(.05f);
                require(pose.boneTransformSnapshots.empty()
                    && Near(pose.pose.GetTransform(movingBone).ToMatrix(),
                        uncorrected.ToMatrix()),
                    "L1 fades and removes the optional BoneTransform task");
                animator.m_BoneTransformConstraints[0].Required = true;
                pose.qualityOverride = 6;
                job.Update(0.f);
                require(pose.qualityStage == animation::quality_stage::l0
                    && pose.boneTransformSnapshots.size() == 1,
                    "Required BoneTransform pins the animator to L0");
                require(Meta::SerializeDocument(&animator).Dump().find(
                    "m_BoneTransformConstraints") != std::string::npos,
                    "BoneTransform authoring survives scene serialization");
                animator.m_BoneTransformConstraints.clear();
                animator.m_LowDetailBoneCount = 0;
                first->GetCurrentState()->animationSpeed = savedSpeed;
                pose.qualityOverride = -1;
                job.Update(0.f);
            }
            auto second = std::make_shared<AnimationController>();
            second->m_owner = &animator;
            second->CreateState("Layer", 1);
            second->CheckTransition();
            require(first->GetCurrentState() == first->m_curState
                && second->GetCurrentState() == second->m_curState
                && first->GetCurrentState() != second->GetCurrentState(),
                "Controller FSM selection is isolated in each instance playback slot");
            second->m_avatarMask = new AvatarMask();
            second->m_avatarMask->RootMask = animator.BuildAvatarBoneMasks(*second->m_avatarMask);
            second->m_avatarMask->useAll = true;
            animator.m_animationControllers.push_back(second);
            job.Update(seconds(0.1));
            std::vector<animation::task_kind> layerTasks;
            animator.GetInstance().taskList.for_each_reachable([&](const animation::task& task)
            {
                if (task.kind == animation::task_kind::prepare_composite
                    || task.kind == animation::task_kind::blend_masked
                    || task.kind == animation::task_kind::materialize_composite)
                    layerTasks.push_back(task.kind);
            });
            require(layerTasks == std::vector<animation::task_kind>{
                animation::task_kind::prepare_composite,
                animation::task_kind::blend_masked,
                animation::task_kind::blend_masked,
                animation::task_kind::materialize_composite },
                "Both live layers compose in authored order before final materialization");
            std::vector<animation::task_kind> interleavedTasks;
            animator.GetInstance().taskList.for_each_reachable([&](const animation::task& task)
            {
                interleavedTasks.push_back(task.kind);
            });
            require(interleavedTasks == std::vector<animation::task_kind>{
                animation::task_kind::prepare_composite,
                animation::task_kind::sample_clip,
                animation::task_kind::materialize,
                animation::task_kind::blend_masked,
                animation::task_kind::sample_clip,
                animation::task_kind::materialize,
                animation::task_kind::blend_masked,
                animation::task_kind::materialize_composite,
                animation::task_kind::output },
                "Each layer consumes shared pose scratch before the next sample");
            require(animator.GetInstance().executedPoseSamples == 2,
                "Two active controller branches execute one clip sample each");
            auto* firstPlayback = &first->GetPlayback();
            auto* secondPlayback = &second->GetPlayback();
            const auto* baseTrack = generation->AnimationTracks(0)[observedBone];
            const auto* additiveTrack = generation->AnimationTracks(1)[observedBone];
            require(baseTrack != nullptr && additiveTrack != nullptr,
                "Layer fixture requires both authored channels");
            const auto firstLocal = [&]
            {
                return assets::animation::SampleLocalTransform(*baseTrack,
                    first->GetPlayback().timeElapsed);
            };
            require(Near(animator.GetInstance().localTransforms[observedBone],
                assets::animation::SampleLocalTransform(*additiveTrack,
                    second->GetPlayback().timeElapsed).ToMatrix()),
                "Last enabled layer wins");
            require(animator.GetInstance().GetAllocatedPoseBufferCount()
                + animator.GetInstance().workerPoseBufferCount <= 7,
                "Layer composition retains at most three S4 recovery/interpolation poses");
            {
                auto& layeredPose = animator.GetInstance();
                animator.m_LowDetailBoneCount = observedBone;
                layeredPose.qualityOverride = 4;
                job.Update(0.f);
                require(layeredPose.qualityStage == animation::quality_stage::l4
                    && layeredPose.executedPoseSamples == 1,
                    "L4 prunes overlay controllers and evaluates only the base clip");
                layeredPose.qualityOverride = 5;
                job.Update(0.f);
                const auto layeredBind = layeredPose.pose.GetTransform(observedBone);
                require(layeredPose.qualityStage == animation::quality_stage::l5
                    && layeredPose.activeBoneCount == observedBone
                    && layeredPose.executedPoseSamples == 1
                    && Near(layeredBind.ToMatrix(),
                        layeredPose.bindLocals[observedBone].ToMatrix()),
                    "L5 retains bind locals through a composite recipe");
                layeredPose.qualityOverride = 3;
                job.Update(0.f);
                require(layeredPose.blendRecoveryActive
                    && Near(layeredPose.pose.GetTransform(observedBone).ToMatrix(),
                        layeredBind.ToMatrix()),
                    "Composite promotion begins at the published L5 pose");
                for (int step = 0; step < 3; ++step) job.Update(.05f);
                require(!layeredPose.blendRecoveryActive
                    && Near(layeredPose.pose.GetTransform(observedBone).ToMatrix(),
                        firstLocal().ToMatrix()),
                    "Composite promotion converges to the base clip");
                layeredPose.qualityOverride = 6;
                job.Update(0.f);
                require(layeredPose.qualityStage == animation::quality_stage::l6
                    && layeredPose.executedPoseSamples == 1,
                    "L6 seeds a composite output pose");
                job.Update(0.f);
                require(layeredPose.executedPoseSamples == 0
                    && layeredPose.poseGlobals.size() == boneCount,
                    "L6 interpolates a composite output without evaluating a clip");
                layeredPose.qualityOverride = -1;
                animator.m_LowDetailBoneCount = 0;
                job.Update(0.f);
            }
            const float firstTimeBeforeReorder = firstPlayback->timeElapsed;
            const float secondTimeBeforeReorder = secondPlayback->timeElapsed;
            std::vector<std::shared_ptr<AnimationController>> extraLayers;
            for (int index = 0; index < 4; ++index)
            {
                auto extra = std::make_shared<AnimationController>();
                extra->m_owner = &animator;
                extra->CreateState("Extra", 1);
                extra->CheckTransition();
                animator.m_animationControllers.push_back(extra);
                extraLayers.push_back(std::move(extra));
            }
            job.Update(0.f);
            require(animator.GetInstance().executedPoseSamples == 6
                && animator.GetInstance().GetAllocatedPoseBufferCount()
                    + animator.GetInstance().workerPoseBufferCount <= 7
                && Near(animator.GetInstance().localTransforms[observedBone],
                    assets::animation::SampleLocalTransform(*additiveTrack, 0.0).ToMatrix()),
                "Six layers share the same pose scratch and preserve authored order");
            animator.m_animationControllers.resize(2);
            extraLayers.clear();
            job.Update(0.f);
            require(animator.GetInstance().controllerSlots.size() == 2,
                "Retired extra layers release their playback slots");
            second->m_additive = true;
            job.Update(0.f);
            std::vector<animation::task_kind> additiveTasks;
            animator.GetInstance().taskList.for_each_reachable([&](const animation::task& task)
            {
                if (task.kind == animation::task_kind::prepare_composite
                    || task.kind == animation::task_kind::blend_masked
                    || task.kind == animation::task_kind::make_additive
                    || task.kind == animation::task_kind::apply_additive
                    || task.kind == animation::task_kind::materialize_composite)
                    additiveTasks.push_back(task.kind);
            });
            require(additiveTasks == std::vector<animation::task_kind>{
                animation::task_kind::prepare_composite,
                animation::task_kind::blend_masked,
                animation::task_kind::make_additive,
                animation::task_kind::apply_additive,
                animation::task_kind::materialize_composite },
                "Additive overlay executes delta creation before weighted application");
            const auto additiveSource = assets::animation::SampleLocalTransform(*additiveTrack,
                second->GetPlayback().timeElapsed);
            const auto additiveReference = assets::animation::SampleLocalTransform(*additiveTrack, 0.0);
            const auto additiveExpected = Animation::ApplyAdditive(
                firstLocal(),
                Animation::MakeAdditive(additiveSource, additiveReference), 1.f).ToMatrix();
            require(Near(animator.GetInstance().localTransforms[observedBone], additiveExpected),
                "Additive layer applies its clip-start delta over the base layer");
            require(Meta::SerializeDocument(&animator).Dump().find("m_additive") != std::string::npos,
                "Additive layer mode is serialized");
            job.Update(0.f);
            require(Near(animator.GetInstance().localTransforms[observedBone], additiveExpected),
                "Additive layer does not accumulate across repeated frames");
            second->m_additive = false;
            job.Update(0.f);
            std::swap(animator.m_animationControllers[0], animator.m_animationControllers[1]);
            job.Update(0.f);
            require(&first->GetPlayback() == firstPlayback && &second->GetPlayback() == secondPlayback
                && firstPlayback->timeElapsed == firstTimeBeforeReorder
                && secondPlayback->timeElapsed == secondTimeBeforeReorder
                && firstPlayback->currentState == first->m_curState
                && secondPlayback->currentState == second->m_curState,
                "Controller playback and FSM remain attached to identity after layer reorder");
            require(animator.GetInstance().samplingCursors[0][0].GetClipIndex() == 1
                && animator.GetInstance().samplingCursors[1][0].GetClipIndex() == 0,
                "Reordered evaluation slots bind cursors to their current clips");
            require(Near(animator.GetInstance().localTransforms[observedBone], firstLocal().ToMatrix()),
                "Reordered layers retain authored selection order");
            {
                auto detached = animator.m_animationControllers.back();
                animator.m_animationControllers.pop_back();
                job.Update(0.f);
                require(&detached->GetPlayback() == firstPlayback
                    && animator.GetInstance().controllerSlots.size() == 2,
                    "Live detached controller retains playback during another layer's tick");
                animator.m_animationControllers.push_back(detached);
            }
            job.Update(0.f);
            require(&first->GetPlayback() == firstPlayback,
                "Reattached controller resumes its original playback slot");
            std::swap(animator.m_animationControllers[0], animator.m_animationControllers[1]);
            second->useController = false;
            job.Update(seconds(0.1));
            std::size_t reachableMaskTasks = 0;
            animator.GetInstance().taskList.for_each_reachable([&](const animation::task& task)
            {
                if (task.kind == animation::task_kind::blend_masked) ++reachableMaskTasks;
            });
            require(reachableMaskTasks == 1,
                "Disabled layer records no masked composition task");
            require(animator.GetInstance().executedPoseSamples == 1,
                "Disabled controller branch executes no clip sample");
            require(Near(animator.GetInstance().localTransforms[observedBone], firstLocal().ToMatrix()),
                "Disabled layer cannot reuse stale staging");
            second->useController = true;
            first->m_useLayer = false;
            second->m_avatarMask->useAll = false;
            second->m_avatarMask->useUpper = false;
            second->m_avatarMask->useLower = false;
            const auto retained = animator.GetInstance().localTransforms[observedBone];
            job.Update(seconds(0.1));
            require(animator.GetInstance().executedPoseSamples == 1,
                "Enabled but non-contributing layer skips clip sampling");
            require(Near(retained, animator.GetInstance().localTransforms[observedBone]), "All-masked local retention");
            require(animator.GetInstance().layerMaskCaches.size() == 2
                && animator.GetInstance().layerMaskCaches[1].m_weights.size() == boneCount
                && animator.GetInstance().layerMaskCaches[1].m_weights[observedBone] == 0.f,
                "Humanoid mask bakes dense zero weight after direct flag edits");
            BoneMask* omittedMask = second->m_avatarMask->m_BoneMasks.back();
            second->m_avatarMask->m_BoneMasks.pop_back();
            require(!animator.ConvertLegacyAvatarMask(*second->m_avatarMask)
                && second->m_avatarMask->isHumanoid,
                "Incomplete legacy mask remains readable without partial conversion");
            second->m_avatarMask->m_BoneMasks.push_back(omittedMask);
            require(animator.ConvertLegacyAvatarMask(*second->m_avatarMask)
                && !second->m_avatarMask->isHumanoid,
                "Legacy humanoid mask converts to named weights");
            job.Update(0.f);
            require(animator.GetInstance().layerMaskCaches[1].m_weights[observedBone] == 0.f
                && Near(retained, animator.GetInstance().localTransforms[observedBone]),
                "Converted mask preserves the selected pose");
            require(Near(retained, instances[0].bone->Transform_().GetLocalMatrix()), "Masked Scene projection");

            // An unavailable clip has no channels: it must not publish the last
            // evaluated staging matrix when its mask is re-enabled.
            second->CreateState("NoChannels", static_cast<int>(generation->Animations().size()));
            second->SetCurState("NoChannels");
            second->m_avatarMask->useAll = true;
            job.Update(seconds(0.1));
            require(Near(retained, animator.GetInstance().localTransforms[observedBone]), "No-channel layer retention");

            first->m_useLayer = true;
            first->m_avatarMask = new AvatarMask();
            first->m_avatarMask->isHumanoid = false;
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                auto* mask = new BoneMask();
                mask->boneName = skeleton.bones[b].name;
                mask->isEnabled = b != observedBone;
                first->m_avatarMask->m_BoneMasks.push_back(mask);
            }
            job.Update(seconds(0.1));
            require(Near(retained, animator.GetInstance().localTransforms[observedBone]), "Named-mask local retention");
            std::vector<math::matrix4x4> maskedGlobals(boneCount, skeleton.rootTransform);
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                const auto parent = skeleton.bones[b].parent;
                maskedGlobals[b] = animator.GetInstance().localTransforms[b]
                    * (parent < b ? maskedGlobals[parent] : skeleton.rootTransform);
            }
            require(Near(animator.GetInstance().finalTransforms[observedBone],
                skeleton.bones[observedBone].inverseBindMatrix * maskedGlobals[observedBone]
                    * skeleton.globalInverseTransform), "Retained local follows current parent in palette");
            require(Near(animator.socketvec[0]->m_boneMatrix,
                maskedGlobals[observedBone] * animator.GetOwner()->Transform_().GetWorldMatrix()),
                "Masked socket follows composite globals");

            auto* observedMask = first->m_avatarMask->m_BoneMasks[observedBone];
            observedMask->isEnabled = true;
            observedMask->weight = 0.5f;
            job.Update(0.f);
            const auto partialExpected = Animation::BlendMasked(Animation::LocalTransform{},
                firstLocal(), 0.5f).ToMatrix();
            require(animator.GetInstance().layerMaskCaches[0].m_weights[observedBone] == 0.5f
                && Near(animator.GetInstance().localTransforms[observedBone], partialExpected),
                "Named partial weight blends the sampled local pose");
            job.Update(0.f);
            require(Near(animator.GetInstance().localTransforms[observedBone], partialExpected),
                "Partial layer remains stable across repeated frames");
            observedMask->weight = 1.f;
            job.Update(0.f);
            require(animator.GetInstance().layerMaskCaches[0].m_weights[observedBone] == 1.f
                && Near(animator.GetInstance().localTransforms[observedBone], firstLocal().ToMatrix()),
                "Direct named-mask enable refreshes dense weight and local selection");
            observedMask->boneName = "missing-observed-bone";
            job.Update(0.f);
            require(animator.GetInstance().layerMaskCaches[0].m_weights[observedBone] == 0.f,
                "Direct named-mask rename invalidates dense weight");
            observedMask->boneName = skeleton.bones[observedBone].name;
            job.Update(0.f);
            require(animator.GetInstance().layerMaskCaches[0].m_weights[observedBone] == 1.f,
                "Restored named-mask name refreshes dense weight");
            BoneMask* firstMask = first->m_avatarMask->m_BoneMasks[0];
            first->m_avatarMask->m_BoneMasks[0] = nullptr;
            job.Update(0.f);
            require(animator.GetInstance().layerMaskCaches[0].m_weights[observedBone] == 0.f,
                "Null mask entry preserves first-null legacy lookup rule");
            first->m_avatarMask->m_BoneMasks[0] = firstMask;
            job.Update(0.f);

            // Single-controller blended pose must also update socket staging/commit.
            animator.m_animationControllers.resize(1);
            second.reset();
            job.Update(0.f);
            require(animator.GetInstance().controllerSlots.size() == 1
                && &first->GetPlayback() == firstPlayback
                && firstPlayback->currentState == first->m_curState,
                "Destroyed controller releases only its playback slot");
            first->GetCurrentState()->animationSpeed = 1.f;
            first->GetPlayback().timeElapsed = 0.f;
            animator.StopAnimation(seconds(0.5));
            job.Update(seconds(0.2));
            require(first->GetPlayback().timeElapsed == 0.f
                && animator.GetPlaybackControl().stopTimer > 0.f
                && animator.GetPlaybackControl().stoppedDuration == seconds(0.2),
                "Paused playback retains its instance-owned accumulated time");
            job.Update(seconds(0.4));
            require(std::abs(first->GetPlayback().timeElapsed
                    - (seconds(0.2) + seconds(0.4)) * static_cast<float>(clip.ticksPerSecond)) < 0.002f
                && animator.GetPlaybackControl().stopTimer <= 0.f
                && animator.GetPlaybackControl().stoppedDuration == 0.f,
                "Resumed playback applies accumulated time once");
            first->DeleteAvatarMask();
            first->m_useLayer = true;
            first->CreateState("Next", 1);
            first->CreateTransition("Current", "Next");
            first->UpdateState();
            require(first->IsBlending() && first->GetNextAnimationIndex() == 1
                && firstPlayback->currentState == first->m_curState
                && firstPlayback->nextState == first->FindState("Next")
                && firstPlayback->currentTransition != nullptr
                && firstPlayback->needBlend
                && animator.GetPlaybackControl().isBlending
                && animator.GetPlaybackControl().nextClipIndex == 1,
                "Real controller transition shares instance-owned blend control");
            first->m_curState = nullptr;
            const std::string savedController = Meta::SerializeDocument(&animator).Dump();
            require(first->m_curState == firstPlayback->currentState
                && savedController.find("m_curState") != std::string::npos,
                "Legacy current-state YAML key mirrors the live instance during serialization");
            const auto currentTracks = generation->AnimationTracks(0);
            const auto nextTracks = generation->AnimationTracks(1);
            std::vector<math::matrix4x4> blendGlobals(boneCount);
            for (float progress : { 0.f, 0.17f, 0.63f, 0.99f })
            {
                first->GetPlayback().timeElapsed = static_cast<float>(clip.durationTicks * progress);
                first->GetPlayback().nextTimeElapsed = static_cast<float>(generation->Animations()[1].durationTicks * progress);
                for (float alpha : { 0.f, 0.25f, 0.5f, 0.75f, 1.f })
                {
                    animator.GetPlaybackControl().blendT = alpha;
                    job.Update(0.f);
                    for (std::size_t b = 0; b < boneCount; ++b)
                    {
                        const auto parent = skeleton.bones[b].parent;
                        const auto parentGlobal = parent < b ? blendGlobals[parent] : skeleton.rootTransform;
                        blendGlobals[b] = parentGlobal;
                        if (!currentTracks[b]) continue;
                        auto local = assets::animation::SampleLocal(*currentTracks[b], first->GetPlayback().timeElapsed);
                        if (nextTracks[b]) local = ReferenceBlend(local,
                            assets::animation::SampleLocal(*nextTracks[b], first->GetPlayback().nextTimeElapsed), alpha);
                        blendGlobals[b] = local * parentGlobal;
                        require(Near(local, animator.GetInstance().localTransforms[b]), "TRS product blend matches matrix reference");
                        require(Near(skeleton.bones[b].inverseBindMatrix * blendGlobals[b]
                            * skeleton.globalInverseTransform, animator.GetInstance().finalTransforms[b]),
                            "TRS blended FK and palette match matrix reference");
                    }
                    const auto referenceSocket = blendGlobals[observedBone]
                        * animator.GetOwner()->Transform_().GetWorldMatrix();
                    require(Near(referenceSocket, animator.socketvec[0]->m_boneMatrix), "TRS blended socket matches reference");
                }
            }
            require(animator.GetInstance().GetAllocatedPoseBufferCount()
                + animator.GetInstance().workerPoseBufferCount <= 7,
                "Transition and prior multi-layer playback retain bounded S4 poses");
            animator.GetPlaybackControl().blendT = 0.5f;
            animator.socketvec[0]->m_boneMatrix = math::matrix4x4{};
            job.Update(seconds(0.1));
            std::vector<math::matrix4x4> globals(boneCount, skeleton.rootTransform);
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                const auto parent = skeleton.bones[b].parent;
                globals[b] = animator.GetInstance().localTransforms[b] * (parent < b ? globals[parent] : skeleton.rootTransform);
            }
            const auto socketExpected = globals[observedBone] * animator.GetOwner()->Transform_().GetWorldMatrix();
            require(Near(socketExpected, animator.socketvec[0]->m_boneMatrix), "Blended socket staging");
            require(Near(socketExpected, animator.socketvec[0]->transform.GetLocalMatrix()), "Blended socket commit");
            const auto position = instances[0].attachment->Transform_().GetLocalMatrix().translation();
            const auto socketPosition = socketExpected.translation();
            require(math::length(position - socketPosition) < 0.002f, "Attached object follows blended socket");
            require(animator.GetInstance().samplingCursors[0][0].GetClipIndex() == 0
                && animator.GetInstance().samplingCursors[0][1].GetClipIndex() == 1
                && animator.GetInstance().samplingCursors[0][0].GetTracks().data() != animator.GetInstance().samplingCursors[0][1].GetTracks().data(),
                "Transition current and next clips own independent key cursors");
            animator.BindModelGeneration(nullptr);
            require(animator.GetInstance().layerTrackTables.empty() && animator.GetInstance().layerMaskCaches.empty()
                && animator.GetInstance().poseGlobals.empty()
                && animator.GetInstance().bindLocals.empty()
                && !animator.GetInstance().l6Seeded
                && animator.GetInstance().l6PreviousPose.GetCount() == 0
                && animator.GetInstance().l6LatestPose.GetCount() == 0
                && animator.GetInstance().workerPoseBufferCount == 0
                && animator.GetInstance().workerPosePoolIdentity == 0,
                "Unbinding clears all borrowed channel views and pose scratch");
            for (const auto& slot : animator.GetInstance().samplingCursors)
                for (const auto& cursor : slot)
                    require(cursor.GetTracks().empty() && cursor.GetClipIndex() == -1, "Generation unbind clears key cursors");
            animator.BindModelGeneration(generation);
            first->GetPlayback().isBlending = false;
            job.Update(0.f);
            assets::animation::EvaluatePose(skeleton, clip, first->GetPlayback().timeElapsed, referencePose);
            for (std::size_t b = 0; b < boneCount; ++b)
                if (referencePose.hasTrack[b])
                    require(Near(referencePose.finals[b], animator.GetInstance().finalTransforms[b]), "Pose after generation rebind");
            first->DeleteTransiton("Current", "Next");
            require(firstPlayback->currentTransition == nullptr
                && firstPlayback->nextState == nullptr
                && firstPlayback->nextAnimationIndex == -1
                && !firstPlayback->needBlend
                && !animator.GetPlaybackControl().isBlending,
                "Removing the active transition clears instance-owned pending FSM state");
            first->DeleteState("Missing");
            first->DeleteState("Next");
            require(first->GetCurrentState() == first->FindState("Current")
                && first->m_curState == first->GetCurrentState(),
                "Removing another state preserves the active instance selection");
            AnimationState speedState(first.get(), "SpeedHandleProbe");
            speedState.animationSpeedParameterName = "SpeedBinding";
            ConditionParameter* speedParameter = animator.AddDefaultParameter(ValueType::Float);
            const std::size_t speedIndex = animator.Parameters.size() - 1;
            speedParameter->name = "SpeedBinding";
            speedParameter->fValue = 2.f;
            animator.NotifyParameterLayoutChanged();
            speedState.UpdateAnimationSpeed();
            require(speedState.m_speedParameterIndex == speedIndex
                && speedState.multiplerAnimationSpeed == 2.f,
                "Speed parameter name resolves to a stable index");
            speedParameter->fValue = 3.f;
            speedState.UpdateAnimationSpeed();
            require(speedState.m_speedParameterIndex == speedIndex
                && speedState.multiplerAnimationSpeed == 3.f,
                "Speed parameter value uses the cached handle");
            animator.DeleteParameter(static_cast<int>(speedIndex));
            speedState.UpdateAnimationSpeed();
            require(speedState.m_speedParameterIndex == static_cast<std::size_t>(-1),
                "Deleted speed parameter invalidates its handle");
            speedParameter = animator.AddDefaultParameter(ValueType::Float);
            speedParameter->name = "SpeedBinding";
            speedParameter->fValue = 4.f;
            animator.NotifyParameterLayoutChanged();
            speedState.UpdateAnimationSpeed();
            require(speedState.multiplerAnimationSpeed == 4.f,
                "Recreated speed parameter binds without a stale pointer");
            animator.DeleteParameter(static_cast<int>(animator.Parameters.size() - 1));
            clr.FlushScriptMessages();
            cleanup();
            require(job.GetAnimatorCount() == 0, "Lifecycle unregisters all probe animators");
            roots.clear();

            // A DDOL Animator keeps its component and playback record when the
            // scene changes, but must be registered with the new scene's job.
            ddolRoot = scene->CreateEntity("AnimationDdolProbe");
            auto* ddolAnimator = ddolRoot->AddComponent<Animator>();
            ddolAnimator->m_Motion.m_guid = generation->Identity().modelId;
            ddolAnimator->BindModelGeneration(generation);
            auto ddolController = std::make_shared<AnimationController>();
            ddolController->m_owner = ddolAnimator;
            ddolController->CreateState("Current", 0);
            ddolController->CheckTransition();
            ddolAnimator->m_animationControllers.push_back(ddolController);
            Runtime::TickSimulationFrame(0.f);
            require(job.GetAnimatorCount() == 1 && AnimatorSystems->GetCount() == 1,
                "DDOL animator enters both animation registries");
            Animator detachedAnimator;
            std::array<bool, 5> offOwnerRejected{};
            std::thread offOwner([&]
            {
                const auto rejected = [](auto&& call)
                {
                    try { call(); }
                    catch (const std::logic_error&) { return true; }
                    return false;
                };
                offOwnerRejected[0] = rejected([&] { job.RegisterAnimator(ddolAnimator); });
                offOwnerRejected[1] = rejected([&] { job.UnregisterAnimator(ddolAnimator); });
                offOwnerRejected[2] = rejected([&] { job.Update(0.f); });
                offOwnerRejected[3] = rejected([&] { detachedAnimator.OnAddedToScene(); });
                offOwnerRejected[4] = rejected([&] { ddolAnimator->OnRemovingFromScene(); });
            });
            offOwner.join();
            const bool registriesUnchanged = job.GetAnimatorCount() == 1
                && AnimatorSystems->GetCount() == 1;
            job.UnregisterAnimator(&detachedAnimator);
            AnimatorSystems->Unregister(&detachedAnimator);
            require(std::ranges::all_of(offOwnerRejected, [](bool value) { return value; })
                && registriesUnchanged,
                "Off-owner calls cannot change animation registries or run jobs");
            AnimInstance* savedInstance = &ddolAnimator->GetInstance();
            ControllerPlayback* savedPlayback = &ddolController->GetPlayback();
            savedPlayback->timeElapsed = 1.f;
            Object::SetDontDestroyOnLoad(ddolRoot);
            require(ddolRoot->IsDontDestroyOnLoad(), "DDOL actor is marked for scene transfer");

            Scene* nextScene = Scene::CreateNewScene("AnimationDdolDestination");
            SceneManagers->ActivateScene(nextScene, true);
            scene = nullptr; // The old scene is deleted during the switch.
            SceneManagers->BeforeAwakeSceneLoad();
            scene = SceneManagers->GetActiveScene();
            Entity* movedRoot = scene->GetEntity("AnimationDdolProbe");
            require(movedRoot == ddolRoot && movedRoot->GetComponent<Animator>() == ddolAnimator,
                "DDOL actor and Animator survive the scene switch");
            require(&ddolAnimator->GetInstance() == savedInstance
                && &ddolController->GetPlayback() == savedPlayback
                && savedPlayback->timeElapsed == 1.f,
                "DDOL Animator preserves instance and controller playback");
            require(job.GetAnimatorCount() == 1 && AnimatorSystems->GetCount() == 1,
                "DDOL Animator reenters both animation registries");
            job.Update(0.f);
            require(ddolAnimator->GetInstance().finalTransforms.size() == boneCount,
                "DDOL Animator publishes a pose after scene transfer");
            cleanup();
            ddolController.reset();
            require(job.GetAnimatorCount() == 0 && AnimatorSystems->GetCount() == 0,
                "DDOL Animator unregisters after final destruction");
            log = "ANIMATION_PRODUCT_OK actors=100 rounds=12 checks=" + std::to_string(checks)
                + " managedThreadErrors=0 model=" + generation->Name();
            return true;
        }
        catch (const std::exception& e)
        {
            cleanup();
            log = "ANIMATION_PRODUCT_FAILED checks=" + std::to_string(checks) + " reason=" + e.what();
            return false;
        }
    }
}
