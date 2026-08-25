#include "ExperimentParity/ExperimentSamplerSelfTest.h"
#include "ExperimentParity/ExperimentPoseSampler.h"

#include "Experiment/Import/ImportedScene.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Experiment/Import/NormalGeneration.h"
#include "Experiment/Import/TangentGeneration.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace im = experiment::importer;

        // 합성 값. 세 축이 서로 다르고 보간 중간값과도 겹치지 않게 골랐다 —
        // 축을 잘못 읽거나 한 성분만 맞아도 통과하는 일이 없도록.
        constexpr math::vector3 PosA{ 1.0f, 2.0f, 3.0f };
        constexpr math::vector3 PosB{ 11.0f, 22.0f, 33.0f };
        constexpr math::vector3 PosC{ -5.0f, 7.0f, -9.0f };

        // 항등과 Y축 90도. slerp 중간값은 45도라 양 끝과 확실히 다르다.
        constexpr math::quaternion QuatA{ 0.0f, 0.0f, 0.0f, 1.0f };
        constexpr math::quaternion QuatB{ 0.0f, 0.70710678f, 0.0f, 0.70710678f };

        struct SamplerChecker final
        {
            std::string& log;
            std::size_t checks{};
            std::size_t failures{};

            void Expect(bool ok, const std::string& what)
            {
                ++checks;
                if (ok) return;
                ++failures;
                log += "  [fail] " + what + "\n";
            }
        };

        // Step 판정은 **정확 일치**를 요구한다. 이 검사의 주장 자체가
        // "보간하지 않고 앞 키 값을 그대로 낸다"이므로 epsilon 을 두면
        // 주장이 약해진다.
        [[nodiscard]] bool SameFloat3(const math::vector3& a, const math::vector3& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }

        [[nodiscard]] bool SameFloat4(const math::vector4& a, const math::vector4& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        [[nodiscard]] bool SameQuaternion(
            const math::quaternion& a, const math::quaternion& b)
        {
            return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
        }

        [[nodiscard]] bool NearFloat3(const math::vector3& a, const math::vector3& b,
            float epsilon)
        {
            return std::abs(a.x - b.x) <= epsilon
                && std::abs(a.y - b.y) <= epsilon
                && std::abs(a.z - b.z) <= epsilon;
        }

        [[nodiscard]] std::string Show(const math::vector3& v)
        {
            char buffer[96];
            std::snprintf(buffer, sizeof(buffer), "(%.4f, %.4f, %.4f)", v.x, v.y, v.z);
            return buffer;
        }

        [[nodiscard]] ex::AnimationChannel MakeTranslationChannel(
            ex::InterpolationMode mode, const std::vector<ex::TranslationKey>& keys)
        {
            ex::AnimationChannel channel;
            channel.bone = ex::BoneIndex(0);
            channel.translationInterpolation = mode;
            channel.translations = keys;
            return channel;
        }

        // ── A. 샘플러 단위 검사 ─────────────────────────────────────────
        void CheckStepTranslation(SamplerChecker& checker)
        {
            const ex::AnimationChannel channel = MakeTranslationChannel(
                ex::InterpolationMode::Step, { { 0.0, PosA }, { 10.0, PosB } });

            const auto at = [&](double t) {
                return sampler::SampleTranslation(channel, t); };

            checker.Expect(SameFloat3(at(0.0), PosA), "Step: 첫 키 시각은 첫 키 값");
            checker.Expect(SameFloat3(at(5.0), PosA),
                "Step: 구간 중간은 앞 키 값 유지 — 실제 " + Show(at(5.0))
                + ", 기대 " + Show(PosA));
            checker.Expect(SameFloat3(at(9.999), PosA), "Step: 다음 키 직전까지 유지");
            checker.Expect(SameFloat3(at(10.0), PosB), "Step: 키 시각에 다음 값으로 전환");
            // ★ 꼬리 케이스. 보간용 구간 선택(size-2 에서 멈춤)을 그대로 쓰면
            //   여기서 앞 키가 나온다 — StepKeyIndex 보정이 막는 회귀다.
            checker.Expect(SameFloat3(at(15.0), PosB),
                "Step: 마지막 키 이후는 마지막 값 — 실제 " + Show(at(15.0))
                + ", 기대 " + Show(PosB));
            checker.Expect(SameFloat3(at(-1.0), PosA), "Step: 첫 키 이전은 첫 키 값");
        }

        void CheckStepTranslationThreeKeys(SamplerChecker& checker)
        {
            const ex::AnimationChannel channel = MakeTranslationChannel(
                ex::InterpolationMode::Step,
                { { 0.0, PosA }, { 10.0, PosB }, { 20.0, PosC } });

            const auto at = [&](double t) {
                return sampler::SampleTranslation(channel, t); };

            checker.Expect(SameFloat3(at(5.0), PosA), "Step(키3): 첫 구간");
            checker.Expect(SameFloat3(at(15.0), PosB), "Step(키3): 가운데 구간");
            checker.Expect(SameFloat3(at(20.0), PosC), "Step(키3): 마지막 키 시각");
            checker.Expect(SameFloat3(at(25.0), PosC),
                "Step(키3): 마지막 키 이후 — 실제 " + Show(at(25.0))
                + ", 기대 " + Show(PosC));
        }

        void CheckLinearTranslation(SamplerChecker& checker)
        {
            const ex::AnimationChannel channel = MakeTranslationChannel(
                ex::InterpolationMode::Linear, { { 0.0, PosA }, { 10.0, PosB } });

            const math::vector3 middle = sampler::SampleTranslation(channel, 5.0);
            const math::vector3 expected{ 6.0f, 12.0f, 18.0f };
            checker.Expect(NearFloat3(middle, expected, 1e-5f),
                "Linear: 구간 중간은 두 키의 중점 — 실제 " + Show(middle)
                + ", 기대 " + Show(expected));
            // Step 과 반드시 달라야 한다. 같으면 이 검사 전체가 판별력을 잃는다.
            checker.Expect(!SameFloat3(middle, PosA),
                "Linear 결과가 Step 결과와 달라야 판별력이 있다");
        }

        void CheckRotation(SamplerChecker& checker)
        {
            ex::AnimationChannel step;
            step.rotationInterpolation = ex::InterpolationMode::Step;
            step.rotations = { { 0.0, QuatA }, { 10.0, QuatB } };

            const math::quaternion stepped = sampler::SampleRotation(step, 5.0);
            checker.Expect(SameQuaternion(stepped, QuatA), "Step 회전: 앞 키 쿼터니언 유지");
            checker.Expect(SameQuaternion(sampler::SampleRotation(step, 15.0), QuatB),
                "Step 회전: 마지막 키 이후 유지");

            ex::AnimationChannel linear = step;
            linear.rotationInterpolation = ex::InterpolationMode::Linear;
            const math::quaternion slerped = sampler::SampleRotation(linear, 5.0);
            checker.Expect(!SameQuaternion(slerped, QuatA) && !SameQuaternion(slerped, QuatB),
                "Linear 회전: 중간은 양 끝 어느 쪽과도 달라야 한다");
        }

        void CheckScale(SamplerChecker& checker)
        {
            ex::AnimationChannel step;
            step.scaleInterpolation = ex::InterpolationMode::Step;
            step.scales = { { 0.0, { 1.0f, 1.0f, 1.0f } },
                            { 10.0, { 4.0f, 4.0f, 4.0f } } };

            checker.Expect(sampler::SampleUniformScale(step, 5.0) == 1.0f,
                "Step 스케일: 앞 키 값 유지");
            checker.Expect(sampler::SampleUniformScale(step, 15.0) == 4.0f,
                "Step 스케일: 마지막 키 이후 유지");

            ex::AnimationChannel linear = step;
            linear.scaleInterpolation = ex::InterpolationMode::Linear;
            checker.Expect(
                std::abs(sampler::SampleUniformScale(linear, 5.0) - 2.5f) <= 1e-5f,
                "Linear 스케일: 중점");
        }

        // 트랙마다 보간을 따로 둔 설계가 실제로 독립인지 — glTF sampler 가
        // 트랙 단위라 한 채널 안에서 섞이는 경우가 실자산에 있다.
        void CheckPerTrackIndependence(SamplerChecker& checker)
        {
            ex::AnimationChannel mixed;
            mixed.translationInterpolation = ex::InterpolationMode::Step;
            mixed.rotationInterpolation = ex::InterpolationMode::Linear;
            mixed.translations = { { 0.0, PosA }, { 10.0, PosB } };
            mixed.rotations = { { 0.0, QuatA }, { 10.0, QuatB } };

            checker.Expect(SameFloat3(sampler::SampleTranslation(mixed, 5.0), PosA),
                "혼합 채널: translation 은 Step 으로 동작");
            checker.Expect(!SameQuaternion(sampler::SampleRotation(mixed, 5.0), QuatA),
                "혼합 채널: rotation 은 같은 채널인데도 Linear 로 동작");
        }

        void CheckDegenerateTracks(SamplerChecker& checker)
        {
            ex::AnimationChannel single;
            single.translationInterpolation = ex::InterpolationMode::Step;
            single.translations = { { 5.0, PosB } };
            checker.Expect(SameFloat3(sampler::SampleTranslation(single, 0.0), PosB)
                && SameFloat3(sampler::SampleTranslation(single, 99.0), PosB),
                "키 1개: 시각과 무관하게 그 값");

            const ex::AnimationChannel empty;
            checker.Expect(
                SameFloat3(sampler::SampleTranslation(empty, 1.0), math::vector3{}),
                "빈 트랙: translation 영벡터");
            checker.Expect(
                SameQuaternion(sampler::SampleRotation(empty, 1.0), QuatA),
                "빈 트랙: rotation 항등");
            checker.Expect(sampler::SampleUniformScale(empty, 1.0) == 1.0f,
                "빈 트랙: scale 1");
        }

        // ── B. 변환 경계 검사 ───────────────────────────────────────────
        // 뼈 2개짜리 최소 씬. 메시는 없어도 되고, 애니메이션만 통과시키면 된다.
        [[nodiscard]] im::ImportedScene MakeMinimalSkinnedScene()
        {
            im::ImportedScene scene;
            scene.metadata.sourcePath = "synthetic.gltf";

            im::SceneNode root;
            root.name = "root";
            scene.nodes.push_back(std::move(root));

            im::SceneNode joint;
            joint.name = "joint";
            joint.parent = im::SceneNodeIndex(0);
            scene.nodes.push_back(std::move(joint));

            im::ImportedSkin skin;
            skin.name = "skin";
            skin.skeletonRoot = im::SceneNodeIndex(0);
            skin.joints = { im::SceneNodeIndex(0), im::SceneNodeIndex(1) };
            skin.inverseBind = { math::matrix4x4{}, math::matrix4x4{} };
            scene.skins.push_back(std::move(skin));

            return scene;
        }

        void CheckConversionPreservesMode(SamplerChecker& checker, std::string& outLog)
        {
            im::ImportedScene scene = MakeMinimalSkinnedScene();

            im::ImportedChannel channel;
            channel.target = im::SceneNodeIndex(1);
            channel.translationInterpolation = im::KeyInterpolation::Step;
            channel.rotationInterpolation = im::KeyInterpolation::Linear;
            // 런타임 타입에 자리가 없는 것 — 강등되고 계수돼야 한다.
            channel.scaleInterpolation = im::KeyInterpolation::CubicSpline;
            channel.translations = { { 0.0, PosA }, { 1.0, PosB } };
            channel.rotations = { { 0.0, QuatA }, { 1.0, QuatB } };
            channel.scales = { { 0.0, { 1.0f, 1.0f, 1.0f } },
                               { 1.0, { 2.0f, 2.0f, 2.0f } } };

            im::ImportedClip clip;
            clip.name = "synthetic";
            clip.durationSeconds = 1.0;
            clip.channels.push_back(std::move(channel));
            scene.clips.push_back(std::move(clip));

            im::ConversionOptions options;
            options.ticksPerSecond = 30.0;
            const im::ConversionResult result =
                im::ConvertToModelDraft(scene, options);

            if (!result.Succeeded() || !result.draft->skeleton
                || result.draft->skeleton->clips.empty()
                || result.draft->skeleton->clips[0].channels.empty())
            {
                checker.Expect(false, "변환 경계가 합성 씬에서 채널을 만들지 못했다");
                for (const im::ImportNote& note : result.notes)
                {
                    outLog += "  [convert] " + std::string(im::ToString(note.code))
                        + " @ " + note.context + " — " + note.message + "\n";
                }
                return;
            }

            const ex::AnimationChannel& converted =
                result.draft->skeleton->clips[0].channels[0];
            checker.Expect(
                converted.translationInterpolation == ex::InterpolationMode::Step,
                "변환: Step 이 런타임까지 보존된다");
            checker.Expect(
                converted.rotationInterpolation == ex::InterpolationMode::Linear,
                "변환: Linear 은 Linear 로 남는다");
            checker.Expect(
                converted.scaleInterpolation == ex::InterpolationMode::Linear,
                "변환: CubicSpline 은 Linear 로 강등된다");

            bool counted = false;
            for (const im::ImportNote& note : result.notes)
            {
                if (note.code == im::ImportNoteCode::UnsupportedInterpolation)
                    counted = true;
            }
            checker.Expect(counted, "변환: CubicSpline 강등이 계수된다(조용한 손실 금지)");

            // 게시된 채널을 그대로 샘플해 계단이 살아 있는지 확인한다 —
            // 플래그 보존과 실제 동작을 한 번 더 잇는다. tick 정본이므로
            // 1초 클립이 30틱이고, 중간은 15틱이다.
            const math::vector3 middle = sampler::SampleTranslation(converted, 15.0);
            checker.Expect(SameFloat3(middle, PosA),
                "변환 산출물 샘플: 계단 유지 — 실제 " + Show(middle)
                + ", 기대 " + Show(PosA));
        }

        // 초→tick 환산이 키를 같은 tick 에 뭉칠 때. Step 은 "그 시각부터의
        // 값"이므로 뭉친 구간에서 마지막 키가 이겨야 한다.
        void CheckKeyCollapse(SamplerChecker& checker, ex::InterpolationMode mode,
            const math::vector3& expectedLast, const std::string& label)
        {
            im::ImportedScene scene = MakeMinimalSkinnedScene();

            im::ImportedChannel channel;
            channel.target = im::SceneNodeIndex(1);
            channel.translationInterpolation = mode == ex::InterpolationMode::Step
                ? im::KeyInterpolation::Step : im::KeyInterpolation::Linear;
            channel.translations = { { 0.0, PosA }, { 0.5, PosB }, { 1.0, PosC } };

            im::ImportedClip clip;
            clip.name = "collapse";
            // duration 을 키보다 짧게 둬 뒤쪽 두 키가 같은 tick 으로 클램프되게 한다.
            clip.durationSeconds = 0.4;
            clip.channels.push_back(std::move(channel));
            scene.clips.push_back(std::move(clip));

            im::ConversionOptions options;
            options.ticksPerSecond = 10.0;
            const im::ConversionResult result =
                im::ConvertToModelDraft(scene, options);

            if (!result.Succeeded() || !result.draft->skeleton
                || result.draft->skeleton->clips.empty()
                || result.draft->skeleton->clips[0].channels.empty())
            {
                checker.Expect(false, label + ": 변환 실패");
                return;
            }

            const ex::AnimationChannel& converted =
                result.draft->skeleton->clips[0].channels[0];
            checker.Expect(converted.translations.size() == 2,
                label + ": 뭉친 키가 하나로 접힌다 — 실제 "
                + std::to_string(converted.translations.size()) + "개");
            if (converted.translations.size() == 2)
            {
                checker.Expect(
                    SameFloat3(converted.translations.back().value, expectedLast),
                    label + ": 남는 값 — 실제 "
                    + Show(converted.translations.back().value)
                    + ", 기대 " + Show(expectedLast));
            }

            bool counted = false;
            for (const im::ImportNote& note : result.notes)
            {
                if (note.code == im::ImportNoteCode::KeyTimeCollapsed) counted = true;
            }
            checker.Expect(counted, label + ": 키 뭉침이 계수된다(조용한 손실 금지)");
        }
    }

    bool RunExperimentSamplerSelfTest(std::string& outLog)
    {
        outLog += "[experiment.sampler] 합성 보간 검사 (자산 의존 없음)\n";

        SamplerChecker checker{ outLog };

        CheckStepTranslation(checker);
        CheckStepTranslationThreeKeys(checker);
        CheckLinearTranslation(checker);
        CheckRotation(checker);
        CheckScale(checker);
        CheckPerTrackIndependence(checker);
        CheckDegenerateTracks(checker);
        CheckConversionPreservesMode(checker, outLog);
        // Step 은 마지막 키가 이기고, Linear 는 기존 규칙대로 먼저 온 키를 남긴다.
        CheckKeyCollapse(checker, ex::InterpolationMode::Step, PosC, "뭉침(Step)");
        CheckKeyCollapse(checker, ex::InterpolationMode::Linear, PosB, "뭉침(Linear)");

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 실패 %zu건\n", checker.checks, checker.failures);
        outLog += summary;

        const bool passed = checker.failures == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }

    // ── 탄젠트 생성 합성 검사 ───────────────────────────────────────────
    namespace
    {
        constexpr float TangentEpsilon = 1e-4f;

        // 평면 메시를 만든다. 법선은 전부 +Z 이고, 삼각형 감김도 +Z 를 향한다.
        [[nodiscard]] im::ImportedMesh MakePlanarMesh(
            const std::vector<math::vector3>& positions,
            const std::vector<math::vector2>& uvs,
            const std::vector<std::uint32_t>& indices)
        {
            im::ImportedMesh mesh;
            mesh.name = "synthetic";
            mesh.streams.positions = positions;
            mesh.streams.uv0 = uvs;
            mesh.streams.normals.assign(positions.size(), math::vector3{ 0.0f, 0.0f, 1.0f });
            mesh.indices = indices;
            return mesh;
        }

        // 셰이더가 실제로 계산하는 것과 같은 식. handedness 가 뒤집히면 여기서
        // 드러난다 — 탄젠트만 보면 부호 오류를 놓친다.
        [[nodiscard]] math::vector3 ReconstructBitangent(const math::vector4& tangent)
        {
            // N = (0,0,1) 이므로 cross(N, T) = (-T.y, T.x, 0).
            const float sign = tangent.w;
            return { -tangent.y * sign, tangent.x * sign, 0.0f };
        }

        [[nodiscard]] bool NearFloat3v(const math::vector3& a, const math::vector3& b)
        {
            return std::abs(a.x - b.x) <= TangentEpsilon
                && std::abs(a.y - b.y) <= TangentEpsilon
                && std::abs(a.z - b.z) <= TangentEpsilon;
        }

        // u 가 +X 로, v 가 +Y 로 증가하는 표준 사각형. 정답이 해석적으로 나온다.
        void CheckTangentAxes(SamplerChecker& checker, float& outSign)
        {
            im::ImportedMesh mesh = MakePlanarMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
                  { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } },
                { 0, 1, 2, 0, 2, 3 });

            im::ImportNoteSink notes;
            im::TangentGenerationStats tangentStats;
            const bool generated = im::GenerateTangents(mesh, "quad", notes, tangentStats);
            checker.Expect(generated, "탄젠트: 표준 사각형에서 생성 성공");
            if (!generated) return;

            checker.Expect(
                mesh.streams.tangents.size() == mesh.streams.positions.size(),
                "탄젠트: 스트림 길이가 정점 수와 일치");
            checker.Expect(mesh.streams.positions.size() == 4,
                "탄젠트: 이음매가 없으면 정점이 늘지 않는다 — 실제 "
                + std::to_string(mesh.streams.positions.size()) + "개");

            bool axesOk = true, bitangentOk = true;
            for (const math::vector4& t : mesh.streams.tangents)
            {
                if (!NearFloat3v({ t.x, t.y, t.z }, { 1.0f, 0.0f, 0.0f })) axesOk = false;
                if (!NearFloat3v(ReconstructBitangent(t), { 0.0f, 1.0f, 0.0f }))
                    bitangentOk = false;
            }
            checker.Expect(axesOk,
                "탄젠트: u 증가 방향(+X)과 일치 — 실제 "
                + Show({ mesh.streams.tangents[0].x, mesh.streams.tangents[0].y,
                         mesh.streams.tangents[0].z }));
            checker.Expect(bitangentOk,
                "탄젠트: w*cross(N,T) 가 v 증가 방향(+Y)과 일치 — 실제 "
                + Show(ReconstructBitangent(mesh.streams.tangents[0])));

            outSign = mesh.streams.tangents[0].w;
        }

        // 위와 기하는 같고 v 방향만 뒤집었다. handedness 가 실제로 계산되는지
        // 보려면 부호가 뒤집히는 짝이 있어야 한다 — 하나만 재면 상수를 박아
        // 놓아도 통과한다.
        void CheckTangentHandednessFlips(SamplerChecker& checker, float referenceSign)
        {
            im::ImportedMesh mesh = MakePlanarMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
                  { 1.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 1.0f }, { 1.0f, 1.0f }, { 1.0f, 0.0f }, { 0.0f, 0.0f } },
                { 0, 1, 2, 0, 2, 3 });

            im::ImportNoteSink notes;
            im::TangentGenerationStats tangentStats;
            if (!im::GenerateTangents(mesh, "quad_flipped", notes, tangentStats))
            {
                checker.Expect(false, "탄젠트(V 반전): 생성 실패");
                return;
            }

            const math::vector4& t = mesh.streams.tangents[0];
            checker.Expect(NearFloat3v({ t.x, t.y, t.z }, { 1.0f, 0.0f, 0.0f }),
                "탄젠트(V 반전): u 방향은 그대로 +X");
            checker.Expect(
                NearFloat3v(ReconstructBitangent(t), { 0.0f, -1.0f, 0.0f }),
                "탄젠트(V 반전): 비탄젠트가 -Y — 실제 "
                + Show(ReconstructBitangent(t)));
            checker.Expect(t.w * referenceSign < 0.0f,
                "탄젠트(V 반전): handedness 부호가 표준 사각형과 반대");
        }

        // ★ 이 검사가 mikktspace 통합 규약의 핵심을 지킨다.
        //   같은 정점을 공유하는 두 삼각형의 UV 가 서로 반대 방향이면 탄젠트가
        //   갈린다. 기존 인덱스에 그대로 써 넣으면 나중 면이 이겨 한쪽이 틀린
        //   부호를 갖는다 — 원본 헤더가 대문자로 금지한 바로 그 상황이다.
        void CheckTangentSeamSplit(SamplerChecker& checker)
        {
            // v1, v2 를 두 삼각형이 공유한다. A 는 u 가 -X 로, B 는 +X 로 증가.
            im::ImportedMesh mesh = MakePlanarMesh(
                { { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
                  {  0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
                { { 1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 0.0f } },
                { 0, 1, 2,   1, 3, 2 });

            im::ImportNoteSink notes;
            im::TangentGenerationStats tangentStats;
            if (!im::GenerateTangents(mesh, "seam", notes, tangentStats))
            {
                checker.Expect(false, "탄젠트(이음매): 생성 실패");
                return;
            }

            checker.Expect(mesh.streams.positions.size() > 4,
                "탄젠트(이음매): 공유 정점이 분리된다 — 실제 "
                + std::to_string(mesh.streams.positions.size()) + "개(원본 4개)");
            checker.Expect(mesh.indices.size() == 6,
                "탄젠트(이음매): 삼각형 수는 그대로");

            // 인덱스가 새 정점 배열 범위 안에 있는지. 재용접이 어긋나면 여기서
            // 터진다.
            bool indicesValid = true;
            for (const std::uint32_t index : mesh.indices)
            {
                if (index >= mesh.streams.positions.size()) indicesValid = false;
            }
            checker.Expect(indicesValid, "탄젠트(이음매): 인덱스가 새 정점 범위 안");
            if (!indicesValid) return;

            // 삼각형 A(코너 0~2)는 u 가 -X 로 증가하므로 탄젠트 x < 0,
            // 삼각형 B(코너 3~5)는 +X 로 증가하므로 x > 0 이어야 한다.
            bool faceAOk = true, faceBOk = true;
            for (std::size_t corner = 0; corner < 3; ++corner)
            {
                if (!(mesh.streams.tangents[mesh.indices[corner]].x < 0.0f))
                    faceAOk = false;
            }
            for (std::size_t corner = 3; corner < 6; ++corner)
            {
                if (!(mesh.streams.tangents[mesh.indices[corner]].x > 0.0f))
                    faceBOk = false;
            }
            checker.Expect(faceAOk,
                "탄젠트(이음매): 삼각형 A 가 자기 UV 방향(-X)을 유지");
            checker.Expect(faceBOk,
                "탄젠트(이음매): 삼각형 B 가 자기 UV 방향(+X)을 유지 "
                "— 실패하면 기존 인덱스에 덮어써 이음매가 뭉갠 것이다");
        }

        void CheckTangentGuards(SamplerChecker& checker)
        {
            // UV 가 없으면 탄젠트는 정의되지 않는다. 조용히 넘어가지 않고
            // 계수해야 한다.
            im::ImportedMesh noUv = MakePlanarMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                {}, { 0, 1, 2 });
            im::ImportNoteSink notes;
            im::TangentGenerationStats tangentStats;
            checker.Expect(!im::GenerateTangents(noUv, "no_uv", notes, tangentStats),
                "가드: UV 없으면 생성하지 않는다");
            checker.Expect(!notes.View().empty(), "가드: UV 없음이 계수된다");

            // 이미 탄젠트가 있으면 손대지 않는다(임포터가 읽어 온 것이 정본).
            im::ImportedMesh existing = MakePlanarMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }, { 0, 1, 2 });
            const math::vector4 marker{ 0.0f, 0.0f, 1.0f, -1.0f };
            existing.streams.tangents.assign(3, marker);
            im::ImportNoteSink untouched;
            checker.Expect(!im::GenerateTangents(existing, "existing", untouched, tangentStats),
                "가드: 기존 탄젠트가 있으면 생성하지 않는다");
            checker.Expect(
                existing.streams.tangents.size() == 3
                && existing.streams.tangents[0].z == marker.z
                && existing.streams.tangents[0].w == marker.w,
                "가드: 기존 탄젠트 값이 보존된다");
        }
    }

    bool RunExperimentTangentSelfTest(std::string& outLog)
    {
        outLog += "[experiment.tangent] mikktspace 합성 검사 (자산 의존 없음)\n";

        SamplerChecker checker{ outLog };

        float referenceSign = 0.0f;
        CheckTangentAxes(checker, referenceSign);
        if (referenceSign != 0.0f)
        {
            CheckTangentHandednessFlips(checker, referenceSign);
        }
        else
        {
            checker.Expect(false, "표준 사각형에서 부호를 못 얻어 반전 검사를 못 했다");
        }
        CheckTangentSeamSplit(checker);
        CheckTangentGuards(checker);

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 실패 %zu건\n", checker.checks, checker.failures);
        outLog += summary;

        const bool passed = checker.failures == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }

    // ── 법선 생성 합성 검사 ─────────────────────────────────────────────
    namespace
    {
        // 법선 없는 메시를 만든다(MakePlanarMesh 는 법선을 채워 준다).
        [[nodiscard]] im::ImportedMesh MakeNormalLessMesh(
            const std::vector<math::vector3>& positions,
            const std::vector<std::uint32_t>& indices)
        {
            im::ImportedMesh mesh;
            mesh.name = "synthetic_no_normal";
            mesh.streams.positions = positions;
            mesh.streams.uv0.assign(positions.size(), math::vector2{});
            mesh.indices = indices;
            return mesh;
        }

        // XY 평면 삼각형. 감김이 반시계이므로 법선은 +Z 다 — 해석적으로 나온다.
        void CheckNormalDirection(SamplerChecker& checker)
        {
            im::ImportedMesh mesh = MakeNormalLessMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { 0, 1, 2 });

            im::ImportNoteSink notes;
            im::NormalGenerationStats stats;
            const bool generated =
                im::GenerateFlatNormals(mesh, "tri", notes, stats);
            checker.Expect(generated, "법선: 없는 메시에서 생성 성공");
            if (!generated) return;

            checker.Expect(
                mesh.streams.normals.size() == mesh.streams.positions.size(),
                "법선: 스트림 길이가 정점 수와 일치");
            bool allUp = !mesh.streams.normals.empty();
            for (const math::vector3& n : mesh.streams.normals)
            {
                if (!NearFloat3v(n, { 0.0f, 0.0f, 1.0f })) allUp = false;
            }
            checker.Expect(allUp,
                "법선: 반시계 감김 → +Z — 실제 "
                + Show(mesh.streams.normals.empty()
                    ? math::vector3{} : mesh.streams.normals[0]));
        }

        // 감김을 뒤집으면 법선도 뒤집혀야 한다. 하나만 재면 상수를 박아 놓아도
        // 통과하므로 짝을 둔다.
        void CheckNormalWindingFlips(SamplerChecker& checker)
        {
            im::ImportedMesh mesh = MakeNormalLessMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { 0, 2, 1 });   // 감김 반전

            im::ImportNoteSink notes;
            im::NormalGenerationStats stats;
            if (!im::GenerateFlatNormals(mesh, "tri_cw", notes, stats))
            {
                checker.Expect(false, "법선(감김 반전): 생성 실패");
                return;
            }
            checker.Expect(
                NearFloat3v(mesh.streams.normals[0], { 0.0f, 0.0f, -1.0f }),
                "법선(감김 반전): -Z — 실제 " + Show(mesh.streams.normals[0]));
        }

        // ★ 평면 법선은 면마다 값이 달라야 하므로 공유 정점이 분리된다.
        //   분리하지 않고 공유 정점에 써 넣으면 나중 면이 이겨 평면 음영이
        //   깨진다 — 탄젠트 재용접과 같은 부류의 함정이다.
        void CheckNormalFaceSplit(SamplerChecker& checker)
        {
            // 정점 4개를 공유하는 직각으로 꺾인 두 삼각형.
            im::ImportedMesh mesh = MakeNormalLessMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f },
                  { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } },
                { 0, 1, 2,   0, 2, 3 });

            im::ImportNoteSink notes;
            im::NormalGenerationStats stats;
            if (!im::GenerateFlatNormals(mesh, "bent", notes, stats))
            {
                checker.Expect(false, "법선(면 분리): 생성 실패");
                return;
            }

            checker.Expect(mesh.streams.positions.size() == 6,
                "법선(면 분리): 삼각형마다 정점 3개 — 실제 "
                + std::to_string(mesh.streams.positions.size()) + "개(원본 4개)");
            checker.Expect(mesh.indices.size() == 6,
                "법선(면 분리): 삼각형 수는 그대로");

            // ★ 정점 수가 틀렸다고 여기서 반환하면 **정작 중요한 행동 단정**
            //   (두 면이 다른 법선을 갖는가)이 통째로 건너뛰어진다. 변이
            //   실험에서 12건이 10건으로 줄며 드러난 약점이다 — 접근할
            //   인덱스만 경계 확인하고 검사는 계속한다.
            if (mesh.indices.size() < 6) return;
            const std::size_t normalCount = mesh.streams.normals.size();
            if (mesh.indices[0] >= normalCount || mesh.indices[3] >= normalCount)
            {
                checker.Expect(false, "법선(면 분리): 인덱스가 법선 범위를 벗어난다");
                return;
            }

            // 두 면의 법선이 서로 달라야 한다. 같다면 공유 정점에 덮어써
            // 한쪽이 상대의 법선을 물려받은 것이다.
            const math::vector3& a = mesh.streams.normals[mesh.indices[0]];
            const math::vector3& b = mesh.streams.normals[mesh.indices[3]];
            checker.Expect(!NearFloat3v(a, b),
                "법선(면 분리): 꺾인 두 면이 서로 다른 법선을 갖는다 — 실제 "
                + Show(a) + " vs " + Show(b));
            checker.Expect(NearFloat3v(a, { 0.0f, 0.0f, 1.0f }),
                "법선(면 분리): 첫 면은 +Z — 실제 " + Show(a));
        }

        void CheckNormalGuards(SamplerChecker& checker)
        {
            // 이미 법선이 있으면 손대지 않는다(source 가 정본).
            im::ImportedMesh existing = MakePlanarMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
                { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 0.0f, 1.0f } }, { 0, 1, 2 });
            im::ImportNoteSink notes;
            im::NormalGenerationStats stats;
            checker.Expect(
                !im::GenerateFlatNormals(existing, "has_normal", notes, stats),
                "가드: 기존 법선이 있으면 생성하지 않는다");
            checker.Expect(existing.streams.positions.size() == 3,
                "가드: 기존 법선이 있으면 정점도 그대로");

            // 넓이 0 삼각형은 평면을 정의하지 못한다. 방향을 지어내면 조명이
            // 그 자리에서 튀므로 영벡터 + 계수여야 한다.
            im::ImportedMesh degenerate = MakeNormalLessMesh(
                { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 2.0f, 0.0f, 0.0f } },
                { 0, 1, 2 });
            im::ImportNoteSink degenerateNotes;
            im::NormalGenerationStats degenerateStats;
            if (!im::GenerateFlatNormals(
                degenerate, "degenerate", degenerateNotes, degenerateStats))
            {
                checker.Expect(false, "가드(퇴화): 생성 자체가 실패했다");
                return;
            }
            checker.Expect(degenerateStats.degenerateFaces == 1,
                "가드(퇴화): 넓이 0 면이 계수된다 — 실제 "
                + std::to_string(degenerateStats.degenerateFaces) + "건");
            checker.Expect(
                NearFloat3v(degenerate.streams.normals[0], math::vector3{}),
                "가드(퇴화): 방향을 지어내지 않고 영벡터로 둔다");
        }
    }

    bool RunExperimentNormalSelfTest(std::string& outLog)
    {
        outLog += "[experiment.normal] 평면 법선 생성 합성 검사 (자산 의존 없음)\n";
        outLog += "  ※ 실자산 중 법선 없는 것이 없어 이 경로는 실자산 게이트가"
                  " 밟지 않는다 — 이 검사가 유일한 판별자다.\n";

        SamplerChecker checker{ outLog };

        CheckNormalDirection(checker);
        CheckNormalWindingFlips(checker);
        CheckNormalFaceSplit(checker);
        CheckNormalGuards(checker);

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 실패 %zu건\n", checker.checks, checker.failures);
        outLog += summary;

        const bool passed = checker.failures == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }
}
