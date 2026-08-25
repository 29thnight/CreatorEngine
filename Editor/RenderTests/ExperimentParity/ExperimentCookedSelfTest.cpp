#include "ExperimentParity/ExperimentCookedSelfTest.h"

#include "Experiment/Cooked/CookedModelCodec.h"
#include "Experiment/Import/ImporterModelDecoder.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
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

                bool sameVertices = right.vertices.size() == left.vertices.size();
                for (std::size_t v = 0; sameVertices && v < left.vertices.size(); ++v)
                    sameVertices = Same(left.vertices[v], right.vertices[v]);
                check.Check(sameVertices, at + ".정점 값(비트 단위)");
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
            root.meshes = { ex::MeshIndex(0), ex::MeshIndex(1) };
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

            // 메시 2개 — 하나는 정점이 없다.
            ex::Mesh mesh{};
            mesh.name = "mesh0";
            mesh.material = ex::MaterialIndex(0);
            for (std::uint32_t i = 0; i < 3; ++i)
            {
                ex::Vertex vertex{};
                vertex.position = { static_cast<float>(i), 1.0f, -2.5f };
                vertex.normal = { 0.0f, 1.0f, 0.0f };
                vertex.uv0 = { 0.25f, 0.75f };
                vertex.uv1 = { 0.5f, 0.5f };
                vertex.tangent = { 1.0f, 0.0f, 0.0f };
                vertex.bitangent = { 0.0f, 0.0f, 1.0f };
                vertex.skin[0] = { ex::BoneIndex(0), 1.0f };
                mesh.vertices.push_back(vertex);
            }
            mesh.indices = { 0u, 1u, 2u };
            mesh.bounds = math::aabb::from_min_max(
                math::vector3{ 0.0f, 1.0f, -2.5f },
                math::vector3{ 2.0f, 1.0f, -2.5f });
            draft.meshes.push_back(mesh);

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
    }

    bool RunExperimentCookedSelfTest(std::string& outLog)
    {
        outLog += "[experiment.cooked] 합성 검사\n";
        Checker check{ outLog };

        // ── 1. 왕복 무손실 ──────────────────────────────────────────────
        const ex::ModelDraft original = MakeSyntheticDraft();
        const std::vector<std::byte> baked = ck::Write(original);
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
            const std::vector<std::byte> again = ck::Write(original);
            check.Check(again == baked, "같은 draft 는 같은 바이트로 구워진다");
        }

        // ── 3. 거부 ─────────────────────────────────────────────────────
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
            std::uint64_t wrong = ck::VertexLayoutHash() ^ 1ull;
            std::memcpy(corrupt.data() + offsetof(ck::FileHeader, vertexLayoutHash),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "정점 레이아웃 해시 상이", check);
        }
        {
            auto corrupt = baked;
            const std::uint32_t wrong = static_cast<std::uint32_t>(sizeof(ex::Vertex)) + 4u;
            std::memcpy(corrupt.data() + offsetof(ck::FileHeader, vertexStride),
                &wrong, sizeof(wrong));
            ExpectRejected(std::move(corrupt), "정점 stride 상이", check);
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

        // ── 4. 스켈레톤 없는 모델 ───────────────────────────────────────
        {
            ex::ModelDraft skinless = MakeSyntheticDraft();
            skinless.skeleton.reset();
            skinless.animator.reset();
            const std::vector<std::byte> bytes = ck::Write(skinless);

            ex::ModelDraft restored{};
            std::vector<ex::ModelLoadIssue> issues;
            const bool ok = ck::Read(bytes, restored, issues);
            check.Check(ok, "스켈레톤 없는 모델을 읽는다");
            if (ok)
            {
                CompareDrafts(skinless, restored, check, "스킨없음");
                check.Check(!restored.skeleton.has_value(),
                    "스켈레톤 없음이 없음으로 복원된다");
            }
        }

        // ── 5. 최소 모델 ────────────────────────────────────────────────
        // 0개짜리를 성공으로 읽어 넘기는 것이 이 저장소의 거짓 통과 양식이었다.
        // 최소 draft 도 왕복해야 하지만, **그 자체가 통과의 근거는 아니다.**
        {
            ex::ModelDraft minimal{};
            minimal.metadata.name = "minimal";
            const std::vector<std::byte> bytes = ck::Write(minimal);
            ex::ModelDraft restored{};
            std::vector<ex::ModelLoadIssue> issues;
            check.Check(ck::Read(bytes, restored, issues), "최소 draft 왕복");
            check.Equal(restored.metadata.name, minimal.metadata.name, "최소 draft name");
        }

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

        // 기본 생성이 glTF·FBX 를 모두 등록한다 — 확장자 분기를 여기서 다시
        // 쓰면 그 규칙이 두 곳이 되고 반드시 어긋난다.
        im::ImporterModelDecoder decoder{};

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
        const std::vector<std::byte> baked = ck::Write(original);

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
