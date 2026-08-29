#include "ExperimentParity/ExperimentGltfImportSelfTest.h"
#include "ExperimentParity/ExperimentLegacyBridge.h"
#include "ExperimentParity/ExperimentPoseSampler.h"

#include "Model.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "Experiment/Import/GltfImporter.h"
#include "Experiment/Import/FbxImporter.h"
#include "Experiment/Import/ImporterModelDecoder.h"
#include "Experiment/Import/ImportedScene.h"
#include "Experiment/Import/SceneToModelDraft.h"
#include "Experiment/ModelLoader.h"
#include "StandardMaterialProperty.h"
#include "Uuid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace im = experiment::importer;

        constexpr std::size_t MaxGltfDiffLines = 24;
        constexpr float BoundsEpsilon = 1e-3f;

        struct GltfDiffLog final
        {
            std::string& log;
            std::size_t count{};

            void Add(std::string text)
            {
                ++count;
                if (count <= MaxGltfDiffLines)
                    log += "  [diff] " + std::move(text) + "\n";
                else if (count == MaxGltfDiffLines + 1)
                    log += "  [diff] ... 이후 생략\n";
            }
        };

        void AppendNotes(const std::vector<im::ImportNote>& notes,
            std::string_view tag, std::string& outLog, std::size_t& outErrors)
        {
            for (const im::ImportNote& note : notes)
            {
                if (note.severity == im::ImportNoteSeverity::Error) ++outErrors;
                outLog += "  [" + std::string(tag) + "] "
                    + (note.severity == im::ImportNoteSeverity::Error ? "ERROR " :
                       note.severity == im::ImportNoteSeverity::Warning ? "WARN  " : "INFO  ")
                    + std::string(im::ToString(note.code)) + " @ " + note.context
                    + " ×" + std::to_string(note.count) + " — " + note.message + "\n";
            }
        }

        struct Aabb final
        {
            float minimum[3]{};
            float maximum[3]{};
            bool valid{};

            void Add(const math::aabb& bounds)
            {
                // ★ 빈 상자는 min()/max() 에 기하적 의미가 없다(라이브러리
                //   주석이 명시한다). 검사 전에 갈라 낸다.
                if (bounds.is_empty()) return;
                const math::vector3 boundsMin = bounds.min();
                const math::vector3 boundsMax = bounds.max();
                const float lo[3] = { boundsMin.x, boundsMin.y, boundsMin.z };
                const float hi[3] = { boundsMax.x, boundsMax.y, boundsMax.z };
                if (!valid)
                {
                    for (int i = 0; i < 3; ++i) { minimum[i] = lo[i]; maximum[i] = hi[i]; }
                    valid = true;
                    return;
                }
                for (int i = 0; i < 3; ++i)
                {
                    minimum[i] = (std::min)(minimum[i], lo[i]);
                    maximum[i] = (std::max)(maximum[i], hi[i]);
                }
            }
        };

        [[nodiscard]] Aabb ModelBounds(const ex::Model& model)
        {
            Aabb aabb;
            for (const ex::Mesh& mesh : model.Meshes()) aabb.Add(mesh.bounds);
            return aabb;
        }

        [[nodiscard]] std::size_t TriangleCount(const ex::Model& model)
        {
            std::size_t total = 0;
            for (const ex::Mesh& mesh : model.Meshes()) total += mesh.indices.size() / 3;
            return total;
        }

        [[nodiscard]] std::size_t VertexCount(const ex::Model& model)
        {
            std::size_t total = 0;
            for (const ex::Mesh& mesh : model.Meshes()) total += mesh.vertices.size();
            return total;
        }

        // ★ 방향을 구분해야 한다. fastgltf 경로는 **의도적으로** legacy 보다
        //   많이 보존한다(스켈레톤을 joint 의 조상 폐포로 잡아 계층 노드를
        //   더 담는다 — 그 덕에 채널 10개가 살아난다). 그것을 실패로 세면
        //   "고침이 성공할수록 게이트가 빨개지는" 자가당착이 된다.
        //   회귀(legacy 에 있는데 fastgltf 에 없음)만 실패로 세고, 추가분은
        //   정보로 남긴다.
        void CompareNameSets(std::vector<std::string> legacy,
            std::vector<std::string> gltf, std::string_view what,
            std::string_view label, GltfDiffLog& diff, std::string& outLog)
        {
            std::ranges::sort(legacy);
            std::ranges::sort(gltf);
            std::vector<std::string> onlyLegacy, onlyGltf;
            std::ranges::set_difference(legacy, gltf, std::back_inserter(onlyLegacy));
            std::ranges::set_difference(gltf, legacy, std::back_inserter(onlyGltf));

            for (const std::string& name : onlyLegacy)
            {
                diff.Add(std::string(what) + " '" + name + "' 이 "
                    + std::string(label) + " 경로에서 누락됐다");
            }
            for (const std::string& name : onlyGltf)
            {
                outLog += "  [gain] " + std::string(what) + " '" + name + "' 은 "
                    + std::string(label) + " 경로에만 있다(보존 개선)\n";
            }
        }

        // ── 탄젠트 감사 ──────────────────────────────────────────────────
        struct TangentAudit final
        {
            std::size_t vertices{};
            std::size_t zeroTangents{};
            std::size_t nonUnit{};
            std::size_t zeroNormals{};
            std::size_t nonOrthogonal{};     // 정규화 |dot(N,T)| > 0.1
            float maxNormalLengthError{};
            float maxNormalDot{};
        };

        [[nodiscard]] TangentAudit AuditTangents(const ex::Model& model)
        {
            TangentAudit audit;
            for (const ex::Mesh& mesh : model.Meshes())
            {
            for (std::size_t vertexIndex = 0;
                vertexIndex < mesh.vertices.size(); ++vertexIndex)
            {
                const ex::Vertex vertex = mesh.vertices[vertexIndex];
                ++audit.vertices;
                    const math::vector4& t = vertex.tangent;
                    const float tangentLength = std::sqrt(
                        t.x * t.x + t.y * t.y + t.z * t.z);
                    if (tangentLength <= 1e-6f) { ++audit.zeroTangents; continue; }
                    if (std::abs(tangentLength - 1.0f) > 1e-3f) ++audit.nonUnit;

                    // 두 벡터를 각각 정규화해서 재야 진짜 각도가 나온다.
                    const math::vector3& n = vertex.normal;
                    const float normalLength = std::sqrt(
                        n.x * n.x + n.y * n.y + n.z * n.z);
                    if (normalLength <= 1e-6f) { ++audit.zeroNormals; continue; }
                    audit.maxNormalLengthError = (std::max)(
                        audit.maxNormalLengthError, std::abs(normalLength - 1.0f));

                    const float cosine = std::abs(
                        n.x * t.x + n.y * t.y + n.z * t.z)
                        / (tangentLength * normalLength);
                    audit.maxNormalDot = (std::max)(audit.maxNormalDot, cosine);
                    if (cosine > 0.1f) ++audit.nonOrthogonal;
                }
            }
            return audit;
        }

        [[nodiscard]] std::string FormatTangentAudit(
            std::string_view label, const TangentAudit& audit)
        {
            char line[320];
            std::snprintf(line, sizeof(line),
                "  탄젠트[%.*s]: 정점 %zu | 영벡터 %zu, 비단위 %zu, 법선 영벡터 %zu"
                " | 법선 길이오차 %.4f | 정규화 |dot| 최대 %.4f, 비직교(>0.1) %zu\n",
                static_cast<int>(label.size()), label.data(),
                audit.vertices, audit.zeroTangents, audit.nonUnit, audit.zeroNormals,
                audit.maxNormalLengthError, audit.maxNormalDot, audit.nonOrthogonal);
            return line;
        }

        // ── Step 보간 감사 ───────────────────────────────────────────────
        // 두 가지를 갈라서 본다: (1) source 의 Step 트랙이 게시까지 살아남았나,
        // (2) 살아남은 트랙이 **실제로 계단으로 계산되나**. 플래그만 옮기고
        // 샘플러가 무시하면 (1)만 초록인 채 화면은 그대로 틀리기 때문이다.
        constexpr float StepObservableEpsilon = 1e-4f;

        struct StepAudit final
        {
            std::size_t sourceTracks{};      // ImportedScene 의 Step 트랙
            std::size_t publishedTracks{};   // 게시 draft 의 Step 트랙
            std::size_t verified{};          // 계단 동작을 실제로 확인한 트랙
            std::size_t violations{};        // Step 인데 계단이 아니었던 트랙
            float maxObservableGap{};        // Linear 였다면 벌어졌을 최대 차이

            // 확인하지 못한 트랙의 내역. "왜 0건인가"에 답하지 못하면
            // 판별력 없는 게이트인지 손실이 애초에 없는 것인지 구분이 안 된다.
            std::size_t singleKeyTracks{};   // 키 1개 — Step/Linear 가 수학적으로 동일
            std::size_t constantTracks{};    // 키는 여럿인데 값이 전부 같음
        };

        [[nodiscard]] std::size_t CountSourceStepTracks(const im::ImportedScene& scene)
        {
            std::size_t total = 0;
            for (const im::ImportedClip& clip : scene.clips)
            {
                for (const im::ImportedChannel& channel : clip.channels)
                {
                    if (channel.translationInterpolation == im::KeyInterpolation::Step)
                        ++total;
                    if (channel.rotationInterpolation == im::KeyInterpolation::Step)
                        ++total;
                    if (channel.scaleInterpolation == im::KeyInterpolation::Step)
                        ++total;
                }
            }
            return total;
        }

        // 값이 실제로 달라지는 첫 구간을 골라 그 **중간 시각**을 샘플한다.
        // Step 이면 앞 키 값과 정확히 같아야 하고, Linear 였다면 두 값 사이
        // 어딘가로 떨어져 반드시 달라진다 — 그래서 이 한 점이 판별력을 갖는다.
        template <typename Key, typename Extract, typename Sample>
        void VerifyStepTrack(const std::vector<Key>& keys, Extract extract,
            Sample sample, StepAudit& audit)
        {
            ++audit.publishedTracks;
            if (keys.size() < 2)
            {
                ++audit.singleKeyTracks;
                return;
            }

            for (std::size_t i = 0; i + 1 < keys.size(); ++i)
            {
                const auto before = extract(keys[i]);
                const auto after = extract(keys[i + 1]);
                float gap = 0.0f;
                for (std::size_t c = 0; c < before.size(); ++c)
                    gap = (std::max)(gap, std::abs(before[c] - after[c]));
                if (gap <= StepObservableEpsilon) continue;   // 판별력 없는 구간

                const double middle = (keys[i].time + keys[i + 1].time) * 0.5;
                const auto sampled = sample(middle);
                float error = 0.0f;
                for (std::size_t c = 0; c < before.size(); ++c)
                    error = (std::max)(error, std::abs(before[c] - sampled[c]));

                ++audit.verified;
                audit.maxObservableGap = (std::max)(audit.maxObservableGap, gap);
                if (error != 0.0f) ++audit.violations;
                return;
            }
            // 어떤 구간도 값이 달라지지 않았다 = 상수 트랙.
            ++audit.constantTracks;
        }

        [[nodiscard]] StepAudit AuditStepInterpolation(
            const im::ImportedScene& scene, const ex::Skeleton& skeleton)
        {
            StepAudit audit;
            audit.sourceTracks = CountSourceStepTracks(scene);

            for (const ex::AnimationClip& clip : skeleton.clips)
            {
                for (const ex::AnimationChannel& channel : clip.channels)
                {
                    if (channel.translationInterpolation == ex::InterpolationMode::Step)
                    {
                        VerifyStepTrack(channel.translations,
                            [](const ex::TranslationKey& key) {
                                return std::array<float, 3>{
                                    key.value.x, key.value.y, key.value.z }; },
                            [&](double time) {
                                const math::vector3 v =
                                    sampler::SampleTranslation(channel, time);
                                return std::array<float, 3>{ v.x, v.y, v.z }; },
                            audit);
                    }
                    if (channel.rotationInterpolation == ex::InterpolationMode::Step)
                    {
                        VerifyStepTrack(channel.rotations,
                            [](const ex::RotationKey& key) {
                                return std::array<float, 4>{
                                    key.quaternion.x, key.quaternion.y,
                                    key.quaternion.z, key.quaternion.w }; },
                            [&](double time) {
            const math::quaternion q =
                sampler::SampleRotation(channel, time);
                                return std::array<float, 4>{ q.x, q.y, q.z, q.w }; },
                            audit);
                    }
                    if (channel.scaleInterpolation == ex::InterpolationMode::Step)
                    {
                        // 샘플러가 legacy 재현으로 x 성분만 쓰므로 검사도 x 로 한다.
                        VerifyStepTrack(channel.scales,
                            [](const ex::ScaleKey& key) {
                                return std::array<float, 1>{ key.value.x }; },
                            [&](double time) {
                                return std::array<float, 1>{
                                    sampler::SampleUniformScale(channel, time) }; },
                            audit);
                    }
                }
            }
            return audit;
        }
    }

    namespace
    {
    [[nodiscard]] std::size_t AuditPackedVertexLayouts(
        const ex::Model& model, std::string& outLog)
    {
        std::size_t failures = 0;
        std::size_t staticMeshes = 0;
        std::size_t skinnedMeshes = 0;
        std::size_t uv1Meshes = 0;
        std::size_t colorMeshes = 0;
        std::size_t packedBytes = 0;
        for (const ex::Mesh& mesh : model.Meshes())
        {
            const ex::VertexAttributeMask attributes = mesh.vertices.AttributeMask();
            const bool skinned = ex::Has(attributes, ex::VertexAttribute::BoneIndices);
            staticMeshes += skinned ? 0u : 1u;
            skinnedMeshes += skinned ? 1u : 0u;
            uv1Meshes += ex::Has(attributes, ex::VertexAttribute::Uv1) ? 1u : 0u;
            colorMeshes += ex::Has(attributes, ex::VertexAttribute::Color) ? 1u : 0u;
            packedBytes += mesh.vertices.ByteSize();

            if (!ex::VertexBuffer::IsSupportedLayout(attributes)
                || mesh.vertices.Stride() != ex::StrideOf(attributes)
                || mesh.vertices.ByteSize()
                    != mesh.vertices.size() * mesh.vertices.Stride())
            {
                ++failures;
            }
        }

        outLog += "  V3 packed layout: static " + std::to_string(staticMeshes)
            + " · skin " + std::to_string(skinnedMeshes)
            + " · uv1 " + std::to_string(uv1Meshes)
            + " · color " + std::to_string(colorMeshes)
            + " · bytes " + std::to_string(packedBytes)
            + " · 위반 " + std::to_string(failures) + "건\n";
        return failures;
    }

    [[nodiscard]] std::size_t AuditStandardMaterialContract(
        const ex::Model& model, std::string& outLog)
    {
        constexpr std::array requiredNumericProperties{
            standard_material::property::BaseColor,
            standard_material::property::Metallic,
            standard_material::property::Roughness,
            standard_material::property::Emissive,
            standard_material::property::NormalScale,
            standard_material::property::OcclusionStrength,
        };

        std::size_t failures = 0;
        for (std::size_t materialIndex = 0;
            materialIndex < model.Materials().size(); ++materialIndex)
        {
            const ex::Material& material = model.Materials()[materialIndex];
            for (const std::string_view required : requiredNumericProperties)
            {
                const auto found = std::ranges::find_if(material.properties,
                    [required](const ex::MaterialProperty& property)
                    {
                        return property.name == required;
                    });
                if (found != material.properties.end()) continue;

                ++failures;
                outLog += "  [diff] material[" + std::to_string(materialIndex)
                    + "]에 표준 property '" + std::string(required)
                    + "'가 없다\n";
            }

            for (const ex::MaterialProperty& property : material.properties)
            {
                if (property.name.empty() || property.name.front() != '_') continue;
                ++failures;
                outLog += "  [diff] material[" + std::to_string(materialIndex)
                    + "]가 폐기된 underscore property '" + property.name
                    + "'를 게시했다\n";
            }
        }

        outLog += "  Material property 계약: "
            + std::to_string(model.Materials().size()) + "개 재질, 위반 "
            + std::to_string(failures) + "건\n";
        return failures;
    }

    // 두 임포터가 같은 IR 로 수렴하므로 게이트도 하나여야 한다. 검사를 복제하면
    // 갈라지는 순간 한쪽만 느슨해진다 — 임포터와 이름표만 주입받는다.
    bool RunImportSelfTest(im::IAssetImporter& importer, std::string_view command,
        std::string_view label, const std::string& modelPath, std::string& outLog)
    {
        outLog += "[" + std::string(command) + "] 대상: " + modelPath + "\n";

        const std::filesystem::path sourcePath(modelPath);
        if (!importer.CanImport(sourcePath))
        {
            outLog += "  결과: 실패 (이 임포터가 다루는 확장자가 아니다)\n";
            return false;
        }

        // 1. 임포트
        im::ImportRequest request;
        request.sourcePath = sourcePath;
        const im::ImportResult imported = importer.Import(request);
        std::size_t errors = 0;
        AppendNotes(imported.notes, "import", outLog, errors);
        if (!imported.Succeeded())
        {
            outLog += "  결과: 실패 (임포터가 ImportedScene 을 만들지 못함)\n";
            return false;
        }
        const im::ImportedScene& scene = *imported.scene;

        char scaleLine[220];
        std::snprintf(scaleLine, sizeof(scaleLine),
            "  %.*s 임포트: nodes %zu, meshes %zu, materials %zu, textures %zu,"
            " skins %zu, clips %zu | vertices %zu, triangles %zu\n",
            static_cast<int>(label.size()), label.data(),
            scene.nodes.size(), scene.meshes.size(), scene.materials.size(),
            scene.textures.size(), scene.skins.size(), scene.clips.size(),
            im::TotalVertexCount(scene), im::TotalTriangleCount(scene));
        outLog += scaleLine;

        // 2. IR 자체 검증
        AppendNotes(im::ValidateImportedScene(scene), "ir", outLog, errors);

        // 3. 변환 경계
		im::ConversionOptions options;
		options.modelAssetId.value =
			FileGuid::CreateRandomV4().m_guid;
        options.modelName = sourcePath.stem().string();
        options.ticksPerSecond = 30.0;
        const im::ConversionResult converted =
            im::ConvertToModelDraft(scene, options);
        AppendNotes(converted.notes, "convert", outLog, errors);
        if (!converted.Succeeded())
        {
            outLog += "  결과: 실패 (변환 경계가 draft 를 만들지 못함)\n";
            return false;
        }

        // 4. 게시 — ★ **실물 디코더**를 탄다.
        // 하네스 전용 디코더(미리 만든 draft 를 돌려주는 것)로 게시하면 생산
        // 경로가 검사에서 통째로 빠진다 — "검사는 통과하는데 실물은 배선조차
        // 안 된" 상태가 정확히 그렇게 생겼다.
        //
        // 임포트가 두 번 도는 값은 치른다(위 IR 감사분 + 디코더 내부분).
        // 같은 입력에 결정론적 경로라 산출물은 같고, 하네스에서 한 번 더 읽는
        // 비용보다 생산 경로를 실제로 태우는 값어치가 크다.
        im::ImporterDecoderOptions decoderOptions;
        decoderOptions.conversion = options;
        ex::ModelLoader loader(
            std::make_unique<im::ImporterModelDecoder>(decoderOptions));
        ex::ModelLoadRequest loadRequest;
        loadRequest.sourcePath = sourcePath;
        loadRequest.sourcePreference = ex::ModelSourcePreference::SourceOnly;
        const ex::ModelLoadResult published = loader.Load(loadRequest);
        for (const ex::ModelLoadIssue& issue : published.issues)
        {
            // ImportNote 를 실어 나르는 항목은 임포트 단계에서 이미 찍었으므로
            // 게시 줄에서는 접는다 — 같은 것을 두 번 보여 주면 로그만 길어진다.
            if (issue.code == ex::ModelLoadIssueCode::ImportNote) continue;
            outLog += std::string("  [validate] ")
                + (issue.severity == ex::ModelLoadIssueSeverity::Error ? "ERROR " :
                   issue.severity == ex::ModelLoadIssueSeverity::Warning ? "WARN  "
                                                                         : "INFO  ")
                + std::string(bridge::ToString(issue.code)) + " @ " + issue.context
                + " — " + issue.message + "\n";
        }
        if (!published.Succeeded())
        {
            outLog += "  결과: 실패 (" + std::string(label)
                + " 산출물이 게시 검증을 통과하지 못함)\n";
            return false;
        }

        // M5의 legacy codec/runtime과 experiment 변환기가 같은 논리 키를
        // 게시하는지 검사한다. 이름이 갈리면 M6에서 ShaderMeta가 살아 있어도
        // property가 실제 draw binding에 도달하지 않는다.
        const std::size_t materialContractFailures =
            AuditStandardMaterialContract(*published.model, outLog);
        const std::size_t vertexLayoutFailures =
            AuditPackedVertexLayouts(*published.model, outLog);

        // ★ 게시된 내용이 없으면 이후 검사는 전부 빈 집합을 돌게 된다. 그
        //   상태를 통과로 내면 "아무것도 검증하지 못했다"가 "이상 없다"로
        //   읽힌다 — 실제로 애니메이션 전용 FBX 가 여기 걸렸다(클립이 전부
        //   탈락해 게시본이 비었는데 초록이 나왔다).
        {
            const std::size_t publishedMeshes = published.model->Meshes().size();
            std::size_t publishedClips = 0, publishedBones = 0, publishedChannels = 0;
            if (const ex::Skeleton* skeleton = published.model->TryGetSkeleton())
            {
                publishedClips = skeleton->clips.size();
                publishedBones = skeleton->bones.size();
                for (const ex::AnimationClip& clip : skeleton->clips)
                    publishedChannels += clip.channels.size();
            }
            // 게시 결과를 **항상** 찍는다. 기준선이 없는 파일에서는 이 줄이
            // 유일한 산출물 증거다 — 없으면 "무엇이 나왔는지"를 다른 줄의
            // 유무로 추론해야 한다.
            char publishedLine[220];
            std::snprintf(publishedLine, sizeof(publishedLine),
                "  게시: 메시 %zu, bone %zu, clip %zu, 채널 %zu\n",
                publishedMeshes, publishedBones, publishedClips, publishedChannels);
            outLog += publishedLine;

            if (publishedMeshes == 0 && publishedClips == 0)
            {
                outLog += "  [diff] 게시된 메시도 클립도 없다 — 이 파일로는"
                    " 아무것도 검증하지 못한다(스킨 없는 애니메이션 전용"
                    " 파일이면 현 ModelDraft 가 스켈레톤을 유도하지 못한다)\n";
                outLog += "  결과: 실패(검증 불가)\n";
                return false;
            }
        }

        // 5. Step 보간 감사 (비교가 아니라 자가 검증 — 기준선과 무관하게 돈다)
        // 구조 검증 오류와 따로 센다. 한 숫자로 합치면 요약 줄이 무엇이
        // 틀렸는지 알려주지 못한다.
        std::size_t stepFailures = 0;
        if (const ex::Skeleton* publishedSkeleton = published.model->TryGetSkeleton())
        {
            const StepAudit audit = AuditStepInterpolation(scene, *publishedSkeleton);
            char stepLine[280];
            std::snprintf(stepLine, sizeof(stepLine),
                "  Step 보간: source %zu 트랙 → 게시 %zu 트랙, 계단 확인 %zu"
                " (위반 %zu, Linear 였다면 최대 %.4f 어긋남)\n",
                audit.sourceTracks, audit.publishedTracks, audit.verified,
                audit.violations, audit.maxObservableGap);
            outLog += stepLine;

            if (audit.sourceTracks > 0 && audit.publishedTracks == 0)
            {
                ++stepFailures;
                outLog += "  [diff] source 에 Step 트랙이 있는데 게시본에 하나도"
                    " 없다 — 변환 경계가 보간 방식을 떨어뜨렸다\n";
            }
            if (audit.violations > 0)
            {
                ++stepFailures;
                outLog += "  [diff] Step 으로 표시된 트랙 "
                    + std::to_string(audit.violations)
                    + "개가 계단이 아니다 — 샘플러가 플래그를 무시하고 있다\n";
            }
            if (audit.publishedTracks > 0 && audit.verified == 0)
            {
                // 실패는 아니지만 통과로 읽히면 안 된다 — 아무것도 검사하지
                // 못한 상태다. 내역을 함께 찍어 "게이트가 무력한 것"인지
                // "이 자산에는 애초에 손실이 없는 것"인지 구분되게 한다.
                char why[220];
                std::snprintf(why, sizeof(why),
                    "  [note] 계단 동작을 확인하지 못했다 — 키 1개 %zu 트랙,"
                    " 값이 상수인 트랙 %zu. 이 자산에서는 Step 과 Linear 의"
                    " 결과가 같다.\n",
                    audit.singleKeyTracks, audit.constantTracks);
                outLog += why;
            }
        }

        // 6. 탄젠트 감사 — 생성 패스가 실제로 값을 채웠는지.
        // 이 검사 전에는 vertex.tangent 가 전부 영벡터였다. "생성했다"는 주장은
        // 영벡터가 사라졌는지로 재야 하고, 방향이 맞는지는 자산으로 알 수 없으니
        // 해석적 정답이 있는 experiment.tangent 가 따로 본다.
        std::size_t tangentFailures = 0;
        const TangentAudit gltfTangents = AuditTangents(*published.model);
        outLog += FormatTangentAudit(label, gltfTangents);

        if (gltfTangents.vertices > 0 && gltfTangents.zeroTangents > 0)
        {
            ++tangentFailures;
            outLog += "  [diff] 탄젠트가 채워지지 않은 정점이 "
                + std::to_string(gltfTangents.zeroTangents) + "개 있다\n";
        }
        if (gltfTangents.nonUnit > 0)
        {
            ++tangentFailures;
            outLog += "  [diff] 단위 길이가 아닌 탄젠트가 "
                + std::to_string(gltfTangents.nonUnit) + "개 있다\n";
        }
        // 재직교화 패스를 넣은 뒤로 이것은 단정이 된다. 보고만 하면 TBN 이
        // 찌그러진 채 초록으로 지나간다 — 실제로 한 번 그럴 뻔했다.
        if (gltfTangents.nonOrthogonal > 0)
        {
            ++tangentFailures;
            outLog += "  [diff] 법선과 직교하지 않는 탄젠트가 "
                + std::to_string(gltfTangents.nonOrthogonal) + "개 있다(최대 |dot| "
                + std::to_string(gltfTangents.maxNormalDot) + ") — TBN 이 찌그러진다\n";
        }

        // 7. legacy(Assimp) 기준선
        const bridge::LoadedPair legacyPair = bridge::LoadAndBridge(modelPath);
        if (!legacyPair.result.Succeeded())
        {
            outLog += "  [note] legacy 기준선을 만들지 못해 비교를 건너뛴다\n";
            outLog += "  구조 검증 오류: " + std::to_string(errors)
                + "건, Step 보간 실패: " + std::to_string(stepFailures)
                + "건, 탄젠트 실패: " + std::to_string(tangentFailures)
                + "건, Material 계약 실패: "
                + std::to_string(materialContractFailures)
                + "건, V3 layout 실패: "
                + std::to_string(vertexLayoutFailures) + "건\n";
            const bool selfOnly =
                errors == 0 && stepFailures == 0 && tangentFailures == 0
                && materialContractFailures == 0 && vertexLayoutFailures == 0;
            outLog += std::string("  결과: ")
                + (selfOnly ? "통과(비교 없음)" : "실패") + "\n";
            return selfOnly;
        }
        const ex::Model& legacy = *legacyPair.result.model;
        const ex::Model& fromGltf = *published.model;

        // 8. 파서 무관 신호로 비교
        GltfDiffLog diff{ outLog };

        const std::size_t legacyTriangles = TriangleCount(legacy);
        const std::size_t gltfTriangles = TriangleCount(fromGltf);
        char sizeLine[240];
        std::snprintf(sizeLine, sizeof(sizeLine),
            "  규모 비교(Assimp → %.*s): 정점 %zu → %zu, 삼각형 %zu → %zu,"
            " 메시 %zu → %zu\n",
            static_cast<int>(label.size()), label.data(),
            VertexCount(legacy), VertexCount(fromGltf),
            legacyTriangles, gltfTriangles,
            legacy.Meshes().size(), fromGltf.Meshes().size());
        outLog += sizeLine;
        if (legacyTriangles != gltfTriangles)
        {
            // JoinIdenticalVertices 는 정점만 합치고 삼각형은 보존한다.
            // 삼각형 수가 다르면 기하 자체가 달라진 것이다.
            diff.Add("삼각형 수 불일치 " + std::to_string(legacyTriangles)
                + " vs " + std::to_string(gltfTriangles));
        }

        const Aabb legacyBounds = ModelBounds(legacy);
        const Aabb gltfBounds = ModelBounds(fromGltf);
        if (legacyBounds.valid && gltfBounds.valid)
        {
            float maxDelta = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                maxDelta = (std::max)(maxDelta,
                    std::abs(legacyBounds.minimum[i] - gltfBounds.minimum[i]));
                maxDelta = (std::max)(maxDelta,
                    std::abs(legacyBounds.maximum[i] - gltfBounds.maximum[i]));
            }
            char boundsLine[200];
            std::snprintf(boundsLine, sizeof(boundsLine),
                "  AABB 최대 편차: %.6f → %s (좌표 규약 일치 검증)\n", maxDelta,
                maxDelta <= BoundsEpsilon ? "일치" : "불일치");
            outLog += boundsLine;
            if (maxDelta > BoundsEpsilon)
            {
                // 편차 하나로는 무엇이 틀렸는지 모른다. 축 부호가 뒤집힌 것과
                // 단위가 어긋난 것과 변환이 통째로 빠진 것이 같은 숫자를 낸다
                // — 실값을 함께 찍어야 원인을 가를 수 있다.
                char values[320];
                std::snprintf(values, sizeof(values),
                    "  [values] Assimp min(%.4f, %.4f, %.4f) max(%.4f, %.4f, %.4f)\n"
                    "  [values] %.*s   min(%.4f, %.4f, %.4f) max(%.4f, %.4f, %.4f)\n",
                    legacyBounds.minimum[0], legacyBounds.minimum[1],
                    legacyBounds.minimum[2], legacyBounds.maximum[0],
                    legacyBounds.maximum[1], legacyBounds.maximum[2],
                    static_cast<int>(label.size()), label.data(),
                    gltfBounds.minimum[0], gltfBounds.minimum[1],
                    gltfBounds.minimum[2], gltfBounds.maximum[0],
                    gltfBounds.maximum[1], gltfBounds.maximum[2]);
                outLog += values;
                diff.Add("메시 로컬 AABB 가 어긋난다 — 좌표 변환이 legacy 와 다르다");
            }
        }

        {
            std::vector<std::string> a, b;
            for (const ex::ModelNode& node : legacy.Nodes()) a.push_back(node.name);
            for (const ex::ModelNode& node : fromGltf.Nodes()) b.push_back(node.name);

            // 루트 이름은 파서 관례다 — Assimp 는 없는 루트에 "RootNode" 를
            // 지어 붙이고 ufbx 는 원본의 빈 이름을 그대로 둔다. 계층에서 같은
            // 자리(첫 노드)인데 이름만 다른 것을 누락으로 세면 거짓 실패가 된다.
            // 자리까지 같은지 확인한 뒤에만 제외한다 — 이름만 보고 빼면 진짜
            // 노드 누락을 함께 삼킨다.
            if (!a.empty() && !b.empty() && a.front() != b.front()
                && (a.front().empty() || b.front().empty()))
            {
                outLog += "  [artifact] 루트 이름이 다르다(Assimp '"
                    + (a.front().empty() ? std::string("<이름 없음>") : a.front())
                    + "' vs " + std::string(label) + " '"
                    + (b.front().empty() ? std::string("<이름 없음>") : b.front())
                    + "') — 파서 관례라 누락으로 세지 않는다\n";
                a.erase(a.begin());
                b.erase(b.begin());
            }
            CompareNameSets(std::move(a), std::move(b), "node", label, diff, outLog);
        }
        {
            // 재질은 이름 집합만으로 판단하면 안 된다. legacy 에만 있는 재질이
            // **어떤 메시도 참조하지 않는다면** 그것은 파서가 덧붙인 유령이지
            // fastgltf 경로의 누락이 아니다. 참조 여부로 갈라 판정한다.
            std::vector<std::uint8_t> referenced(legacy.Materials().size(), 0);
            for (const ex::Mesh& mesh : legacy.Meshes())
            {
                if (ex::IsInRange(mesh.material, referenced.size()))
                    referenced[mesh.material.Value()] = 1;
            }

            std::vector<std::string> gltfNames;
            for (const ex::Material& m : fromGltf.Materials())
                gltfNames.push_back(m.name);

            const auto join = [](const std::vector<std::string>& names)
            {
                std::string out;
                for (const std::string& name : names)
                {
                    if (!out.empty()) out += ", ";
                    out += name.empty() ? "<이름 없음>" : name;
                }
                return out;
            };

            std::vector<std::string> legacyNames;
            std::size_t phantomCount = 0;
            for (std::size_t i = 0; i < legacy.Materials().size(); ++i)
            {
                const std::string& name = legacy.Materials()[i].name;
                legacyNames.push_back(name);
                const bool present = std::ranges::find(gltfNames, name) != gltfNames.end();
                if (present) continue;
                if (!referenced[i])
                {
                    ++phantomCount;
                    outLog += "  [artifact] material '" + name
                        + "' 은 Assimp 에만 있고 어떤 메시도 참조하지 않는다 "
                          "— 파서가 덧붙인 유령이라 누락으로 세지 않는다\n";
                }
                else
                {
                    diff.Add("material '" + name + "' 이 "
                        + std::string(label) + " 경로에서 누락됐다(메시가 참조 중)");
                }
            }
            if (legacyNames != gltfNames)
            {
                outLog += "  [재질 목록] Assimp: [" + join(legacyNames) + "]\n";
                outLog += "             " + std::string(label) + ": ["
                    + join(gltfNames) + "]\n";
            }
            for (const std::string& name : gltfNames)
            {
                if (std::ranges::find(legacyNames, name) == legacyNames.end())
                {
                    outLog += "  [gain] material '" + name + "' 은 "
                        + std::string(label) + " 경로에만 있다(보존 개선)\n";
                }
            }
            (void)phantomCount;
        }

        const ex::Skeleton* legacySkeleton = legacy.TryGetSkeleton();
        const ex::Skeleton* gltfSkeleton = fromGltf.TryGetSkeleton();
        if ((legacySkeleton != nullptr) != (gltfSkeleton != nullptr))
        {
            diff.Add("skeleton 존재 여부가 다르다");
        }
        else if (legacySkeleton && gltfSkeleton)
        {
            std::vector<std::string> a, b;
            for (const ex::Bone& bone : legacySkeleton->bones) a.push_back(bone.name);
            for (const ex::Bone& bone : gltfSkeleton->bones) b.push_back(bone.name);
            CompareNameSets(std::move(a), std::move(b), "bone", label, diff, outLog);

            std::vector<std::string> ca, cb;
            for (const ex::AnimationClip& clip : legacySkeleton->clips)
                ca.push_back(clip.name);
            for (const ex::AnimationClip& clip : gltfSkeleton->clips)
                cb.push_back(clip.name);
            CompareNameSets(std::move(ca), std::move(cb), "clip", label, diff, outLog);

            std::size_t legacyChannels = 0, gltfChannels = 0;
            for (const ex::AnimationClip& clip : legacySkeleton->clips)
                legacyChannels += clip.channels.size();
            for (const ex::AnimationClip& clip : gltfSkeleton->clips)
                gltfChannels += clip.channels.size();
            char animLine[200];
            std::snprintf(animLine, sizeof(animLine),
                "  스켈레톤 비교: bone %zu → %zu, clip %zu → %zu, 채널 %zu → %zu\n",
                legacySkeleton->bones.size(), gltfSkeleton->bones.size(),
                legacySkeleton->clips.size(), gltfSkeleton->clips.size(),
                legacyChannels, gltfChannels);
            outLog += animLine;
        }

        // legacy 도 같은 자로 잰다. 두 경로가 서로 다른 알고리즘(Assimp
        // CalcTangentSpace vs mikktspace)이므로 값은 당연히 다르고, 그것을
        // 실패로 세면 안 된다. 다만 **TBN 이 성립하는가**는 알고리즘과 무관한
        // 최소 요건이라 양쪽에 같은 자를 대 비교값으로 남긴다.
        {
            const TangentAudit legacyTangents = AuditTangents(legacy);
            outLog += FormatTangentAudit("Assimp", legacyTangents);
        }

        outLog += "  구조 불일치: " + std::to_string(diff.count) + "건, 검증 오류 "
            + std::to_string(errors) + "건, Step 보간 실패 "
            + std::to_string(stepFailures) + "건, 탄젠트 실패 "
            + std::to_string(tangentFailures) + "건, Material 계약 실패 "
            + std::to_string(materialContractFailures)
            + "건, V3 layout 실패 " + std::to_string(vertexLayoutFailures) + "건\n";
        const bool passed = diff.count == 0 && errors == 0
            && stepFailures == 0 && tangentFailures == 0
            && materialContractFailures == 0 && vertexLayoutFailures == 0;
        outLog += std::string("  결과: ") + (passed ? "통과" : "실패") + "\n";
        return passed;
    }
    }

    bool RunExperimentGltfImportSelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        im::GltfImporter importer;
        return RunImportSelfTest(
            importer, "experiment.gltf", "fastgltf", modelPath, outLog);
    }

    bool RunExperimentFbxImportSelfTest(
        const std::string& modelPath, std::string& outLog)
    {
        im::FbxImporter importer;
        return RunImportSelfTest(
            importer, "experiment.fbx", "ufbx", modelPath, outLog);
    }
}
