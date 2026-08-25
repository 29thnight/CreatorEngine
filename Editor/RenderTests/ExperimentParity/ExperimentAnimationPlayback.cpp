#include "ExperimentParity/ExperimentAnimationPlayback.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"
#include "ExperimentParity/ExperimentPoseSampler.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "ModelLoader.h"
#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Import/ImporterModelDecoder.h"
#include "Experiment/Import/GltfImporter.h"
#include "Experiment/Import/FbxImporter.h"
#include "Experiment/Import/NormalGeneration.h"
#include "Experiment/Import/TangentGeneration.h"
#include "Uuid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace im = experiment::importer;
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
    // ── 맞대결 벤치 ─────────────────────────────────────────────────────
    // ★ 이전 벤치는 성능 우열 비교가 아니었다. 실물 디코더가 없어
    //   `legacy 로드 → 브리지 → 검증 → 게시`를 재는 구조였고, 그건 legacy
    //   **위에 얹는** 비용이라 더 나올 수밖에 없었다. I1 로 실물 디코더가
    //   생겨 이제야 같은 일을 하는 두 경로를 나란히 잴 수 있다.
    //
    // ★ legacy 는 `.asset` 쿠킹 바이너리가 있으면 Assimp 를 아예 안 돈다.
    //   그것을 모르고 재면 **쿠킹 역직렬화 대 소스 전체 파싱**을 비교하게
    //   되므로, 어느 경로를 탔는지 반드시 함께 보고한다.
    bool RunExperimentModelBenchmark(
        const std::string& modelPath, int iterations, std::string& outLog)
    {
        iterations = (std::max)(1, (std::min)(iterations, 50));
        outLog += "[experiment.bench] 대상: " + modelPath
            + " (반복 " + std::to_string(iterations) + ")\n";
#ifdef _DEBUG
        outLog += "  ★ Debug 빌드다 — 성능 판정에 쓰면 안 된다."
                  " 같은 조건에서 Release 와 25배까지 벌어지고 규모별 우열이"
                  " 뒤집힌 전례가 있다. 방향 참고용.\n";
#endif

        const std::filesystem::path sourcePath(modelPath);
        std::filesystem::path assetPath = sourcePath;
        assetPath.replace_extension(".asset");

        // legacy 가 어느 경로를 탈지 먼저 판정해 보고한다.
        std::error_code errorCode;
        bool cookedUsable = std::filesystem::exists(assetPath, errorCode);
        if (cookedUsable && std::filesystem::exists(sourcePath, errorCode))
        {
            const auto assetTime =
                std::filesystem::last_write_time(assetPath, errorCode);
            const auto sourceTime =
                std::filesystem::last_write_time(sourcePath, errorCode);
            cookedUsable = sourceTime <= assetTime;
        }
        outLog += std::string("  legacy 경로: ")
            + (cookedUsable ? "**.asset 쿠킹 바이너리**(Assimp 미실행)"
                            : "Assimp 소스 파싱")
            + "\n";

        // ── 1. legacy (실제 경로 그대로) ────────────────────────────────
        std::shared_ptr<::Model> legacyModel;
        const auto legacyStats = MeasureMs(iterations, [&]
        {
            legacyModel = ::Model::LoadModelShared(modelPath);
        });
        if (!legacyModel)
        {
            outLog += "  결과: 실패 (legacy 로드 불가)\n";
            return false;
        }
        outLog += "  legacy 로드            : " + FormatMs(legacyStats) + "\n";

        // ★ 지금 집어 둔다. 뒤에 강제 소스 파싱과 experiment 로드가 끼어들어
        //   thread_local 내역이 다른 로드의 것으로 바뀔 수 있다.
        //   집는 값은 **마지막 회차**의 내역이지 legacyStats 의 최솟값 회차가
        //   아니다 — 구간 비율을 보는 용도이지 총합과 자릿수까지 맞지 않는다.
        const ::ModelLoader::CookedLoadBreakdown cookedBreakdown =
            ::ModelLoader::LastCookedLoadBreakdown();

        // ── 2. legacy 강제 소스 파싱 ────────────────────────────────────
        // 쿠킹이 있으면 위 수치는 역직렬화다. 소스 대 소스로도 재려면 .asset
        // 이 없는 자리에 원본을 복사해 Assimp 를 강제로 태운다.
        std::pair<double, double> legacySourceStats{ 0.0, 0.0 };
        bool measuredLegacySource = false;
        if (cookedUsable)
        {
            // ★ 원본 옆이 아니라 **같은 폴더**에 다른 이름으로 둔다. temp 로
            //   옮기면 legacy 로더가 자산 루트 기준으로 부수 파일을 찾다가
            //   실패할 수 있고, 실제로 그렇게 조용히 실패했다.
            const std::filesystem::path scratchCopy = sourcePath.parent_path()
                / ("__bench_src__" + sourcePath.filename().string());
            std::filesystem::remove(scratchCopy, errorCode);
            std::filesystem::copy_file(sourcePath, scratchCopy,
                std::filesystem::copy_options::overwrite_existing, errorCode);

            if (errorCode)
            {
                outLog += "  [note] 강제 소스 사본을 만들지 못해 Assimp 수치를"
                    " 얻지 못했다: " + errorCode.message() + "\n";
            }
            else
            {
                std::shared_ptr<::Model> forced;
                legacySourceStats = MeasureMs(iterations, [&]
                {
                    // 혹시 로더가 캐시를 떨구면 2회차부터 쿠킹을 재게 된다.
                    // 매 회 지워 소스 파싱만 재는 것을 보장한다.
                    std::filesystem::path leftover = scratchCopy;
                    leftover.replace_extension(".asset");
                    std::error_code ignored;
                    std::filesystem::remove(leftover, ignored);
                    forced = ::Model::LoadModelShared(scratchCopy.string());
                });
                measuredLegacySource = static_cast<bool>(forced);
                if (measuredLegacySource)
                {
                    outLog += "  legacy 강제 소스(Assimp): "
                        + FormatMs(legacySourceStats) + "\n";
                }
                else
                {
                    // ★ 조용히 쿠킹 수치로 대체하지 않는다. 그러면 라벨만
                    //   Assimp 이고 값은 역직렬화인 거짓 비교가 된다.
                    outLog += "  [note] 강제 소스 로드가 실패해 Assimp 수치를"
                        " 얻지 못했다 — 소스 대 소스 판정을 생략한다\n";
                }
                std::filesystem::path leftover = scratchCopy;
                leftover.replace_extension(".asset");
                std::filesystem::remove(leftover, errorCode);
                std::filesystem::remove(scratchCopy, errorCode);
            }
        }

        // ── 3. experiment (실물 디코더 전체) ────────────────────────────
        im::ImporterDecoderOptions decoderOptions;
        decoderOptions.conversion.modelAssetId.value =
            Uuid::FromName(FileGuid::ns_filesystem(), sourcePath.string());
        decoderOptions.conversion.modelName = sourcePath.stem().string();
        decoderOptions.conversion.ticksPerSecond = 30.0;

        ex::ModelLoadRequest loadRequest;
        loadRequest.sourcePath = sourcePath;
        loadRequest.sourcePreference = ex::ModelSourcePreference::SourceOnly;

        ex::ModelLoadResult experimentResult;
        const auto experimentStats = MeasureMs(iterations, [&]
        {
            ex::ModelLoader loader(
                std::make_unique<im::ImporterModelDecoder>(decoderOptions));
            experimentResult = loader.Load(loadRequest);
        });
        if (!experimentResult.Succeeded())
        {
            outLog += "  experiment 로드        : 실패 — experiment.gltf /"
                      " experiment.fbx 로 원인 확인\n";
            outLog += "  결과: 실패\n";
            return false;
        }
        outLog += "  experiment 로드(전체)  : " + FormatMs(experimentStats) + "\n";

        // ── 3-1. 쿠킹 경로 (I7) ──────────────────────────────
        //
        // 같은 모델을 굽고 다시 읽는 데 얼마가 드는가.
        //
        // ★ legacy 쿠킹과 비교할 때만 의미가 있다. 소스 임포트와 비교하면
        //   종류가 다른 일을 재는 것이다.
        //
        // ★ 경로를 만들지 않는다 — 임시 파일에 굽고 그 경로를 그대로 넘긴다.
        //   쿠킹 산출물의 자리는 SerializationPlan §3.6.1 소관이고 미결정이다.
        std::pair<double, double> cookedStats{ 0.0, 0.0 };
        std::size_t cookedBytes = 0;
        bool cookedMeasured = false;
        {
            im::ImporterModelDecoder draftDecoder(decoderOptions);
            ex::ModelDecodeResult decoded = draftDecoder.Decode(loadRequest);
            if (decoded.draft.has_value())
            {
                const std::vector<std::byte> baked =
                    experiment::cooked::Write(*decoded.draft);
                cookedBytes = baked.size();

                std::filesystem::path bakedPath = sourcePath;
                bakedPath.replace_extension(".bench.cemc");
                {
                    std::ofstream out(bakedPath, std::ios::binary | std::ios::trunc);
                    out.write(reinterpret_cast<const char*>(baked.data()),
                        static_cast<std::streamsize>(baked.size()));
                }

                ex::ModelLoadRequest cookedRequest;
                cookedRequest.cookedPath = bakedPath;
                cookedRequest.sourcePreference = ex::ModelSourcePreference::CookedOnly;

                ex::ModelLoadResult cookedResult;
                cookedStats = MeasureMs(iterations, [&]
                {
                    ex::ModelLoader loader(
                        std::make_unique<experiment::cooked::CookedModelDecoder>());
                    cookedResult = loader.Load(cookedRequest);
                });
                // ★ 실패를 0ms 로 읽히게 두지 않는다.
                cookedMeasured = cookedResult.Succeeded();
                if (!cookedMeasured)
                {
                    outLog += "  experiment 쿠킹        : 실패 — experiment.cooked 로 "
                              "원인 확인\n";
                }

                // ── 쿠킹 로드 구간 분해 ─────────────────────────────
                // 총합만 보면 "어디를 고쳐야 하는가"를 또 추측하게 된다.
                // 이 슬라이스에서 이미 두 번 추측이 틀렸으므로 갈라 둔다.
                if (cookedMeasured)
                {
                    // ★ 디코더와 **같은 수단**으로 재야 한다. 처음엔 여기만
                    //   ifstream 으로 두어, 디코더를 fread 로 바꾼 뒤에도 프로브가
                    //   1.76ms 를 계속 찍었다 — 하지도 않는 일을 재는 계측기였다.
                    const auto readStats = MeasureMs(iterations, [&]
                    {
                        std::vector<std::byte> buffer(cookedBytes);
                        std::FILE* in = nullptr;
#if defined(_WIN32)
                        if (0 != ::_wfopen_s(&in, bakedPath.c_str(), L"rb")) in = nullptr;
#else
                        in = std::fopen(bakedPath.c_str(), "rb");
#endif
                        if (in)
                        {
                            (void)std::fread(buffer.data(), 1, buffer.size(), in);
                            std::fclose(in);
                        }
                    });
                    const auto parseStats = MeasureMs(iterations, [&]
                    {
                        ex::ModelDraft parsed;
                        std::vector<ex::ModelLoadIssue> parseIssues;
                        (void)experiment::cooked::Read(baked, parsed, parseIssues);
                    });
                    ex::ModelDraft validated;
                    std::vector<ex::ModelLoadIssue> ignored;
                    (void)experiment::cooked::Read(baked, validated, ignored);
                    const auto validateStats = MeasureMs(iterations, [&]
                    {
                        (void)ex::ModelLoader::Validate(validated);
                    });

                    // ★ 구간 합과 총합을 빼서 "나머지"라고 부르지 않는다.
                    //   구간마다 따로 잰 **최솟값**은 더할 수 있는 양이 아니다
                    //   (각 구간의 최선이 같은 회차에서 나오지 않는다). 실제로
                    //   합이 총합을 넘어 음수 나머지가 나왔다. 비율만 읽는다.
                    char stages[320];
                    std::snprintf(stages, sizeof(stages),
                        "    구간(각각의 min — 서로 더할 수 없다): "
                        "파일 읽기 %.3f · 파싱 %.3f · 검증 %.3f / 전체 %.3f\n",
                        readStats.first, parseStats.first, validateStats.first,
                        cookedStats.first);
                    outLog += stages;
                }
                std::filesystem::remove(bakedPath, errorCode);
            }
        }
        if (cookedMeasured)
        {
            char cookedLine[220];
            std::snprintf(cookedLine, sizeof(cookedLine),
                "  experiment 쿠킹        : %s (%zu B)\n",
                FormatMs(cookedStats).c_str(), cookedBytes);
            outLog += cookedLine;
        }

        // ── 4. 판정 ─────────────────────────────────────────────────────
        // 같은 일을 하는 것끼리만 비교한다. 쿠킹 대 소스는 우열이 아니라
        // **아직 쿠킹 경로가 없다는 사실**을 말할 뿐이다.
        const double experiment = experimentStats.first;
        // 쿠킹이 없으면 legacy 로드가 곧 Assimp 소스 파싱이다. 쿠킹이 있으면
        // 강제 소스 측정이 성공했을 때만 소스 대 소스를 판정한다 — 실패했는데
        // 쿠킹 값을 끌어다 쓰면 라벨만 Assimp 인 거짓 비교가 된다.
        const bool haveSourceBaseline = !cookedUsable || measuredLegacySource;
        if (haveSourceBaseline)
        {
            const double legacySource = measuredLegacySource
                ? legacySourceStats.first : legacyStats.first;
            const double slower = (std::max)(legacySource, experiment);
            const double faster = (std::min)(legacySource, experiment);

            char verdict[320];
            std::snprintf(verdict, sizeof(verdict),
                "  [소스 대 소스] Assimp %.3fms vs experiment %.3fms → %s (%.2f배)\n",
                legacySource, experiment,
                experiment < legacySource ? "experiment 우세" : "legacy 우세",
                faster > 0.0 ? slower / faster : 0.0);
            outLog += verdict;
        }
        else
        {
            outLog += "  [소스 대 소스] 판정 불가 — Assimp 기준선을 얻지 못했다\n";
        }

        if (cookedUsable)
        {
            char cooked[300];
            std::snprintf(cooked, sizeof(cooked),
                "  [실사용 주의] legacy 실사용은 쿠킹 %.3fms 다 — experiment 에는"
                " 아직 쿠킹 경로가 없어(I7) 실사용 로드는 %.2f배 느리다.\n",
                legacyStats.first,
                legacyStats.first > 0.0 ? experiment / legacyStats.first : 0.0);
            outLog += cooked;

            // ── 쿠킹 로드 구간 분해 (I7 목표 수치 산정용) ──────────────
            // 총합만으로는 "임포터를 바꿔서 이길 수 있는 부분"과 "무엇을 써도
            // 그대로 남는 공유 비용(텍스처)"을 가를 수 없다.
            if (cookedMeasured)
            {
                // ★ 여기가 I7 의 본문이다 — 쿠킹 대 쿠킹.
                //   앞의 "소스 대 소스"와 섞어 읽으면 종류가 다른 일을
                //   비교하게 된다.
                char cookedVerdict[260];
                const double legacyCooked = legacyStats.first;
                const double experimentCooked = cookedStats.first;
                std::snprintf(cookedVerdict, sizeof(cookedVerdict),
                    "  [쿠킹 대 쿠킹] legacy %.3fms vs experiment %.3fms → %s (%.2f배)\n",
                    legacyCooked, experimentCooked,
                    experimentCooked < legacyCooked ? "experiment 우세" : "legacy 우세",
                    experimentCooked > 0.0 ? legacyCooked / experimentCooked : 0.0);
                outLog += cookedVerdict;
            }

            const ::ModelLoader::CookedLoadBreakdown& cb = cookedBreakdown;
            if (!cb.valid)
            {
                // ★ 0ms 를 찍어 "비용 없음"으로 읽히게 두지 않는다.
                outLog += "  [쿠킹 분해] 내역 없음 — 쿠킹 경로를 타지 않았다\n";
            }
            else
            {
                const double attributable = cb.materialsMs - cb.materialTextureMs;
                char parts[640];
                std::snprintf(parts, sizeof(parts),
                    "  [쿠킹 분해] 총 %.3fms = open %.3f + 스켈레톤 %.3f + 노드 %.3f"
                    " + 메시 %.3f + 재질 %.3f\n"
                    "              그중 텍스처(공유 비용) %.3fms · 재질 나머지 %.3fms\n"
                    "              임포터가 건드릴 수 있는 몫 = %.3fms (%.1f%%)\n",
                    cb.totalMs, cb.openMs, cb.skeletonMs, cb.nodesMs,
                    cb.meshesMs, cb.materialsMs,
                    cb.materialTextureMs, attributable,
                    cb.totalMs - cb.materialTextureMs,
                    cb.totalMs > 0.0
                        ? 100.0 * (cb.totalMs - cb.materialTextureMs) / cb.totalMs : 0.0);
                outLog += parts;
            }
        }

        // ── 4-1. 단계별 분해 ────────────────────────────────────────────
        // 총합만 보면 "경로 전체가 느리다"와 "특정 패스가 지배한다"를 구분할
        // 수 없다. 후처리를 끈 채 임포트해 순수 파싱을 재고, 나머지를 하나씩
        // 돌려 각 패스를 분리한다. 패스가 씬을 바꾸므로 매 회 사본으로 잰다.
        {
            im::GltfImporter gltfImporter;
            im::FbxImporter fbxImporter;
            im::IAssetImporter* stageImporter =
                gltfImporter.CanImport(sourcePath)
                    ? static_cast<im::IAssetImporter*>(&gltfImporter)
                    : static_cast<im::IAssetImporter*>(&fbxImporter);

            im::ImportRequest bare;
            bare.sourcePath = sourcePath;
            bare.options.generateMissingNormals = false;
            bare.options.generateMissingTangents = false;

            im::ImportResult parsed;
            const auto parseStats = MeasureMs(iterations, [&]
            {
                parsed = stageImporter->Import(bare);
            });

            if (parsed.Succeeded())
            {
                const im::ImportedScene pristine = *parsed.scene;

                const auto normalStats = MeasureMs(iterations, [&]
                {
                    im::ImportedScene copy = pristine;
                    im::ImportNoteSink sink;
                    im::ImportOptions on;
                    im::GenerateMissingNormals(copy, on, sink);
                });

                im::ImportedScene withNormals = pristine;
                {
                    im::ImportNoteSink sink;
                    im::ImportOptions on;
                    im::GenerateMissingNormals(withNormals, on, sink);
                }
                im::TangentGenerationStats tangentDetail;
                const auto tangentStats = MeasureMs(iterations, [&]
                {
                    im::ImportedScene copy = withNormals;
                    im::ImportNoteSink sink;
                    im::ImportOptions on;
                    tangentDetail = im::GenerateMissingTangents(copy, on, sink);
                });

                im::ImportedScene finished = withNormals;
                {
                    im::ImportNoteSink sink;
                    im::ImportOptions on;
                    im::GenerateMissingTangents(finished, on, sink);
                }
                const auto convertStats = MeasureMs(iterations, [&]
                {
                    const auto result = im::ConvertToModelDraft(
                        finished, decoderOptions.conversion);
                    (void)result;
                });

                const im::ConversionResult converted =
                    im::ConvertToModelDraft(finished, decoderOptions.conversion);
                std::pair<double, double> validateStats{ 0.0, 0.0 };
                std::pair<double, double> copyStats{ 0.0, 0.0 };
                if (converted.Succeeded())
                {
                    validateStats = MeasureMs(iterations, [&]
                    {
                        const auto issues = ex::ModelLoader::Validate(
                            *converted.draft, ex::ModelImportOptions{});
                        (void)issues;
                    });
                    // 게시는 draft 복사 + 검증 + move 다. 복사 몫을 따로 재
                    // 두면 "게시가 비싸다"를 복사 탓과 구분할 수 있다.
                    copyStats = MeasureMs(iterations, [&]
                    {
                        ex::ModelDraft duplicate = *converted.draft;
                        (void)duplicate;
                    });
                }

                char breakdown[520];
                std::snprintf(breakdown, sizeof(breakdown),
                    "  [분해] 파싱 %.3f · 법선 %.3f · 탄젠트 %.3f · 변환 %.3f"
                    " · 검증 %.3f · draft복사 %.3f (ms, 각 min)\n"
                    "         합계 %.3fms 대 전체 %.3fms\n",
                    parseStats.first, normalStats.first, tangentStats.first,
                    convertStats.first, validateStats.first, copyStats.first,
                    parseStats.first + normalStats.first + tangentStats.first
                        + convertStats.first + validateStats.first
                        + copyStats.first,
                    experiment);
                outLog += breakdown;

                // 탄젠트가 지배적이므로 그 안도 갈라 본다. 어디에 쓰였는지
                // 모르면 최적화가 추측이 된다.
                char tangentDetailLine[280];
                std::snprintf(tangentDetailLine, sizeof(tangentDetailLine),
                    "  [탄젠트 내부] mikktspace %.3f · 직교화 %.3f · 재용접 %.3f"
                    " (ms, 1회분) — 정점 %zu → %zu\n",
                    tangentDetail.mikktspaceMs, tangentDetail.reorthogonalizeMs,
                    tangentDetail.weldMs, tangentDetail.verticesBefore,
                    tangentDetail.verticesAfter);
                outLog += tangentDetailLine;
            }
            else
            {
                outLog += "  [분해] 후처리 없이 임포트하지 못해 분해를 생략한다\n";
            }
        }

        // ── 5. 구조 ─────────────────────────────────────────────────────
        std::size_t legacyVertices = 0, legacyIndices = 0;
        for (std::size_t i = 0; i < legacyModel->GetMeshCount(); ++i)
        {
            const auto* mesh = legacyModel->GetMesh(static_cast<int>(i));
            legacyVertices += mesh->GetVertices().size();
            legacyIndices += mesh->GetIndices().size();
        }
        std::size_t experimentVertices = 0, experimentIndices = 0;
        for (const ex::Mesh& mesh : experimentResult.model->Meshes())
        {
            experimentVertices += mesh.vertices.size();
            experimentIndices += mesh.indices.size();
        }

        char structure[420];
        std::snprintf(structure, sizeof(structure),
            "  구조: sizeof(Vertex) legacy %zuB vs experiment %zuB\n"
            "        정점 %zu → %zu, 인덱스 %zu → %zu\n"
            "        정점 바이트 %.2fMB → %.2fMB\n",
            sizeof(::Vertex), sizeof(ex::Vertex),
            legacyVertices, experimentVertices, legacyIndices, experimentIndices,
            static_cast<double>(legacyVertices * sizeof(::Vertex))
                / (1024.0 * 1024.0),
            static_cast<double>(experimentVertices * sizeof(ex::Vertex))
                / (1024.0 * 1024.0));
        outLog += structure;

        outLog += "  결과: 통과\n";
        return true;
    }
}
