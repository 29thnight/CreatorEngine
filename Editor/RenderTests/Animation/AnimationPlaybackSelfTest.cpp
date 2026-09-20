#include "Animation/AnimationPlaybackSelfTest.h"
#include "AnimationJob.h"
#include "Animator.h"
#include "Assets/ModelAssetGeneration.h"
#include "BoneComponent.h"
#include "ClrHost.h"
#include "DataSystem.h"
#include "RenderScene.h"
#include "RuntimeFrame.h"
#include "Scene.h"
#include "SceneManager.h"
#include "ScriptComponent.h"
#include "Socket.h"
#include <array>
#include <cmath>
#include <stdexcept>
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
        auto& job = renderScene->GetAnimationJob();
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
        int checks = 0;
        const auto require = [&](bool ok, const std::string& name)
        {
            if (!ok) throw std::runtime_error(name);
            ++checks;
        };
        const auto cleanup = [&]
        {
            for (const auto handle : roots)
                if (scene->Resolve(handle)) scene->DestroyEntity(handle.index);
            scene->EndFramePass();
            clr.FlushRegistrations();
        };
        try
        {
            for (int i = 0; i < 100; ++i)
            {
                auto* owner = scene->CreateEntity("AnimationPlayback_" + std::to_string(i));
                roots.push_back(scene->HandleOf(owner->m_index));
                Instance instance;
                instance.animator = owner->AddComponent<Animator>();
                auto& animator = *instance.animator;
                animator.m_Motion.m_guid = generation->Identity().modelId;
                animator.BindModelGeneration(generation);
                auto controller = std::make_shared<AnimationController>();
                controller->m_owner = &animator;
                controller->CreateState("Any", -1, true);
                controller->CreateState("Current", 0);
                controller->CheckTransition();
                require(controller->m_curState && controller->GetAnimationIndex() == 0,
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
            Runtime::TickSimulationFrame(0.f);
            require(job.GetAnimatorCount() == 100, "All 100 animators registered through lifecycle");

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
                    controller.m_timeElapsed = static_cast<float>(progress * clip.durationTicks);
                    controller.m_curState->animationSpeed = speed;
                    item.animator->SetClipLooping(0, loop);
                }
            };
            const auto seconds = [&](double cycles)
            { return static_cast<float>(cycles * clip.durationTicks / clip.ticksPerSecond); };

            setup(0, 1, true);
            job.Update(seconds(0.5));
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
            for (int round = 0; round < 12; ++round)
            {
                for (std::size_t i = 0; i < instances.size(); ++i)
                    instances[i].animator->m_animationControllers[0]->m_timeElapsed =
                        static_cast<float>(clip.durationTicks * (i + round * 0.31) / 120.0);
                job.Update(seconds(0.01));
                for (const auto& item : instances)
                {
                    auto& animator = *item.animator;
                    require(job.EvaluateGenerationPose(animator, *generation, 0,
                        animator.m_animationControllers[0]->m_timeElapsed, expected.data()), "Sequential sample");
                    for (std::size_t b = 0; b < boneCount; ++b)
                        require(Near(expected[b], animator.m_FinalTransforms[b]), "Parallel palette isolation");
                    require(Near(item.bone->Transform_().GetLocalMatrix(), animator.m_localTransforms[observedBone]),
                        "Scene bone publication");
                }
            }
            clr.FlushScriptMessages();

            auto& animator = *instances[0].animator;
            // Restrict later checks to one actor without removing registry entries.
            for (std::size_t i = 1; i < instances.size(); ++i) instances[i].animator->SetEnabled(false);
            auto first = animator.m_animationControllers[0];
            auto second = std::make_shared<AnimationController>();
            second->m_owner = &animator;
            second->CreateState("Layer", 1);
            second->CheckTransition();
            second->m_avatarMask = new AvatarMask();
            second->m_avatarMask->useAll = true;
            animator.m_animationControllers.push_back(second);
            job.Update(seconds(0.1));
            require(Near(animator.m_localTransforms[observedBone], second->m_LocalTransforms[observedBone]),
                "Last enabled layer wins");
            second->useController = false;
            job.Update(seconds(0.1));
            require(Near(animator.m_localTransforms[observedBone], first->m_LocalTransforms[observedBone]),
                "Disabled layer cannot reuse stale staging");
            second->useController = true;
            first->m_useLayer = false;
            second->m_avatarMask->useAll = false;
            second->m_avatarMask->useUpper = false;
            second->m_avatarMask->useLower = false;
            const auto retained = animator.m_localTransforms[observedBone];
            job.Update(seconds(0.1));
            require(Near(retained, animator.m_localTransforms[observedBone]), "All-masked local retention");
            require(Near(retained, instances[0].bone->Transform_().GetLocalMatrix()), "Masked Scene projection");

            // An unavailable clip has no channels: it must not publish the last
            // evaluated staging matrix when its mask is re-enabled.
            second->CreateState("NoChannels", static_cast<int>(generation->Animations().size()));
            second->SetCurState("NoChannels");
            second->m_avatarMask->useAll = true;
            job.Update(seconds(0.1));
            require(Near(retained, animator.m_localTransforms[observedBone]), "No-channel layer retention");

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
            require(Near(retained, animator.m_localTransforms[observedBone]), "Named-mask local retention");
            std::vector<math::matrix4x4> maskedGlobals(boneCount, skeleton.rootTransform);
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                const auto parent = skeleton.bones[b].parent;
                maskedGlobals[b] = animator.m_localTransforms[b]
                    * (parent < b ? maskedGlobals[parent] : skeleton.rootTransform);
            }
            require(Near(animator.m_FinalTransforms[observedBone],
                skeleton.bones[observedBone].inverseBindMatrix * maskedGlobals[observedBone]
                    * skeleton.globalInverseTransform), "Retained local follows current parent in palette");
            require(Near(animator.socketvec[0]->m_boneMatrix,
                maskedGlobals[observedBone] * animator.GetOwner()->Transform_().GetWorldMatrix()),
                "Masked socket follows composite globals");

            // Single-controller blended pose must also update socket staging/commit.
            animator.m_animationControllers.resize(1);
            first->DeleteAvatarMask();
            first->m_useLayer = true;
            first->CreateState("Next", 1);
            first->CreateTransition("Current", "Next");
            first->UpdateState();
            require(first->m_isBlend && first->GetNextAnimationIndex() == 1, "Real controller transition");
            animator.blendT = 0.5f;
            animator.socketvec[0]->m_boneMatrix = math::matrix4x4{};
            job.Update(seconds(0.1));
            std::vector<math::matrix4x4> globals(boneCount, skeleton.rootTransform);
            for (std::size_t b = 0; b < boneCount; ++b)
            {
                const auto parent = skeleton.bones[b].parent;
                globals[b] = animator.m_localTransforms[b] * (parent < b ? globals[parent] : skeleton.rootTransform);
            }
            const auto socketExpected = globals[observedBone] * animator.GetOwner()->Transform_().GetWorldMatrix();
            require(Near(socketExpected, animator.socketvec[0]->m_boneMatrix), "Blended socket staging");
            require(Near(socketExpected, animator.socketvec[0]->transform.GetLocalMatrix()), "Blended socket commit");
            const auto position = instances[0].attachment->Transform_().GetLocalMatrix().translation();
            const auto socketPosition = socketExpected.translation();
            require(math::length(position - socketPosition) < 0.002f, "Attached object follows blended socket");
            clr.FlushScriptMessages();
            cleanup();
            require(job.GetAnimatorCount() == 0, "Lifecycle unregisters all probe animators");
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
