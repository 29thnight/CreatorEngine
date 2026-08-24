#include "ExperimentParity/ExperimentAnimationPlayback.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"
#include "ExperimentParity/ExperimentPoseSampler.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        using namespace DirectX;

        double KeyTime(const NodeAnimation::PositionKey& key) { return key.m_time; }
        double KeyTime(const NodeAnimation::RotationKey& key) { return key.m_time; }
        double KeyTime(const NodeAnimation::ScaleKey& key) { return key.m_time; }

        // ── 공통 키 구간 선택 (legacy CurrentKeyIndex 와 같은 규칙) ─────────
        template <typename Key>
        std::size_t IntervalIndex(const std::vector<Key>& keys, double time)
        {
            std::size_t index = 0;
            while (index + 2 < keys.size()
                && KeyTime(keys[index + 1]) <= time)
            {
                ++index;
            }
            return index;
        }

        // Experiment 채널 샘플러는 sampler::SampleLocal 하나뿐이다
        // (ExperimentPoseSampler.h — Step 보간을 여기서 집행한다).

        // ── legacy 참조 샘플러 (legacy 자료구조·순회 그대로) ────────────────
        XMMATRIX SampleLegacyLocal(const NodeAnimation& nodeAnim, double time)
        {
            XMVECTOR position = nodeAnim.m_positionKeys.empty()
                ? XMVectorZero() : nodeAnim.m_positionKeys.front().m_position;
            if (nodeAnim.m_positionKeys.size() > 1)
            {
                const std::size_t index = IntervalIndex(nodeAnim.m_positionKeys, time);
                const auto& k0 = nodeAnim.m_positionKeys[index];
                const auto& k1 = nodeAnim.m_positionKeys[index + 1];
                const float t = static_cast<float>(
                    (time - k0.m_time) / (k1.m_time - k0.m_time));
                position = XMVectorLerp(k0.m_position, k1.m_position, t);
            }

            XMVECTOR rotation = nodeAnim.m_rotationKeys.empty()
                ? XMQuaternionIdentity() : nodeAnim.m_rotationKeys.front().m_rotation;
            if (nodeAnim.m_rotationKeys.size() > 1)
            {
                const std::size_t index = IntervalIndex(nodeAnim.m_rotationKeys, time);
                const auto& k0 = nodeAnim.m_rotationKeys[index];
                const auto& k1 = nodeAnim.m_rotationKeys[index + 1];
                const float t = static_cast<float>(
                    (time - k0.m_time) / (k1.m_time - k0.m_time));
                rotation = XMQuaternionSlerp(k0.m_rotation, k1.m_rotation, t);
            }

            float scale = nodeAnim.m_scaleKeys.empty()
                ? 1.0f : nodeAnim.m_scaleKeys.front().m_scale.x;
            if (nodeAnim.m_scaleKeys.size() > 1)
            {
                const std::size_t index = IntervalIndex(nodeAnim.m_scaleKeys, time);
                const auto& k0 = nodeAnim.m_scaleKeys[index];
                const auto& k1 = nodeAnim.m_scaleKeys[index + 1];
                const float t = static_cast<float>(
                    (time - k0.m_time) / (k1.m_time - k0.m_time));
                scale = k0.m_scale.x + (k1.m_scale.x - k0.m_scale.x) * t;
            }

            return XMMatrixScaling(scale, scale, scale)
                * XMMatrixRotationQuaternion(rotation)
                * XMMatrixTranslationFromVector(position);
        }

        struct LegacyPose final
        {
            std::vector<XMMATRIX> finals{};   // old bone index 기준
            std::vector<std::uint8_t> hasChannel{};
        };

        void EvaluateLegacyBone(::Bone* bone, ::Skeleton& skeleton,
            ::Animation& clip, double time, FXMMATRIX parentTransform,
            LegacyPose& pose)
        {
            const auto found = clip.m_nodeAnimations.find(bone->m_name);
            if (found == clip.m_nodeAnimations.end())
            {
                // legacy UpdateBone 재현: 채널 없는 bone 은 parent 를 그대로 전달.
                for (::Bone* child : bone->m_children)
                    EvaluateLegacyBone(child, skeleton, clip, time, parentTransform, pose);
                return;
            }

            const XMMATRIX local = SampleLegacyLocal(found->second, time);
            const XMMATRIX global = local * parentTransform;
            const auto index = static_cast<std::size_t>(bone->m_index);
            pose.finals[index] =
                bone->m_offset * global * skeleton.m_globalInverseTransform;
            pose.hasChannel[index] = 1;

            for (::Bone* child : bone->m_children)
                EvaluateLegacyBone(child, skeleton, clip, time, global, pose);
        }

        using ExperimentPose = sampler::Pose;

        float MaxAbsDiff(FXMMATRIX a, CXMMATRIX b)
        {
            XMFLOAT4X4 fa, fb;
            XMStoreFloat4x4(&fa, a);
            XMStoreFloat4x4(&fb, b);
            float maxDiff = 0.0f;
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                    maxDiff = (std::max)(maxDiff,
                        std::abs(fa.m[row][column] - fb.m[row][column]));
            return maxDiff;
        }

        bool AllFinite(FXMMATRIX m)
        {
            XMFLOAT4X4 f;
            XMStoreFloat4x4(&f, m);
            for (std::size_t row = 0; row < 4; ++row)
                for (std::size_t column = 0; column < 4; ++column)
                    if (!std::isfinite(f.m[row][column])) return false;
            return true;
        }

        template <typename Fn>
        std::pair<double, double> MeasureMs(int iterations, Fn&& fn)
        {
            double minMs = 0.0, totalMs = 0.0;
            for (int i = 0; i < iterations; ++i)
            {
                const auto begin = std::chrono::steady_clock::now();
                fn();
                const auto end = std::chrono::steady_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(
                    end - begin).count();
                totalMs += ms;
                minMs = (i == 0) ? ms : (std::min)(minMs, ms);
            }
            return { minMs, totalMs / iterations };
        }

        std::string FormatMs(std::pair<double, double> stats)
        {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "min %.3fms avg %.3fms",
                stats.first, stats.second);
            return buffer;
        }
    }

    bool RunExperimentAnimationPlaybackSelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        outLog += "[experiment.anim] 대상: " + modelPath + "\n";

        const bridge::LoadedPair pair = bridge::LoadAndBridge(modelPath);
        if (!bridge::AppendOutcome(pair, outLog))
        {
            outLog += "  결과: 실패 (로드/브리지/검증 단계)\n";
            return false;
        }

        const ex::Skeleton* skeleton = pair.result.model->TryGetSkeleton();
        if (!skeleton || skeleton->clips.empty())
        {
            outLog += "  결과: 실패 (skeleton 또는 clip 이 없어 재생 검증 불가)\n";
            return false;
        }
        ::Skeleton& legacySkeleton = *pair.legacy->m_Skeleton;

        constexpr float ParityEpsilon = 2e-3f;
        constexpr float MovementEpsilon = 1e-4f;
        std::size_t failures = 0;
        bool anyMovement = false;

        for (std::size_t clipIndex = 0; clipIndex < skeleton->clips.size(); ++clipIndex)
        {
            const ex::AnimationClip& clip = skeleton->clips[clipIndex];
            ::Animation& legacyClip = legacySkeleton.m_animations[clipIndex];

            const double duration = clip.durationTicks;
            const double sampleTimes[5] = {
                0.0, duration * 0.25, duration * 0.5,
                duration * 0.75, duration * 0.999 };

            float maxParityError = 0.0f;
            float maxMovement = 0.0f;
            bool finite = true;
            ExperimentPose firstPose;

            for (std::size_t sampleIndex = 0; sampleIndex < 5; ++sampleIndex)
            {
                const double time = sampleTimes[sampleIndex];

                ExperimentPose exPose;
                sampler::EvaluatePose(*skeleton, clip, time, exPose);

                LegacyPose legacyPose;
                legacyPose.finals.assign(
                    legacySkeleton.m_bones.size(), XMMatrixIdentity());
                legacyPose.hasChannel.assign(legacySkeleton.m_bones.size(), 0);
                if (legacySkeleton.m_rootBone)
                {
                    EvaluateLegacyBone(legacySkeleton.m_rootBone, legacySkeleton,
                        legacyClip, time, legacySkeleton.m_rootTransform, legacyPose);
                }

                for (std::size_t oldIndex = 0;
                    oldIndex < legacyPose.finals.size(); ++oldIndex)
                {
                    const std::size_t newIndex = pair.remap.Empty()
                        ? oldIndex : pair.remap.oldToNew[oldIndex];
                    if (legacyPose.hasChannel[oldIndex]
                        != exPose.hasChannel[newIndex])
                    {
                        ++failures;
                        outLog += "  [diff] clips[" + std::to_string(clipIndex)
                            + "] bone(old " + std::to_string(oldIndex)
                            + ") 채널 유무 불일치\n";
                        continue;
                    }
                    if (!legacyPose.hasChannel[oldIndex]) continue;
                    if (!AllFinite(exPose.finals[newIndex])) finite = false;
                    maxParityError = (std::max)(maxParityError, MaxAbsDiff(
                        legacyPose.finals[oldIndex], exPose.finals[newIndex]));
                }

                if (sampleIndex == 0)
                {
                    firstPose = std::move(exPose);
                }
                else
                {
                    for (std::size_t boneIndex = 0;
                        boneIndex < exPose.finals.size(); ++boneIndex)
                    {
                        if (!exPose.hasChannel[boneIndex]) continue;
                        maxMovement = (std::max)(maxMovement, MaxAbsDiff(
                            firstPose.finals[boneIndex], exPose.finals[boneIndex]));
                    }
                }
            }

            const bool moving = maxMovement > MovementEpsilon;
            anyMovement = anyMovement || moving;
            const bool parityOk = maxParityError <= ParityEpsilon;
            if (!parityOk || !finite) ++failures;

            char line[256];
            std::snprintf(line, sizeof(line),
                "  clips[%zu] '%s' 채널 %zu: 파리티 최대오차 %.6f%s, 포즈 변화 %.4f → %s\n",
                clipIndex, clip.name.c_str(), clip.channels.size(),
                maxParityError, finite ? "" : " [비유한!]", maxMovement,
                (parityOk && finite) ? (moving ? "재생 OK" : "정지 포즈") : "실패");
            outLog += line;
        }

        if (!anyMovement)
        {
            ++failures;
            outLog += "  [diff] 어떤 clip 도 포즈 변화를 만들지 않았다 — 재생 미확인\n";
        }

        const bool passed = failures == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패")
            + " (clip " + std::to_string(skeleton->clips.size()) + "개)\n";
        return passed;
    }

    bool RunExperimentModelBenchmark(
        const std::string& modelPath, int iterations, std::string& outLog)
    {
        iterations = (std::max)(1, (std::min)(iterations, 50));
        outLog += "[experiment.bench] 대상: " + modelPath
            + " (반복 " + std::to_string(iterations) + ")\n";
#ifdef _DEBUG
        outLog += "  ★ Debug 빌드 수치 — 방향 참고용. 성능 판정은 Release 로만.\n";
#endif

        // 1. legacy full load (첫 회는 캐시 콜드일 수 있어 별도 표기)
        std::shared_ptr<::Model> lastLegacy;
        double coldMs = 0.0;
        const auto loadStats = MeasureMs(iterations, [&]
        {
            const auto begin = std::chrono::steady_clock::now();
            lastLegacy = ::Model::LoadModelShared(modelPath);
            const auto end = std::chrono::steady_clock::now();
            if (coldMs == 0.0)
                coldMs = std::chrono::duration<double, std::milli>(end - begin).count();
        });
        if (!lastLegacy)
        {
            outLog += "  결과: 실패 (legacy 로드 불가)\n";
            return false;
        }
        char coldLine[128];
        std::snprintf(coldLine, sizeof(coldLine),
            "  legacy full load     : %s (첫 회 %.3fms)\n",
            FormatMs(loadStats).c_str(), coldMs);
        outLog += coldLine;

        // 2. 브리지 변환 (remap + draft 전체 구축)
        bridge::BridgeReport report;
        bridge::BoneRemap remap;
        if (lastLegacy->m_Skeleton)
            remap = bridge::ComputeBoneRemap(*lastLegacy->m_Skeleton, report);
        if (!report.failures.empty())
        {
            outLog += "  결과: 실패 (브리지 불가 구조)\n";
            return false;
        }
        const int cheapIterations = (std::max)(iterations, 10);
        ex::ModelDraft draft;
        const auto bridgeStats = MeasureMs(cheapIterations, [&]
        {
            bridge::BridgeReport scratch;
            draft = bridge::BuildDraftFromLegacy(*lastLegacy, remap, scratch);
        });
        outLog += "  bridge 변환          : " + FormatMs(bridgeStats) + "\n";

        // 3. Validate 단독
        const auto validateStats = MeasureMs(cheapIterations, [&]
        {
            const auto issues =
                ex::ModelLoader::Validate(draft, ex::ModelImportOptions{});
            (void)issues;
        });
        outLog += "  Experiment Validate  : " + FormatMs(validateStats) + "\n";

        // 4. draft 복사 (게시 측정에서 빼서 볼 기준)
        const auto copyStats = MeasureMs(cheapIterations, [&]
        {
            ex::ModelDraft copy = draft;
            (void)copy;
        });
        outLog += "  draft 복사(기준)     : " + FormatMs(copyStats) + "\n";

        // 5. 게시 전체 (decoder 복사 + Validate + move 게시)
        bridge::LegacyBridgeDecoder* decoderPtr = nullptr;
        {
            auto decoder = std::make_unique<bridge::LegacyBridgeDecoder>(
                ex::ModelDraft(draft));
            decoderPtr = decoder.get();
            ex::ModelLoader loader(std::move(decoder));
            ex::ModelLoadRequest request;
            request.sourcePath = modelPath;
            request.sourcePreference = ex::ModelSourcePreference::SourceOnly;
            ex::ModelLoadResult lastResult;
            const auto publishStats = MeasureMs(cheapIterations, [&]
            {
                lastResult = loader.Load(request);
            });
            (void)decoderPtr;
            outLog += "  게시 Load(복사 포함) : " + FormatMs(publishStats) + "\n";

            // 6. 포즈 샘플링 (애니메이션 배선 비용)
            if (lastResult.Succeeded())
            {
                if (const ex::Skeleton* skeleton =
                    lastResult.model->TryGetSkeleton();
                    skeleton && !skeleton->clips.empty())
                {
                    const auto poseStats = MeasureMs(cheapIterations, [&]
                    {
                        ExperimentPose pose;
                        for (const ex::AnimationClip& clip : skeleton->clips)
                        {
                            for (int step = 0; step < 60; ++step)
                            {
                                const double time = clip.durationTicks
                                    * (static_cast<double>(step) / 59.0);
                                sampler::EvaluatePose(*skeleton, clip, time, pose);
                            }
                        }
                    });
                    const std::size_t poseCount = skeleton->clips.size() * 60;
                    char poseLine[160];
                    std::snprintf(poseLine, sizeof(poseLine),
                        "  포즈 샘플링          : %s (%zu포즈, 포즈당 avg %.1fus)\n",
                        FormatMs(poseStats).c_str(), poseCount,
                        poseStats.second * 1000.0 / static_cast<double>(poseCount));
                    outLog += poseLine;
                }
                else
                {
                    outLog += "  포즈 샘플링          : (skeleton/clip 없음 — 생략)\n";
                }
            }
            else
            {
                outLog += "  게시 실패 — Validate 이슈는 experiment.model 로 확인\n";
                outLog += "  결과: 실패\n";
                return false;
            }
        }

        std::size_t vertexTotal = 0;
        for (std::size_t i = 0; i < lastLegacy->GetMeshCount(); ++i)
            vertexTotal += lastLegacy->GetMesh(static_cast<int>(i))->GetVertices().size();
        outLog += "  규모: vertices " + std::to_string(vertexTotal)
            + ", bones " + std::to_string(lastLegacy->m_Skeleton
                ? lastLegacy->m_Skeleton->m_bones.size() : 0) + "\n";
        outLog += "  결과: 통과\n";
        return true;
    }
}
