#include "ExperimentParity/ExperimentCookedSelfTest.h"

#include "Experiment/Cooked/CookedAssetManifest.h"
#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Cooked/ModelCookIdentity.h"
#include "Experiment/Import/ImporterModelDecoder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace RenderTest
{
    namespace
    {
        namespace ex = experiment;
        namespace ck = experiment::cooked;
        namespace im = experiment::importer;

        // 단정 하나. 실패해도 계속 진행한다 — 첫 실패에서 멈추면 "몇 개가
        // 깨졌는가"를 못 보고, 그러면 변이가 정확히 몇 건을 빨갛게 만드는지도
        // 셀 수 없다(이 저장소의 이빨 증명 규칙).
        struct Checker final
        {
            std::string& log;
            std::size_t passed{};
            std::size_t failed{};

            void Check(bool condition, const std::string& what)
            {
                if (condition) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }

            template <typename T>
            void Equal(const T& actual, const T& expected, const std::string& what)
            {
                if (actual == expected) { ++passed; return; }
                ++failed;
                log += "    [실패] " + what + "\n";
            }
        };

        [[nodiscard]] bool SameFloat(float a, float b) noexcept
        {
            // 쿠킹은 **무손실**이다. 근사 비교를 쓰면 양자화 버그가 통과한다.
            return std::memcmp(&a, &b, sizeof(float)) == 0;
        }

        [[nodiscard]] bool SameDouble(double a, double b) noexcept
        {
            return std::memcmp(&a, &b, sizeof(double)) == 0;
        }

        [[nodiscard]] bool Same(const math::vector2& a, const math::vector2& b) noexcept
        {
            return SameFloat(a.x, b.x) && SameFloat(a.y, b.y);
        }
        [[nodiscard]] bool Same(const math::vector3& a, const math::vector3& b) noexcept
        {
            return SameFloat(a.x, b.x) && SameFloat(a.y, b.y) && SameFloat(a.z, b.z);
        }
        [[nodiscard]] bool Same(const math::vector4& a, const math::vector4& b) noexcept
        {
            return SameFloat(a.x, b.x) && SameFloat(a.y, b.y)
                && SameFloat(a.z, b.z) && SameFloat(a.w, b.w);
        }
        [[nodiscard]] bool Same(const math::matrix4x4& a, const math::matrix4x4& b) noexcept
        {
            for (int row = 0; row < 4; ++row)
                for (int column = 0; column < 4; ++column)
                    if (!SameFloat(a.m[row][column], b.m[row][column])) return false;
            return true;
        }
        [[nodiscard]] bool Same(const math::aabb& a, const math::aabb& b) noexcept
        {
            return Same(a.center, b.center) && Same(a.extents, b.extents);
        }
        [[nodiscard]] bool Same(const ex::Vertex& a, const ex::Vertex& b) noexcept
        {
            // 바이트 비교로 충분하다 — Vertex 는 trivially copyable 이고
            // 쿠킹은 무손실이므로 한 비트도 달라지면 안 된다.
            return std::memcmp(&a, &b, sizeof(ex::Vertex)) == 0;
        }

        template <typename Key>
        [[nodiscard]] bool SameKeys(const std::vector<Key>& a, const std::vector<Key>& b)
        {
            if (a.size() != b.size()) return false;
            if (a.empty()) return true;
            return std::memcmp(a.data(), b.data(), a.size() * sizeof(Key)) == 0;
        }

        [[nodiscard]] bool SameValue(const ex::MaterialPropertyValue& a,
            const ex::MaterialPropertyValue& b)
        {
            if (a.index() != b.index()) return false;
            return std::visit([&](const auto& left) -> bool
            {
                using T = std::decay_t<decltype(left)>;
                const auto& right = std::get<T>(b);
                if constexpr (std::is_same_v<T, float>) return SameFloat(left, right);
                else if constexpr (std::is_same_v<T, math::vector2>) return Same(left, right);
                else if constexpr (std::is_same_v<T, math::vector3>) return Same(left, right);
                else if constexpr (std::is_same_v<T, math::vector4>) return Same(left, right);
                else if constexpr (std::is_same_v<T, ex::TextureReference>)
                {
                    return left.assetId == right.assetId
                        && left.logicalName == right.logicalName
                        && left.fallbackPath == right.fallbackPath
                        && left.colorSpace == right.colorSpace;
                }
                else return left == right;
            }, a);
        }

        // draft 두 개가 **모든 값에서** 같은가.
        void CompareDrafts(const ex::ModelDraft& a, const ex::ModelDraft& b,
            Checker& check, const std::string& label)
        {
            const auto tag = [&](const char* what) { return label + " " + what; };

            check.Equal(b.metadata.assetId, a.metadata.assetId, tag("metadata.assetId"));
            check.Equal(b.metadata.name, a.metadata.name, tag("metadata.name"));
            check.Equal(b.metadata.sourcePath, a.metadata.sourcePath, tag("metadata.sourcePath"));
            check.Equal(b.metadata.cookedPath, a.metadata.cookedPath, tag("metadata.cookedPath"));
            check.Check(b.metadata.sourceWriteTime == a.metadata.sourceWriteTime,
                tag("metadata.sourceWriteTime"));

            check.Equal(b.nodes.size(), a.nodes.size(), tag("노드 수"));
            for (std::size_t i = 0; i < a.nodes.size() && i < b.nodes.size(); ++i)
            {
                const auto& left = a.nodes[i];
                const auto& right = b.nodes[i];
                const std::string at = tag("노드[") + std::to_string(i) + "]";
                check.Equal(right.name, left.name, at + ".name");
                check.Equal(right.parent, left.parent, at + ".parent");
                check.Check(Same(right.localTransform, left.localTransform), at + ".transform");
                check.Equal(right.meshes, left.meshes, at + ".meshes");
            }

            check.Equal(b.meshes.size(), a.meshes.size(), tag("메시 수"));
            for (std::size_t i = 0; i < a.meshes.size() && i < b.meshes.size(); ++i)
            {
                const auto& left = a.meshes[i];
                const auto& right = b.meshes[i];
                const std::string at = tag("메시[") + std::to_string(i) + "]";
                check.Equal(right.name, left.name, at + ".name");
                check.Equal(right.material, left.material, at + ".material");
                check.Check(Same(right.bounds, left.bounds), at + ".bounds");
                check.Equal(right.indices, left.indices, at + ".indices");
                check.Equal(right.vertices.size(), left.vertices.size(), at + ".정점 수");
                check.Equal(right.vertices.AttributeMask(), left.vertices.AttributeMask(),
                    at + ".정점 mask");
                check.Equal(right.vertices.Stride(), left.vertices.Stride(),
                    at + ".정점 stride");
                check.Equal(right.vertices.ByteSize(), left.vertices.ByteSize(),
                    at + ".정점 byte 수");

                bool sameVertices = right.vertices.size() == left.vertices.size();
                for (std::size_t v = 0; sameVertices && v < left.vertices.size(); ++v)
                    sameVertices = Same(left.vertices[v], right.vertices[v]);
                check.Check(sameVertices, at + ".정점 값(비트 단위)");
                check.Check(std::ranges::equal(
                    left.vertices.Bytes(), right.vertices.Bytes()),
                    at + ".packed 정점 byte(비트 단위)");
            }

            check.Equal(b.materials.size(), a.materials.size(), tag("재질 수"));
            for (std::size_t i = 0; i < a.materials.size() && i < b.materials.size(); ++i)
            {
                const auto& left = a.materials[i];
                const auto& right = b.materials[i];
                const std::string at = tag("재질[") + std::to_string(i) + "]";
                check.Equal(right.assetId, left.assetId, at + ".assetId");
                check.Equal(right.shaderAssetId, left.shaderAssetId, at + ".shaderAssetId");
                check.Equal(right.name, left.name, at + ".name");
                check.Check(right.blendMode == left.blendMode, at + ".blendMode");
                check.Equal(right.keywords, left.keywords, at + ".keywords");
                check.Equal(right.keywordSelections, left.keywordSelections,
                    at + ".keywordSelections");
                check.Equal(right.properties.size(), left.properties.size(),
                    at + ".property 수");
                for (std::size_t p = 0;
                    p < left.properties.size() && p < right.properties.size(); ++p)
                {
                    const std::string pat = at + ".property[" + std::to_string(p) + "]";
                    check.Equal(right.properties[p].name, left.properties[p].name,
                        pat + ".name");
                    check.Check(SameValue(left.properties[p].value,
                        right.properties[p].value), pat + ".value");
                }
            }

            check.Equal(b.skeleton.has_value(), a.skeleton.has_value(), tag("스켈레톤 유무"));
            if (a.skeleton.has_value() && b.skeleton.has_value())
            {
                const auto& left = *a.skeleton;
                const auto& right = *b.skeleton;
                check.Equal(right.rootBone, left.rootBone, tag("skeleton.rootBone"));
                check.Check(Same(right.rootTransform, left.rootTransform),
                    tag("skeleton.rootTransform"));
                check.Check(Same(right.globalInverseTransform, left.globalInverseTransform),
                    tag("skeleton.globalInverseTransform"));

                check.Equal(right.bones.size(), left.bones.size(), tag("뼈 수"));
                for (std::size_t i = 0; i < left.bones.size() && i < right.bones.size(); ++i)
                {
                    const std::string at = tag("뼈[") + std::to_string(i) + "]";
                    check.Equal(right.bones[i].name, left.bones[i].name, at + ".name");
                    check.Equal(right.bones[i].parent, left.bones[i].parent, at + ".parent");
                    check.Check(Same(right.bones[i].inverseBindMatrix,
                        left.bones[i].inverseBindMatrix), at + ".inverseBind");
                }

                check.Equal(right.clips.size(), left.clips.size(), tag("클립 수"));
                for (std::size_t c = 0; c < left.clips.size() && c < right.clips.size(); ++c)
                {
                    const auto& lc = left.clips[c];
                    const auto& rc = right.clips[c];
                    const std::string at = tag("클립[") + std::to_string(c) + "]";
                    check.Equal(rc.name, lc.name, at + ".name");
                    check.Check(SameDouble(rc.durationTicks, lc.durationTicks),
                        at + ".durationTicks");
                    check.Check(SameDouble(rc.ticksPerSecond, lc.ticksPerSecond),
                        at + ".ticksPerSecond");
                    check.Equal(rc.looping, lc.looping, at + ".looping");
                    check.Equal(rc.channels.size(), lc.channels.size(), at + ".채널 수");

                    for (std::size_t n = 0;
                        n < lc.channels.size() && n < rc.channels.size(); ++n)
                    {
                        const auto& lch = lc.channels[n];
                        const auto& rch = rc.channels[n];
                        const std::string cat = at + ".채널[" + std::to_string(n) + "]";
                        check.Equal(rch.bone, lch.bone, cat + ".bone");
                        check.Check(rch.translationInterpolation
                            == lch.translationInterpolation, cat + ".transInterp");
                        check.Check(rch.rotationInterpolation
                            == lch.rotationInterpolation, cat + ".rotInterp");
                        check.Check(rch.scaleInterpolation
                            == lch.scaleInterpolation, cat + ".scaleInterp");
                        check.Check(SameKeys(lch.translations, rch.translations),
                            cat + ".translations(비트 단위)");
                        check.Check(SameKeys(lch.rotations, rch.rotations),
                            cat + ".rotations(비트 단위)");
                        check.Check(SameKeys(lch.scales, rch.scales),
                            cat + ".scales(비트 단위)");
                    }
                }
            }

            check.Equal(b.animator.has_value(), a.animator.has_value(), tag("애니메이터 유무"));
            if (a.animator.has_value() && b.animator.has_value())
            {
                check.Equal(b.animator->motionAssetId, a.animator->motionAssetId,
                    tag("animator.motionAssetId"));
                check.Equal(b.animator->defaultClip, a.animator->defaultClip,
                    tag("animator.defaultClip"));
            }
        }

        // ── 합성 draft ──────────────────────────────────────────────────
        // 실자산이 우연히 밟지 않는 형태를 일부러 넣는다.
        [[nodiscard]] ex::ModelDraft MakeSyntheticDraft()
        {
            ex::ModelDraft draft{};

            draft.metadata.name = "합성 모델";                    // 비 ASCII
            draft.metadata.sourcePath = u8"C:/자산/모델 이름.glb"; // 유니코드 + 공백
            draft.metadata.cookedPath = u8"C:/파생/모델.cemc";
            draft.metadata.assetId.value = Uuid::Parse("11111111-2222-3333-4444-555555555555");

            // 노드 3개 — 하나는 이름이 비었고, 하나는 메시가 없다.
            ex::ModelNode root{};
            root.name = "root";
            root.meshes = { ex::MeshIndex(0), ex::MeshIndex(1), ex::MeshIndex(2) };
            draft.nodes.push_back(root);

            ex::ModelNode empty{};
            empty.name.clear();                       // ★ 빈 이름
            empty.parent = ex::NodeIndex(0);
            draft.nodes.push_back(empty);

            ex::ModelNode leaf{};
            leaf.name = "leaf";
            leaf.parent = ex::NodeIndex(1);
            leaf.localTransform.m[0][3] = 1.5f;
            draft.nodes.push_back(leaf);              // ★ 메시 0개

            // 메시 3개 — all-attribute, static core, 빈 메시를 각각 넣는다.
            ex::Mesh mesh{};
            mesh.name = "mesh0";
            mesh.material = ex::MaterialIndex(0);
            (void)mesh.vertices.SetLayout(ex::kAllVertexAttributes);
            for (std::uint32_t i = 0; i < 3; ++i)
            {
                ex::Vertex vertex{};
                vertex.position = { static_cast<float>(i), 1.0f, -2.5f };
                vertex.normal = { 0.0f, 1.0f, 0.0f };
                vertex.uv0 = { 0.25f, 0.75f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f, -1.0f };
                vertex.boneIndices[0] = 0;
                vertex.boneWeights[0] = 1.0f;
                const math::vector2 uv1{ 0.5f, 0.5f };
                const math::vector4 color{ 1.0f, 0.5f, 0.25f, 1.0f };
                (void)mesh.vertices.Append(vertex, &uv1, &color);
            }
            mesh.indices = { 0u, 1u, 2u };
            mesh.bounds = math::aabb::from_min_max(
                math::vector3{ 0.0f, 1.0f, -2.5f },
                math::vector3{ 2.0f, 1.0f, -2.5f });
            draft.meshes.push_back(mesh);

            ex::Mesh staticMesh{};
            staticMesh.name = "mesh_static_core";
            staticMesh.material = ex::MaterialIndex(1);
            for (std::uint32_t i = 0; i < 3; ++i)
            {
                ex::Vertex vertex{};
                vertex.position = { static_cast<float>(i), 0.0f, 0.0f };
                vertex.normal = { 0.0f, 1.0f, 0.0f };
                vertex.uv0 = { 0.0f, 0.0f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
                staticMesh.vertices.push_back(vertex);
            }
            staticMesh.indices = { 0u, 1u, 2u };
            staticMesh.bounds = math::aabb::from_min_max(
                math::vector3{ 0.0f, 0.0f, 0.0f },
                math::vector3{ 2.0f, 0.0f, 0.0f });
            draft.meshes.push_back(staticMesh);

            ex::Mesh emptyMesh{};                     // ★ 정점·인덱스 0개
            emptyMesh.name = "mesh_empty";
            emptyMesh.material = ex::MaterialIndex(1);
            draft.meshes.push_back(emptyMesh);

            // 재질 2개 — property 대안 9가지를 모두 덮는다.
            ex::Material material{};
            material.name = "재질0";
            material.assetId.value = Uuid::Parse("66666666-7777-8888-9999-aaaaaaaaaaaa");
            material.shaderAssetId.value = Uuid::Parse("bbbbbbbb-cccc-dddd-eeee-ffffffffffff");
            material.blendMode = ex::MaterialBlendMode::Transparent;
            material.keywords = { "_NORMALMAP", "" };  // ★ 빈 키워드
            material.keywordSelections = { 0u, 3u, 65535u };

            const auto property = [](const char* name, ex::MaterialPropertyValue value)
            {
                ex::MaterialProperty p{};
                p.name = name;
                p.value = std::move(value);
                return p;
            };
            ex::TextureReference texture{};
            texture.assetId.value = Uuid::Parse("01234567-89ab-cdef-0123-456789abcdef");
            texture.logicalName = "_BaseMap";
            texture.fallbackPath = u8"C:/자산/텍스처 이름.png";
            texture.colorSpace = ex::TextureColorSpace::Srgb;

            material.properties.push_back(property("b", true));
            material.properties.push_back(property("i", std::int32_t{ -7 }));
            material.properties.push_back(property("u", std::uint32_t{ 9u }));
            material.properties.push_back(property("f", 0.125f));
            material.properties.push_back(property("f2", math::vector2{ 1.0f, 2.0f }));
            material.properties.push_back(property("f3", math::vector3{ 1.0f, 2.0f, 3.0f }));
            material.properties.push_back(
                property("f4", math::vector4{ 1.0f, 2.0f, 3.0f, 4.0f }));
            material.properties.push_back(property("s", std::string{ "문자열 값" }));
            material.properties.push_back(property("t", texture));
            draft.materials.push_back(material);

            ex::Material bare{};                      // ★ property·keyword 전부 0개
            bare.name = "재질1";
            bare.assetId.value = Uuid::Parse("77777777-7777-4777-8777-777777777777");
            bare.shaderAssetId = material.shaderAssetId;
            draft.materials.push_back(bare);

            // 스켈레톤 — 채널마다 보간이 다르고 트랙 개수도 다르다.
            ex::Skeleton skeleton{};
            skeleton.rootBone = ex::BoneIndex(0);
            skeleton.rootTransform.m[0][0] = 2.0f;
            skeleton.globalInverseTransform.m[1][1] = 0.5f;

            ex::Bone bone0{};
            bone0.name = "mixamorig:Hips";
            skeleton.bones.push_back(bone0);
            ex::Bone bone1{};
            bone1.name = "mixamorig:Spine";
            bone1.parent = ex::BoneIndex(0);
            bone1.inverseBindMatrix.m[1][3] = -1.25f;
            skeleton.bones.push_back(bone1);

            ex::AnimationClip clip{};
            clip.name = "die";
            clip.durationTicks = 41.0;
            clip.ticksPerSecond = 30.0;
            clip.looping = false;

            ex::AnimationChannel channel0{};
            channel0.bone = ex::BoneIndex(0);
            channel0.rotationInterpolation = ex::InterpolationMode::Step;  // ★ 트랙별 상이
            channel0.translations = {
                { 0.0, { 0.0f, 0.0f, 0.0f } }, { 1.0, { 1.0f, 2.0f, 3.0f } } };
            channel0.rotations = { { 0.0, { 0.0f, 0.0f, 0.0f, 1.0f } } };
            // ★ scales 는 비운다 — 빈 트랙이 범위 계산에서 죽지 않는지 본다.
            clip.channels.push_back(channel0);

            ex::AnimationChannel channel1{};
            channel1.bone = ex::BoneIndex(1);
            channel1.translationInterpolation = ex::InterpolationMode::Step;
            channel1.scaleInterpolation = ex::InterpolationMode::Step;
            // ★ 번째 채널의 키는 반드시 **0이 아닌 오프셋**에서 시작해야 한다.
            //   처음엔 channel1 의 translations 를 비워 둔 탓에 모든
            //   begin 이 0 이었고, 그래서 "시작 오프셋을 무시한다"는 변이가
            //   합성 검사를 그대로 통과했다(실자산만 616건 빨개졌다).
            //   범위 계산을 검사하려면 범위가 서로 달라야 한다.
            channel1.translations = {
                { 0.0, { 9.0f, 8.0f, 7.0f } }, { 2.0, { -1.0f, -2.0f, -3.0f } } };
            channel1.rotations = {
                { 0.25, { 0.5f, 0.5f, 0.5f, 0.5f } },
                { 1.75, { 0.0f, 0.70710678f, 0.0f, 0.70710678f } } };
            channel1.scales = { { 0.5, { 2.0f, 2.0f, 2.0f } } };
            clip.channels.push_back(channel1);

            // ★ 세 번째 채널은 일부만 비운다 — "빈 트랙이 뒤따르는 범위를
            //   망가뜨리지 않는가"를 보려면 빈 트랙이 끝이 아니고 앞뒤가 채워져
            //   있어야 한다.
            ex::AnimationChannel channel2{};
            channel2.bone = ex::BoneIndex(1);
            channel2.translations = { { 3.0, { 4.0f, 5.0f, 6.0f } } };
            // rotations 는 비운다.
            channel2.scales = { { 3.0, { 1.0f, 1.0f, 1.0f } } };
            clip.channels.push_back(channel2);

            skeleton.clips.push_back(clip);
            skeleton.clips.push_back(ex::AnimationClip{});  // ★ 채널 0개 클립
            draft.skeleton = std::move(skeleton);

            ex::AnimatorData animator{};
            animator.motionAssetId.value = Uuid::Parse("fedcba98-7654-3210-fedc-ba9876543210");
            animator.defaultClip = ex::AnimationClipIndex(0);
            draft.animator = animator;

            return draft;
        }

        // 거부되어야 하는 페이로드 하나를 검사한다.
        //
        // ★ "false 를 돌려줬다"만 보면 부족하다. **draft 가 나오지 않았는지**도
        //   봐야 한다 — 거부해 놓고 빈 모델을 채워 주면 legacy 의 조용한 오독을
        //   이름만 바꿔 물려받는 것이다.
        void ExpectRejected(std::vector<std::byte> bytes, const std::string& what,
            Checker& check)
        {
            ex::ModelDraft draft{};
            std::vector<ex::ModelLoadIssue> issues;
            const bool accepted = ck::Read(bytes, draft, issues);

            check.Check(!accepted, "거부되어야 한다: " + what);
            check.Check(!issues.empty(), "거부 사유가 남아야 한다: " + what);
            check.Check(draft.nodes.empty() && draft.meshes.empty()
                && !draft.skeleton.has_value(),
                "거부 후 draft 가 비어야 한다: " + what);
            for (const ex::ModelLoadIssue& issue : issues)
            {
                check.Check(issue.code == ex::ModelLoadIssueCode::CookedPayloadRejected,
                    "거부 코드는 CookedPayloadRejected 여야 한다: " + what);
                // 거부는 정상 폴백이므로 Error 로 올리지 않는다.
                check.Check(issue.severity != ex::ModelLoadIssueSeverity::Error,
                    "거부는 Error 가 아니어야 한다: " + what);
            }
        }

        // source preview에서는 아직 catalog identity가 비어 있을 수 있지만, 그
        // 상태를 cooked artifact로 게시하는 것은 실패해야 한다. fallback path가
        // 있더라도 texture AssetId를 대신하지 못한다.
        void ExpectWriteRejected(const ex::ModelDraft& draft,
            ex::ModelLoadIssueCode expectedCode, const std::string& expectedContext,
            const std::string& what, Checker& check)
        {
            const ck::CookedWriteResult result = ck::Write(draft);
            check.Check(!result.Succeeded(), "쿠킹 게시가 거부되어야 한다: " + what);
            check.Check(result.bytes.empty(), "거부된 쿠킹 바이트가 비어야 한다: " + what);
            check.Check(!result.issues.empty(), "쿠킹 거부 사유가 남아야 한다: " + what);
            check.Check(std::ranges::any_of(result.issues,
                [&](const ex::ModelLoadIssue& issue)
                {
                    return issue.code == expectedCode
                        && issue.context == expectedContext;
                }), "쿠킹 거부 code/context가 정확해야 한다: " + what);
            for (const ex::ModelLoadIssue& issue : result.issues)
            {
                check.Check(issue.severity == ex::ModelLoadIssueSeverity::Error,
                    "쿠킹 게시 거부는 Error여야 한다: " + what);
            }
        }

        // 실제 catalog 배선 전의 codec fixture 전용 identity다. source path나
        // material 이름으로 ID를 만들지 않고, v4 shape의 분리된 domain/index를
        // 사용해 한 번의 decode 안에서만 결정적으로 유일하게 만든다.
        [[nodiscard]] ex::AssetId MakeFixtureAssetId(
            std::uint8_t domain, std::size_t index = 0) noexcept
        {
            ex::AssetId id{};
            id.value.data[0] = domain;
            for (std::size_t byte = 0; byte < sizeof(index); ++byte)
            {
                id.value.data[15 - byte] = static_cast<std::uint8_t>(
                    (index >> (byte * 8u)) & std::size_t{ 0xffu });
            }
            id.value.data[6] = 0x40u;
            id.value.data[8] = static_cast<std::uint8_t>(
                (id.value.data[8] & 0x3fu) | 0x80u);
            return id;
        }

        [[nodiscard]] bool ReadTextFile(const std::filesystem::path& path,
            std::string& out)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream) return false;
            stream.seekg(0, std::ios::end);
            const std::streamoff bytes = stream.tellg();
            if (bytes < 0) return false;
            stream.seekg(0, std::ios::beg);
            out.resize(static_cast<std::size_t>(bytes));
            if (!out.empty())
                stream.read(out.data(), static_cast<std::streamsize>(out.size()));
            return stream.good() || stream.eof();
        }

        [[nodiscard]] ex::AssetId ReadMetaAssetId(
            const std::filesystem::path& metaPath, std::string& outFailure)
        {
            std::string text;
            if (!ReadTextFile(metaPath, text))
            {
                outFailure = "meta를 읽을 수 없다: " + metaPath.string();
                return {};
            }
            ex::AssetId id{};
            std::vector<ck::ModelIdentityIssue> issues;
            if (!ck::ReadAssetIdFromMeta(text, id, issues))
            {
                outFailure = "meta GUID를 읽을 수 없다: " + metaPath.string();
                if (!issues.empty()) outFailure += " (" + issues.front().message + ")";
                return {};
            }
            return id;
        }

        void RunIdentityAndManifestContractSelfTest(
            std::span<const std::byte> baked, Checker& check,
            std::string& outLog)
        {
            const std::string validSidecar =
                "guid: 10101010-1010-4010-8010-101010101010\n"
                "subAssets:\n"
                "  schemaVersion: 1\n"
                "  materials:\n"
                "    - key: gltf/material/0\n"
                "      name: Fixture\n"
                "      guid: 30303030-3030-4030-8030-303030303030\n"
                "  embeddedTextures:\n"
                "    - key: gltf/image/0\n"
                "      guid: 40404040-4040-4040-8040-404040404040\n";

            ck::ModelCookIdentity identity;
            std::vector<ck::ModelIdentityIssue> identityIssues;
            check.Check(ck::ReadModelCookIdentity(validSidecar,
                identity, identityIssues), "model subasset sidecar v1을 읽는다");
            check.Equal(identity.FindMaterial("gltf/material/0"),
                ex::AssetId{ Uuid::Parse("30303030-3030-4030-8030-303030303030") },
                "material source key가 저장된 UUIDv4로 해석된다");
            check.Equal(identity.FindEmbeddedTexture("gltf/image/0"),
                ex::AssetId{ Uuid::Parse("40404040-4040-4040-8040-404040404040") },
                "embedded texture source key가 저장된 UUIDv4로 해석된다");

            im::ImportedScene matchingScene;
            matchingScene.materials.emplace_back().sourceKey = "gltf/material/0";
            matchingScene.textures.emplace_back().sourceKey = "gltf/image/0";
            matchingScene.textures.back().embeddedBytes.push_back(std::byte{ 1u });
            identityIssues.clear();
            check.Check(ck::ValidateModelCookIdentity(matchingScene,
                identity, identityIssues),
                "sidecar subasset key가 현재 import 결과와 정확히 맞는다");

            const std::size_t identityFailedBefore = check.failed;
            {
                ck::ModelCookIdentity parsed;
                std::vector<ck::ModelIdentityIssue> issues;
                std::string invalid = validSidecar;
                invalid.replace(invalid.find("1010-4010"), 9u, "1010-5010");
                check.Check(!ck::ReadModelCookIdentity(invalid, parsed, issues),
                    "UUIDv5 model sidecar를 거부한다");
            }
            {
                ck::ModelCookIdentity parsed;
                std::vector<ck::ModelIdentityIssue> issues;
                std::string duplicate = validSidecar;
                const std::string textureGuid =
                    "40404040-4040-4040-8040-404040404040";
                duplicate.replace(duplicate.find(textureGuid), textureGuid.size(),
                    "30303030-3030-4030-8030-303030303030");
                check.Check(!ck::ReadModelCookIdentity(duplicate, parsed, issues),
                    "중복 model subasset UUIDv4를 거부한다");
            }
            {
                im::ImportedScene missing = matchingScene;
                missing.materials[0].sourceKey = "gltf/material/1";
                std::vector<ck::ModelIdentityIssue> issues;
                check.Check(!ck::ValidateModelCookIdentity(missing,
                    identity, issues), "누락/stale material source key를 거부한다");
            }
            {
                im::ImportedScene duplicate = matchingScene;
                duplicate.materials.push_back(duplicate.materials[0]);
                std::vector<ck::ModelIdentityIssue> issues;
                check.Check(!ck::ValidateModelCookIdentity(duplicate,
                    identity, issues), "중복 importer source key를 거부한다");
            }
            outLog += check.failed == identityFailedBefore
                ? "  model subasset identity 거부: 4/4\n"
                : "  model subasset identity 거부: 실패\n";

            ck::Sha256Digest digest{};
            std::string hashError;
            check.Check(ck::ComputeSha256(baked, digest, hashError),
                "cooked bytes의 SHA-256을 계산한다");

            const ex::AssetId modelId = MakeFixtureAssetId(0x10u);
            const ex::AssetId materialId = MakeFixtureAssetId(0x30u);
            const ex::AssetId shaderId = MakeFixtureAssetId(0x20u);
            const ex::AssetId textureId = MakeFixtureAssetId(0x40u);
            const std::string modelPath = ck::MakeDerivedModelArtifactPath(modelId);
            check.Check(modelPath ==
                "Derived/Models/10/10000000-0000-4000-8000-000000000000.cemc",
                "model GUID가 shard된 canonical Derived path를 만든다");

            const auto entry = [&](ex::AssetId id, ck::CookedAssetKind kind,
                std::string path, std::vector<ex::AssetId> dependencies)
            {
                ck::CookedAssetManifestEntry value;
                value.assetId = id;
                value.kind = kind;
                value.formatVersion = kind == ck::CookedAssetKind::Model
                    || kind == ck::CookedAssetKind::Material
                    ? ck::kFormatVersion : 1u;
                value.byteSize = baked.size();
                value.contentSha256 = digest;
                value.artifactPath = std::move(path);
                value.dependencies = std::move(dependencies);
                return value;
            };

            ck::CookedAssetManifest manifest;
            // 일부러 역순으로 넣어 writer의 canonical ordering을 검증한다.
            manifest.entries.push_back(entry(textureId, ck::CookedAssetKind::Texture,
                "Derived/Textures/40/texture.cetex", {}));
            manifest.entries.push_back(entry(shaderId, ck::CookedAssetKind::ShaderMeta,
                "Derived/Shaders/20/shader.ceshader", {}));
            manifest.entries.push_back(entry(materialId, ck::CookedAssetKind::Material,
                modelPath, { shaderId, textureId }));
            manifest.entries.push_back(entry(modelId, ck::CookedAssetKind::Model,
                modelPath, { materialId }));

            const ck::AssetManifestWriteResult write =
                ck::WriteAssetManifest(manifest);
            check.Check(write.Succeeded(), "GUID-addressed manifest를 쓴다");
            ck::CookedAssetManifest reversed = manifest;
            std::ranges::reverse(reversed.entries);
            const ck::AssetManifestWriteResult deterministic =
                ck::WriteAssetManifest(reversed);
            check.Check(deterministic.Succeeded()
                && deterministic.bytes == write.bytes,
                "manifest bytes가 입력 순서와 무관하게 결정적이다");

            ck::CookedAssetManifest restoredManifest;
            std::vector<ck::AssetManifestIssue> manifestIssues;
            check.Check(ck::ReadAssetManifest(write.bytes,
                restoredManifest, manifestIssues), "manifest를 엄격히 읽는다");
            const ck::CookedAssetManifestEntry* restoredModel =
                restoredManifest.Find(modelId);
            check.Check(restoredModel && restoredModel->artifactPath == modelPath,
                "model GUID lookup이 Derived artifact를 찾는다");
            manifestIssues.clear();
            check.Check(restoredModel && ck::VerifyArtifact(*restoredModel,
                baked.size(), digest, manifestIssues),
                "manifest size/SHA-256이 cooked artifact와 맞는다");

            const std::size_t manifestFailedBefore = check.failed;
            {
                ck::CookedAssetManifest invalid = manifest;
                invalid.entries[0].assetId = invalid.entries[1].assetId;
                check.Check(!ck::WriteAssetManifest(invalid).Succeeded(),
                    "중복 manifest GUID를 거부한다");
            }
            {
                ck::CookedAssetManifest invalid = manifest;
                invalid.entries[0].assetId = {};
                check.Check(!ck::WriteAssetManifest(invalid).Succeeded(),
                    "nil manifest GUID를 거부한다");
            }
            {
                ck::CookedAssetManifest invalid = manifest;
                invalid.entries[0].artifactPath = "Derived/../source.glb";
                check.Check(!ck::WriteAssetManifest(invalid).Succeeded(),
                    "path escape manifest를 거부한다");
            }
            {
                ck::CookedAssetManifest invalid = manifest;
                invalid.entries[2].dependencies.push_back(
                    MakeFixtureAssetId(0x70u));
                check.Check(!ck::WriteAssetManifest(invalid).Succeeded(),
                    "해석되지 않는 dependency GUID를 거부한다");
            }
            {
                ck::CookedAssetManifest invalid = manifest;
                invalid.entries[0].contentSha256 = {};
                check.Check(!ck::WriteAssetManifest(invalid).Succeeded(),
                    "빈 SHA-256 manifest entry를 거부한다");
            }
            {
                ck::Sha256Digest stale = digest;
                stale[0] ^= 1u;
                std::vector<ck::AssetManifestIssue> issues;
                check.Check(restoredModel && !ck::VerifyArtifact(*restoredModel,
                    baked.size(), stale, issues),
                    "stale artifact SHA-256을 거부한다");
            }
            outLog += check.failed == manifestFailedBefore
                ? "  cooked asset manifest 거부: 6/6\n"
                : "  cooked asset manifest 거부: 실패\n";
        }

		[[nodiscard]] std::optional<std::size_t> FirstMeshRecordOffset(
			std::span<const std::byte> bytes)
		{
			if (bytes.size() < sizeof(ck::FileHeader)) return std::nullopt;
			ck::FileHeader header{};
			std::memcpy(&header, bytes.data(), sizeof(header));
			const std::size_t tableBytes = static_cast<std::size_t>(header.sectionCount)
				* sizeof(ck::SectionEntry);
			if (sizeof(header) + tableBytes > bytes.size()
				|| bytes.size() < sizeof(ck::CookedMesh)) return std::nullopt;
			for (std::uint32_t i = 0; i < header.sectionCount; ++i)
			{
				ck::SectionEntry entry{};
				std::memcpy(&entry, bytes.data() + sizeof(header)
					+ static_cast<std::size_t>(i) * sizeof(entry), sizeof(entry));
				if (entry.kind != static_cast<std::uint32_t>(ck::SectionKind::Meshes)
					|| entry.elementCount == 0 || entry.bytes < sizeof(ck::CookedMesh)
					|| entry.offset > bytes.size() - sizeof(ck::CookedMesh))
				{
					continue;
				}
				return static_cast<std::size_t>(entry.offset);
			}
			return std::nullopt;
		}
    }

    bool RunExperimentCookedSelfTest(std::string& outLog)
    {
        outLog += "[experiment.cooked] 합성 검사\n";
        Checker check{ outLog };

        // ── 1. 왕복 무손실 ──────────────────────────────────────────────
        const ex::ModelDraft original = MakeSyntheticDraft();
        check.Check(original.meshes.size() >= 2, "V3 합성 mesh 2종이 있다");
        if (original.meshes.size() >= 2)
        {
            check.Equal(original.meshes[0].vertices.Stride(),
                ex::StrideOf(ex::kAllVertexAttributes),
                "optional+skin mesh는 전체 attribute stride다");
            check.Equal(original.meshes[1].vertices.Stride(), 48u,
                "static mesh는 core 48B다");
            check.Equal(original.meshes[1].vertices.ByteSize(),
                original.meshes[1].vertices.size() * std::size_t{ 48 },
                "static mesh가 skin byte를 내지 않는다");
        }
        const ck::CookedWriteResult bakedResult = ck::Write(original);
        check.Check(bakedResult.Succeeded(), "유효한 identity의 굽기가 성공한다");
        if (!bakedResult.Succeeded())
        {
            for (const ex::ModelLoadIssue& issue : bakedResult.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }
        const std::vector<std::byte>& baked = bakedResult.bytes;
        check.Check(!baked.empty(), "굽기 산출물이 비어 있지 않다");

        {
            ex::ModelDraft restored{};
            std::vector<ex::ModelLoadIssue> issues;
            const bool ok = ck::Read(baked, restored, issues);
            check.Check(ok, "정상 페이로드를 읽는다");
            if (ok) CompareDrafts(original, restored, check, "합성");
            else for (const auto& issue : issues) outLog += "    사유: " + issue.message + "\n";
        }

        // ── 2. 결정성 — 같은 입력은 같은 바이트 ─────────────────────────
        // 굽기가 비결정적이면 캐시가 매번 더러워지고 diff 가 무의미해진다.
        {
            const ck::CookedWriteResult again = ck::Write(original);
            check.Check(again.Succeeded(), "결정성 재굽기가 성공한다");
            check.Check(again.bytes == baked, "같은 draft 는 같은 바이트로 구워진다");
        }

        // ── 3. 게시 전 identity 거부 ────────────────────────────────────
        const std::size_t publicationFailedBefore = check.failed;
        {
            ex::ModelDraft invalid = original;
            invalid.metadata.assetId = {};
            ExpectWriteRejected(invalid, ex::ModelLoadIssueCode::InvalidAssetIdentity,
                "metadata.assetId", "nil model AssetId", check);
        }
        {
            ex::ModelDraft invalid = original;
            invalid.materials[0].assetId = {};
            ExpectWriteRejected(invalid, ex::ModelLoadIssueCode::InvalidAssetIdentity,
                "materials[0].assetId", "nil material AssetId", check);
        }
        {
            ex::ModelDraft invalid = original;
            invalid.materials[0].shaderAssetId = {};
            ExpectWriteRejected(invalid, ex::ModelLoadIssueCode::InvalidAssetIdentity,
                "materials[0].shaderAssetId", "nil ShaderMeta AssetId", check);
        }
        {
            ex::ModelDraft invalid = original;
            auto& texture = std::get<ex::TextureReference>(
                invalid.materials[0].properties.back().value);
            texture.assetId = {};
            check.Check(!texture.fallbackPath.empty(),
                "음성 fixture에 fallback path가 실제로 있다");
            ExpectWriteRejected(invalid, ex::ModelLoadIssueCode::InvalidTextureReference,
                "materials[0].properties[8].textureAssetId",
                "fallback path만 있는 texture", check);
        }
        {
            ex::ModelDraft invalid = original;
            invalid.materials[1].assetId = invalid.materials[0].assetId;
            ExpectWriteRejected(invalid, ex::ModelLoadIssueCode::InvalidAssetIdentity,
                "materials[1].assetId", "중복 material AssetId", check);
        }
        outLog += check.failed == publicationFailedBefore
            ? "  cooked identity 게시 거부: 5/5\n"
            : "  cooked identity 게시 거부: 실패\n";

        // ── 4. 읽기 거부 ────────────────────────────────────────────────
        {
            auto corrupt = baked;
            corrupt[0] = static_cast<std::byte>(0xFF);
            ExpectRejected(std::move(corrupt), "매직 손상", check);
        }
        {
            auto corrupt = baked;
            const std::uint32_t wrong = ck::kFormatVersion + 1u;
            std::memcpy(corrupt.data() + offsetof(ck::FileHeader, formatVersion),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "포맷 버전 상이", check);
        }
        {
            // ★ 이 한 건이 이 포맷을 만든 이유의 절반이다. legacy 는 이 검사가
            //   없어서 레이아웃이 바뀌면 조용히 오독했다.
            auto corrupt = baked;
            std::uint64_t wrong = ck::VertexLayoutTableHash() ^ 1ull;
            std::memcpy(corrupt.data() + offsetof(ck::FileHeader, vertexLayoutTableHash),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "정점 레이아웃 표 해시 상이", check);
        }
        {
            auto corrupt = baked;
            const std::uint32_t wrong = ex::StrideOf(ex::kAllVertexAttributes) + 4u;
            std::memcpy(corrupt.data() + offsetof(ck::FileHeader, maxVertexStride),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "최대 정점 stride 상이", check);
        }
        {
            auto corrupt = baked;
            ck::FileHeader header{};
            std::memcpy(&header, corrupt.data(), sizeof(header));
            const std::uint32_t wrong = header.vertexAttributeMaskUnion
                ^ ex::Bit(ex::VertexAttribute::Uv1);
            std::memcpy(corrupt.data()
                + offsetof(ck::FileHeader, vertexAttributeMaskUnion),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "정점 속성 union 상이", check);
        }
        {
            auto corrupt = baked;
            const std::optional<std::size_t> meshOffset = FirstMeshRecordOffset(corrupt);
            check.Check(meshOffset.has_value(), "손상 검사용 첫 mesh 레코드를 찾는다");
            if (meshOffset)
            {
                const std::uint32_t wrong = ex::StrideOf(ex::kAllVertexAttributes) - 4u;
                std::memcpy(corrupt.data() + *meshOffset
                    + offsetof(ck::CookedMesh, vertexStride), &wrong, sizeof(wrong));
                ExpectRejected(std::move(corrupt), "mesh별 정점 stride 상이", check);
            }
        }
        {
            auto corrupt = baked;
            const std::optional<std::size_t> meshOffset = FirstMeshRecordOffset(corrupt);
            check.Check(meshOffset.has_value(), "mask 손상 검사용 첫 mesh 레코드를 찾는다");
            if (meshOffset)
            {
                const std::uint32_t wrong = ex::kAllVertexAttributes
                    & ~ex::Bit(ex::VertexAttribute::Tangent);
                std::memcpy(corrupt.data() + *meshOffset
                    + offsetof(ck::CookedMesh, vertexAttributeMask),
                    &wrong, sizeof(wrong));
                ExpectRejected(std::move(corrupt), "mesh별 필수 정점 mask 누락", check);
            }
        }
        {
            auto truncated = baked;
            truncated.resize(baked.size() / 2);
            ExpectRejected(std::move(truncated), "파일이 절반으로 잘림", check);
        }
        {
            auto grown = baked;
            grown.push_back(std::byte{ 0 });
            ExpectRejected(std::move(grown), "파일 뒤에 바이트가 붙음", check);
        }
        {
            ExpectRejected(std::vector<std::byte>{}, "빈 파일", check);
        }
        {
            ExpectRejected(std::vector<std::byte>(4, std::byte{ 0 }),
                "헤더보다 짧은 파일", check);
        }

        // ── 5. 스켈레톤 없는 모델 ───────────────────────────────────────
        {
            ex::ModelDraft skinless = MakeSyntheticDraft();
            skinless.skeleton.reset();
            skinless.animator.reset();
            const ck::CookedWriteResult write = ck::Write(skinless);
            check.Check(write.Succeeded(), "스켈레톤 없는 모델 굽기");

            ex::ModelDraft restored{};
            std::vector<ex::ModelLoadIssue> issues;
            const bool ok = ck::Read(write.bytes, restored, issues);
            check.Check(ok, "스켈레톤 없는 모델을 읽는다");
            if (ok)
            {
                CompareDrafts(skinless, restored, check, "스킨없음");
                check.Check(!restored.skeleton.has_value(),
                    "스켈레톤 없음이 없음으로 복원된다");
            }
        }

        // ── 6. 최소 모델 ────────────────────────────────────────────────
        // 0개짜리를 성공으로 읽어 넘기는 것이 이 저장소의 거짓 통과 양식이었다.
        // 최소 draft 도 왕복해야 하지만, **그 자체가 통과의 근거는 아니다.**
        {
            ex::ModelDraft minimal{};
            minimal.metadata.name = "minimal";
            minimal.metadata.assetId = MakeFixtureAssetId(0x50u);
            const ck::CookedWriteResult write = ck::Write(minimal);
            check.Check(write.Succeeded(), "identity가 있는 최소 draft 굽기");
            ex::ModelDraft restored{};
            std::vector<ex::ModelLoadIssue> issues;
            check.Check(ck::Read(write.bytes, restored, issues), "최소 draft 왕복");
            check.Equal(restored.metadata.name, minimal.metadata.name, "최소 draft name");
        }

        // ── 7. D5-b1 sidecar identity + GUID manifest 계약 ──────────────
        RunIdentityAndManifestContractSelfTest(baked, check, outLog);

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 통과 %zu · 실패 %zu\n",
            check.passed + check.failed, check.passed, check.failed);
        outLog += summary;
        return 0 == check.failed;
    }

    bool RunExperimentCookedRoundTrip(const std::string& modelPath, std::string& outLog)
    {
        outLog += "[experiment.cooked] 실자산 왕복: " + modelPath + "\n";

        const std::filesystem::path source(modelPath);

        std::filesystem::path modelMetaPath = source;
        modelMetaPath += ".meta";
        std::string modelMetaText;
        if (!ReadTextFile(modelMetaPath, modelMetaText))
        {
            outLog += "  결과: 실패 (model sidecar를 읽을 수 없음)\n";
            return false;
        }
        ck::ModelCookIdentity modelIdentity;
        std::vector<ck::ModelIdentityIssue> modelIdentityIssues;
        if (!ck::ReadModelCookIdentity(modelMetaText,
            modelIdentity, modelIdentityIssues))
        {
            outLog += "  결과: 실패 (model subasset identity 불일치)\n";
            for (const ck::ModelIdentityIssue& issue : modelIdentityIssues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        const std::filesystem::path assetsRoot = source.parent_path().parent_path();
        std::string identityFailure;
        const ex::AssetId gbufferShaderId = ReadMetaAssetId(
            assetsRoot / "Shaders/DefaultPassShader/GBuffer.shadermeta.meta",
            identityFailure);
        if (!gbufferShaderId.IsValid())
        {
            outLog += "  결과: 실패 (" + identityFailure + ")\n";
            return false;
        }
        const ex::AssetId forwardShaderId = ReadMetaAssetId(
            assetsRoot / "Shaders/DefaultPassShader/Forward.shadermeta.meta",
            identityFailure);
        if (!forwardShaderId.IsValid())
        {
            outLog += "  결과: 실패 (" + identityFailure + ")\n";
            return false;
        }

        // D5-b1은 fixture ID를 쓰지 않는다. 모델/내부 재질은 model sidecar,
        // 외부 texture와 ShaderMeta는 각자의 sidecar UUIDv4를 읽는다. Blend만
        // Forward, Opaque/Mask는 GBuffer라는 PBR shader 정책도 resolver 경계에서
        // 명시한다.
        std::vector<std::string> materialSourceKeys;
        std::vector<std::string> embeddedTextureSourceKeys;
        std::vector<std::string> resolutionFailures;
        im::ImporterDecoderOptions decoderOptions{};
        decoderOptions.conversion.modelAssetId = modelIdentity.modelAssetId;
        decoderOptions.conversion.resolveMaterialAsset =
            [&](const im::ImportedMaterial& material, std::size_t)
            {
                if (std::ranges::find(materialSourceKeys, material.sourceKey)
                    == materialSourceKeys.end())
                {
                    materialSourceKeys.push_back(material.sourceKey);
                }
                return modelIdentity.FindMaterial(material.sourceKey);
            };
        decoderOptions.conversion.resolveShaderAsset =
            [&](const im::ImportedMaterial& material, std::size_t)
            {
                return material.alphaMode == im::AlphaMode::Blend
                    ? forwardShaderId : gbufferShaderId;
            };
        decoderOptions.conversion.resolveTextureAsset =
            [&](const im::ImportedTexture& texture)
            {
                if (texture.IsEmbedded())
                {
                    if (std::ranges::find(embeddedTextureSourceKeys,
                        texture.sourceKey) == embeddedTextureSourceKeys.end())
                    {
                        embeddedTextureSourceKeys.push_back(texture.sourceKey);
                    }
                    return modelIdentity.FindEmbeddedTexture(texture.sourceKey);
                }

                std::filesystem::path textureMetaPath = texture.sourcePath;
                textureMetaPath += ".meta";
                std::string failure;
                const ex::AssetId id = ReadMetaAssetId(textureMetaPath, failure);
                if (!id.IsValid()) resolutionFailures.push_back(std::move(failure));
                return id;
            };
        im::ImporterModelDecoder decoder(std::move(decoderOptions));

        ex::ModelLoadRequest request{};
        request.sourcePath = source;
        request.sourcePreference = ex::ModelSourcePreference::SourceOnly;

        ex::ModelDecodeResult decoded = decoder.Decode(request);
        if (!decoded.draft.has_value())
        {
            outLog += "  결과: 실패 (임포트 불가)\n";
            for (const auto& issue : decoded.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }

        Checker check{ outLog };
        const ex::ModelDraft& original = *decoded.draft;
        check.Equal(original.metadata.assetId, modelIdentity.modelAssetId,
            "실자산 model AssetId가 sidecar UUIDv4와 같다");
        check.Check(resolutionFailures.empty(),
            "외부 texture sidecar identity가 모두 해석된다");
        check.Equal(materialSourceKeys.size(), modelIdentity.materials.size(),
            "import material source key와 sidecar identity 수가 같다");
        check.Equal(embeddedTextureSourceKeys.size(),
            modelIdentity.embeddedTextures.size(),
            "import embedded texture source key와 sidecar identity 수가 같다");
        for (const ck::ModelSubAssetIdentity& identity : modelIdentity.materials)
        {
            check.Check(std::ranges::find(materialSourceKeys, identity.sourceKey)
                != materialSourceKeys.end(),
                "stale model material subasset identity가 없다");
        }
        for (const ck::ModelSubAssetIdentity& identity : modelIdentity.embeddedTextures)
        {
            check.Check(std::ranges::find(embeddedTextureSourceKeys, identity.sourceKey)
                != embeddedTextureSourceKeys.end(),
                "stale embedded texture subasset identity가 없다");
        }
        std::size_t materialIdentities = 0;
        std::size_t shaderIdentities = 0;
        std::size_t textureReferences = 0;
        std::size_t textureIdentities = 0;
        for (std::size_t materialIndex = 0;
            materialIndex < original.materials.size(); ++materialIndex)
        {
            const ex::Material& material = original.materials[materialIndex];
            if (material.assetId.IsValid()) ++materialIdentities;
            if (material.shaderAssetId.IsValid()) ++shaderIdentities;
            check.Check(material.assetId.IsValid(),
                "실자산 material[" + std::to_string(materialIndex) + "] AssetId가 채워진다");
            check.Check(material.shaderAssetId.IsValid(),
                "실자산 material[" + std::to_string(materialIndex) + "] ShaderMeta ID가 채워진다");
            for (std::size_t propertyIndex = 0;
                propertyIndex < material.properties.size(); ++propertyIndex)
            {
                if (const auto* texture = std::get_if<ex::TextureReference>(
                    &material.properties[propertyIndex].value))
                {
                    ++textureReferences;
                    if (texture->assetId.IsValid()) ++textureIdentities;
                    check.Check(texture->assetId.IsValid(),
                        "실자산 texture reference AssetId가 채워진다");
                }
            }
        }
        char identityScale[240];
        std::snprintf(identityScale, sizeof(identityScale),
            "  cooked identity: model=%s materials=%zu/%zu shaders=%zu/%zu textures=%zu/%zu\n",
            original.metadata.assetId.IsValid() ? "yes" : "no",
            materialIdentities, original.materials.size(),
            shaderIdentities, original.materials.size(),
            textureIdentities, textureReferences);
        outLog += identityScale;
        char sidecarScale[200];
        std::snprintf(sidecarScale, sizeof(sidecarScale),
            "  sidecar identity: model=yes materials=%zu/%zu embedded=%zu/%zu\n",
            materialSourceKeys.size(), modelIdentity.materials.size(),
            embeddedTextureSourceKeys.size(), modelIdentity.embeddedTextures.size());
        outLog += sidecarScale;

        const ck::CookedWriteResult write = ck::Write(original);
        check.Check(write.Succeeded(), "실자산 checked cook가 성공한다");
        if (!write.Succeeded())
        {
            for (const ex::ModelLoadIssue& issue : write.issues)
                outLog += "    " + issue.context + ": " + issue.message + "\n";
            return false;
        }
        const std::vector<std::byte>& baked = write.bytes;

        ck::Sha256Digest cookedDigest{};
        std::string hashError;
        check.Check(ck::ComputeSha256(baked, cookedDigest, hashError),
            "실자산 cooked artifact SHA-256을 계산한다");
        const std::string derivedPath =
            ck::MakeDerivedModelArtifactPath(original.metadata.assetId);
        check.Check(!derivedPath.empty(),
            "실자산 model GUID가 Derived .cemc path를 만든다");

        ck::CookedAssetManifest manifest;
        ck::CookedAssetManifestEntry modelEntry;
        modelEntry.assetId = original.metadata.assetId;
        modelEntry.kind = ck::CookedAssetKind::Model;
        modelEntry.formatVersion = ck::kFormatVersion;
        modelEntry.byteSize = baked.size();
        modelEntry.contentSha256 = cookedDigest;
        modelEntry.artifactPath = derivedPath;
        for (const ex::Material& material : original.materials)
            modelEntry.dependencies.push_back(material.assetId);
        manifest.entries.push_back(std::move(modelEntry));

        for (const ex::Material& material : original.materials)
        {
            ck::CookedAssetManifestEntry materialEntry;
            materialEntry.assetId = material.assetId;
            materialEntry.kind = ck::CookedAssetKind::Material;
            materialEntry.formatVersion = ck::kFormatVersion;
            materialEntry.byteSize = baked.size();
            materialEntry.contentSha256 = cookedDigest;
            // material은 이 단계에서 model .cemc container 안의 subasset이다.
            materialEntry.artifactPath = derivedPath;
            manifest.entries.push_back(std::move(materialEntry));
        }

        const ck::AssetManifestWriteResult manifestWrite =
            ck::WriteAssetManifest(manifest);
        check.Check(manifestWrite.Succeeded(),
            "실자산 model/material GUID manifest를 쓴다");
        ck::CookedAssetManifest restoredManifest;
        std::vector<ck::AssetManifestIssue> manifestIssues;
        const bool manifestRead = ck::ReadAssetManifest(manifestWrite.bytes,
            restoredManifest, manifestIssues);
        check.Check(manifestRead, "실자산 GUID manifest를 다시 읽는다");
        const ck::CookedAssetManifestEntry* resolvedModel =
            restoredManifest.Find(original.metadata.assetId);
        check.Check(resolvedModel && resolvedModel->artifactPath == derivedPath,
            "실제 model GUID lookup이 canonical Derived path를 찾는다");
        std::size_t resolvedMaterials = 0u;
        for (const ex::Material& material : original.materials)
        {
            const ck::CookedAssetManifestEntry* resolved =
                restoredManifest.Find(material.assetId);
            if (resolved && resolved->artifactPath == derivedPath
                && resolved->kind == ck::CookedAssetKind::Material)
            {
                ++resolvedMaterials;
            }
        }
        check.Equal(resolvedMaterials, original.materials.size(),
            "실제 material subasset GUID가 model container로 해석된다");
        manifestIssues.clear();
        check.Check(resolvedModel && ck::VerifyArtifact(*resolvedModel,
            baked.size(), cookedDigest, manifestIssues),
            "실자산 manifest SHA-256/크기가 cooked bytes와 같다");
        char manifestScale[220];
        std::snprintf(manifestScale, sizeof(manifestScale),
            "  manifest identity: entries=%zu model=%s materials=%zu/%zu sha256=%s\n",
            restoredManifest.entries.size(), resolvedModel ? "yes" : "no",
            resolvedMaterials, original.materials.size(),
            resolvedModel ? "yes" : "no");
        outLog += manifestScale;

        ex::ModelDraft restored{};
        std::vector<ex::ModelLoadIssue> issues;
        const bool ok = ck::Read(baked, restored, issues);
        check.Check(ok, "실자산 페이로드를 읽는다");
        if (ok) CompareDrafts(original, restored, check, "실자산");
        else for (const auto& issue : issues) outLog += "    사유: " + issue.message + "\n";

        // ★ 규모를 찍는다. 0건을 성공으로 읽는 것을 막는 유일한 방법은
        //   무엇을 몇 개 비교했는지 눈에 보이게 하는 것이다.
        std::size_t vertexCount = 0, keyCount = 0, channelCount = 0;
        for (const ex::Mesh& mesh : original.meshes) vertexCount += mesh.vertices.size();
        if (original.skeleton.has_value())
        {
            for (const ex::AnimationClip& clip : original.skeleton->clips)
            {
                channelCount += clip.channels.size();
                for (const ex::AnimationChannel& channel : clip.channels)
                {
                    keyCount += channel.translations.size()
                        + channel.rotations.size() + channel.scales.size();
                }
            }
        }
        char scale[256];
        std::snprintf(scale, sizeof(scale),
            "  비교 규모: 정점 %zu · 인덱스는 메시별 · 채널 %zu · 키 %zu · 재질 %zu · 뼈 %zu\n"
            "  구운 크기: %zu B\n",
            vertexCount, channelCount, keyCount, original.materials.size(),
            original.skeleton.has_value() ? original.skeleton->bones.size() : 0u,
            baked.size());
        outLog += scale;

        check.Check(vertexCount > 0, "비교한 정점이 0개가 아니다");

        char summary[160];
        std::snprintf(summary, sizeof(summary),
            "  단정 %zu건 중 통과 %zu · 실패 %zu\n",
            check.passed + check.failed, check.passed, check.failed);
        outLog += summary;
        return 0 == check.failed;
    }
}
