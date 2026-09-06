#include "ModelStableKeys.h"
#include "AssetIdentityHex.h"

#include "Sha256.h"

#include <Windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <set>
#include <utility>

#pragma comment(lib, "bcrypt.lib")

namespace assets
{
    namespace im = experiment::importer;

    namespace
    {
        void AddIssue(std::vector<StableKeyIssue>& issues, StableKeyIssueCode code,
            std::string context, std::string message)
        {
            issues.push_back({ code, std::move(context), std::move(message) });
        }

        [[nodiscard]] std::string KindContext(SubAssetKind kind, std::size_t index)
        {
            return std::string(ToKindName(kind)) + "[" + std::to_string(index) + "]";
        }

        // ── 지문 ────────────────────────────────────────────────────────────
        // 지문은 신원이 아니다. authoring key 재결합과 stale 판정에만 쓴다. 그래서
        // 표현을 바꾸면 재결합만 실패하고(→ 명시적 오류) 신원은 조용히 바뀌지 않는다.
        class FingerprintBuilder final
        {
        public:
            void Tag(std::string_view tag)
            {
                Bytes(tag.data(), tag.size());
                const std::uint8_t sep = 0x1Fu;
                Bytes(&sep, 1u);
            }
            void Text(std::string_view text)
            {
                U32(static_cast<std::uint32_t>(text.size()));
                Bytes(text.data(), text.size());
            }
            void U32(std::uint32_t v)
            {
                const std::uint8_t b[4] = {
                    static_cast<std::uint8_t>(v >> 24), static_cast<std::uint8_t>(v >> 16),
                    static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v) };
                Bytes(b, 4u);
            }
            void F32(float v)
            {
                std::uint32_t bits{};
                std::memcpy(&bits, &v, sizeof(bits));
                U32(bits);
            }
            void F64(double v)
            {
                std::uint64_t bits{};
                std::memcpy(&bits, &v, sizeof(bits));
                U32(static_cast<std::uint32_t>(bits >> 32));
                U32(static_cast<std::uint32_t>(bits));
            }
            void Bytes(const void* data, std::size_t size) { m_sha.Update(data, size); }
            template <typename T>
            void Pod(const std::vector<T>& values)
            {
                U32(static_cast<std::uint32_t>(values.size()));
                if (!values.empty()) Bytes(values.data(), values.size() * sizeof(T));
            }
            [[nodiscard]] std::string Finish()
            {
                return std::string(kFingerprintPrefix) + Hash::ToHex(m_sha.Finish());
            }

        private:
            Hash::Sha256 m_sha;
        };

        void Slot(FingerprintBuilder& fp, const im::TextureSlot& slot,
            const im::ImportedScene& scene, const std::vector<std::string>& textureFingerprints)
        {
            fp.Tag("slot");
            if (slot.IsValid() && slot.texture.Value() < scene.textures.size())
            {
                // 슬롯은 텍스처의 **지문**으로 잇는다(ordinal이 아니다).
                fp.Text(textureFingerprints[slot.texture.Value()]);
            }
            else
            {
                fp.Text("");
            }
            fp.U32(slot.uvSet);
            fp.F32(slot.offset.x); fp.F32(slot.offset.y);
            fp.F32(slot.tiling.x); fp.F32(slot.tiling.y);
            fp.U32(static_cast<std::uint32_t>(slot.wrapU));
            fp.U32(static_cast<std::uint32_t>(slot.wrapV));
            // Preserve existing fingerprints for identity/default rotations.
            if (slot.rotation != 0.f) { fp.Tag("uv.rotation"); fp.F32(slot.rotation); }
        }

        [[nodiscard]] std::string TextureFingerprint(const im::ImportedTexture& texture)
        {
            FingerprintBuilder fp;
            fp.Tag("texture.v1");
            fp.Text(texture.mimeType);
            fp.U32(static_cast<std::uint32_t>(texture.colorSpace));
            fp.Pod(texture.embeddedBytes);
            return fp.Finish();
        }

        [[nodiscard]] std::string MaterialFingerprint(const im::ImportedMaterial& m,
            const im::ImportedScene& scene, const std::vector<std::string>& textureFingerprints)
        {
            FingerprintBuilder fp;
            fp.Tag("material.v1");
            fp.F32(m.baseColorFactor.x); fp.F32(m.baseColorFactor.y);
            fp.F32(m.baseColorFactor.z); fp.F32(m.baseColorFactor.w);
            fp.F32(m.metallicFactor); fp.F32(m.roughnessFactor);
            fp.F32(m.emissiveFactor.x); fp.F32(m.emissiveFactor.y); fp.F32(m.emissiveFactor.z);
            fp.F32(m.emissiveStrength); fp.F32(m.normalScale); fp.F32(m.occlusionStrength);
            fp.U32(static_cast<std::uint32_t>(m.alphaMode));
            fp.F32(m.alphaCutoff);
            fp.U32(m.doubleSided ? 1u : 0u);
            Slot(fp, m.baseColor, scene, textureFingerprints);
            Slot(fp, m.metallicRoughness, scene, textureFingerprints);
            Slot(fp, m.normal, scene, textureFingerprints);
            Slot(fp, m.occlusion, scene, textureFingerprints);
            Slot(fp, m.emissive, scene, textureFingerprints);
            return fp.Finish();
        }

        [[nodiscard]] std::string MeshFingerprint(const im::ImportedMesh& mesh)
        {
            FingerprintBuilder fp;
            fp.Tag("mesh.v1");
            fp.Pod(mesh.streams.positions);
            fp.Pod(mesh.indices);
            return fp.Finish();
        }

        [[nodiscard]] std::string NodeName(const im::ImportedScene& scene,
            im::SceneNodeIndex index)
        {
            return index.IsValid() && index.Value() < scene.nodes.size()
                ? scene.nodes[index.Value()].name : std::string{};
        }

        [[nodiscard]] std::string SkinFingerprint(const im::ImportedSkin& skin,
            const im::ImportedScene& scene)
        {
            FingerprintBuilder fp;
            fp.Tag("skeleton.v1");
            fp.Text(NodeName(scene, skin.skeletonRoot));
            fp.U32(static_cast<std::uint32_t>(skin.joints.size()));
            for (const im::SceneNodeIndex joint : skin.joints) fp.Text(NodeName(scene, joint));
            fp.Pod(skin.inverseBind);
            return fp.Finish();
        }

        // 스킨이 없는 animation-only source도 ConvertToModelDraft에서 채널 target과
        // 그 조상으로 skeleton 하나를 유도한다. sidecar inventory가 import 원본의
        // skin 배열만 세면 CEMC에는 skeleton이 있는데 sidecar에는 없는 부분 게시가
        // 된다. 같은 폐포를 여기서 지문화하되, 합성 skeleton의 신원은 이름을
        // 지어내지 않고 one-time authoring key로 발급한다.
        [[nodiscard]] std::optional<std::string> AnimationOnlySkeletonFingerprint(
            const im::ImportedScene& scene)
        {
            if (!scene.skins.empty() || scene.clips.empty() || scene.nodes.empty())
                return std::nullopt;

            std::vector<std::uint8_t> member(scene.nodes.size(), 0u);
            for (const im::ImportedClip& clip : scene.clips)
            {
                for (const im::ImportedChannel& channel : clip.channels)
                {
                    if (!channel.target.IsValid()
                        || channel.target.Value() >= scene.nodes.size()) continue;
                    std::uint32_t cursor = channel.target.Value();
                    for (std::size_t steps = 0; steps <= scene.nodes.size(); ++steps)
                    {
                        if (member[cursor]) break;
                        member[cursor] = 1u;
                        const im::SceneNodeIndex parent = scene.nodes[cursor].parent;
                        if (!parent.IsValid() || parent.Value() >= scene.nodes.size()) break;
                        cursor = parent.Value();
                    }
                }
            }
            if (std::ranges::none_of(member, [](std::uint8_t value) { return value != 0u; }))
                return std::nullopt;

            // 노드 ordinal은 fingerprint에도 쓰지 않는다. root부터의 이름 경로와
            // local TRS를 node별로 해시한 뒤 정렬해 importer 배열 재배치에 안정적이다.
            std::vector<std::string> nodeFingerprints;
            for (std::size_t nodeIndex = 0; nodeIndex < scene.nodes.size(); ++nodeIndex)
            {
                if (!member[nodeIndex]) continue;
                std::vector<std::string_view> path;
                std::uint32_t cursor = static_cast<std::uint32_t>(nodeIndex);
                for (std::size_t steps = 0; steps <= scene.nodes.size(); ++steps)
                {
                    path.push_back(scene.nodes[cursor].name);
                    const im::SceneNodeIndex parent = scene.nodes[cursor].parent;
                    if (!parent.IsValid() || parent.Value() >= scene.nodes.size()
                        || !member[parent.Value()]) break;
                    cursor = parent.Value();
                }
                std::ranges::reverse(path);

                FingerprintBuilder node;
                node.Tag("animation-only-skeleton.node.v1");
                node.U32(static_cast<std::uint32_t>(path.size()));
                for (std::string_view component : path) node.Text(component);
                const im::TrsTransform& local = scene.nodes[nodeIndex].local;
                node.F32(local.translation.x); node.F32(local.translation.y);
                node.F32(local.translation.z);
                node.F32(local.rotation.x); node.F32(local.rotation.y);
                node.F32(local.rotation.z); node.F32(local.rotation.w);
                node.F32(local.scale.x); node.F32(local.scale.y); node.F32(local.scale.z);
                nodeFingerprints.push_back(node.Finish());
            }
            std::ranges::sort(nodeFingerprints);
            FingerprintBuilder aggregate;
            aggregate.Tag("animation-only-skeleton.v1");
            aggregate.U32(static_cast<std::uint32_t>(nodeFingerprints.size()));
            for (const std::string& node : nodeFingerprints) aggregate.Text(node);
            return aggregate.Finish();
        }

        [[nodiscard]] std::string ClipFingerprint(const im::ImportedClip& clip,
            const im::ImportedScene& scene)
        {
            FingerprintBuilder fp;
            fp.Tag("animation.v1");
            fp.F64(clip.durationSeconds);
            fp.U32(static_cast<std::uint32_t>(clip.channels.size()));
            for (const im::ImportedChannel& channel : clip.channels)
            {
                fp.Text(NodeName(scene, channel.target));
                // ★ 키 구조체는 통째로 넣지 않는다 — {double time; vector3}는 4바이트
                //   패딩이 있어 바이트 해시가 비결정적이 된다. 필드별로 넣는다.
                fp.U32(static_cast<std::uint32_t>(channel.translations.size()));
                for (const auto& k : channel.translations)
                {
                    fp.F64(k.time); fp.F32(k.value.x); fp.F32(k.value.y); fp.F32(k.value.z);
                }
                fp.U32(static_cast<std::uint32_t>(channel.rotations.size()));
                for (const auto& k : channel.rotations)
                {
                    fp.F64(k.time); fp.F32(k.quaternion.x); fp.F32(k.quaternion.y);
                    fp.F32(k.quaternion.z); fp.F32(k.quaternion.w);
                }
                fp.U32(static_cast<std::uint32_t>(channel.scales.size()));
                for (const auto& k : channel.scales)
                {
                    fp.F64(k.time); fp.F32(k.value.x); fp.F32(k.value.y); fp.F32(k.value.z);
                }
            }
            return fp.Finish();
        }

        // ── 규칙 엔진 보조 ──────────────────────────────────────────────────
        [[nodiscard]] bool IsUsableText(std::string_view text) noexcept
        {
            if (text.empty() || !IsWellFormedUtf8(text)) return false;
            IdentityIssue issue{};
            return IsUtf8Nfc(text, issue);
        }

        struct KindGroup final
        {
            std::vector<std::size_t> elementIndices{}; // elements[] 안의 위치
        };
    }

    std::string_view ToString(StableKeyOrigin origin) noexcept
    {
        switch (origin)
        {
        case StableKeyOrigin::Exporter:  return "exporter";
        case StableKeyOrigin::Semantic:  return "semantic";
        case StableKeyOrigin::Authoring: return "authoring";
        }
        return "unknown";
    }

    bool IsStableKeyError(StableKeyIssueCode code) noexcept
    {
        switch (code)
        {
        case StableKeyIssueCode::NameNotNfc:
        case StableKeyIssueCode::AuthoringKeyRetired:
            return false;
        default:
            return true;
        }
    }

    bool StableKeyResult::Succeeded() const noexcept
    {
        return std::ranges::none_of(issues, [](const StableKeyIssue& issue)
        {
            return IsStableKeyError(issue.code);
        });
    }

    std::size_t StableKeyResult::CountOrigin(SubAssetKind kind, StableKeyOrigin origin) const noexcept
    {
        return static_cast<std::size_t>(std::ranges::count_if(assignments,
            [kind, origin](const StableKeyAssignment& a)
            {
                return a.kind == kind && a.origin == origin;
            }));
    }

    bool IsForbiddenOrdinalKey(std::string_view text) noexcept
    {
        // `gltf/material/0`, `fbx/material/12`, `3` — 마지막 경로 조각이 숫자만이고
        // 허용 접두가 없다. 허용 접두는 TryParseStableKey가 별도로 본다.
        if (text.empty()) return false;
        const std::size_t slash = text.rfind('/');
        const std::string_view tail = slash == std::string_view::npos
            ? text : text.substr(slash + 1u);
        if (tail.empty()) return false;
        return std::ranges::all_of(tail, [](char c) { return c >= '0' && c <= '9'; });
    }

    bool TryParseStableKey(std::string_view text, StableKeyOrigin& outOrigin,
        std::string& outError) noexcept
    {
        outError.clear();
        std::string_view value;
        if (text.starts_with(kStableKeyOriginExporter))
        {
            outOrigin = StableKeyOrigin::Exporter;
            value = text.substr(kStableKeyOriginExporter.size());
        }
        else if (text.starts_with(kStableKeyOriginName))
        {
            outOrigin = StableKeyOrigin::Semantic;
            value = text.substr(kStableKeyOriginName.size());
        }
        else if (text.starts_with(kStableKeyOriginAuthoring))
        {
            outOrigin = StableKeyOrigin::Authoring;
            value = text.substr(kStableKeyOriginAuthoring.size());
        }
        else
        {
            outError = IsForbiddenOrdinalKey(text)
                ? "ordinal key는 금지다(§2.3): " + std::string(text)
                : "허용 접두(exporter:|name:|authoring:)가 없다: " + std::string(text);
            return false;
        }
        if (!IsUsableText(value))
        {
            outError = "값이 비어 있거나 NFC UTF-8이 아니다: " + std::string(text);
            return false;
        }
        if (outOrigin == StableKeyOrigin::Authoring)
        {
            std::vector<std::uint8_t> bytes;
            if (!TryParseLowerHex(value, bytes, kAuthoringKeyBytes))
            {
                outError = "authoring key는 64자 소문자 hex여야 한다: " + std::string(text);
                return false;
            }
        }
        return true;
    }

    bool CreateAuthoringKeyBytes(std::array<std::uint8_t, kAuthoringKeyBytes>& out,
        std::string& outError) noexcept
    {
        outError.clear();
        std::array<std::uint8_t, kAuthoringKeyBytes> bytes{};
        const NTSTATUS status = ::BCryptGenRandom(nullptr, bytes.data(),
            static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (status < 0)
        {
            outError = "BCryptGenRandom failed";
            return false;
        }
        out = bytes;
        return true;
    }

    std::string MakeAuthoringStableKey(std::span<const std::uint8_t, kAuthoringKeyBytes> bytes)
    {
        return std::string(kStableKeyOriginAuthoring) + ToLowerHex(bytes);
    }

    std::vector<StableKeyElement> CollectStableKeyElements(const im::ImportedScene& scene)
    {
        std::vector<StableKeyElement> out;

        std::vector<std::string> textureFingerprints(scene.textures.size());
        for (std::size_t i = 0; i < scene.textures.size(); ++i)
            textureFingerprints[i] = TextureFingerprint(scene.textures[i]);

        for (std::size_t i = 0; i < scene.materials.size(); ++i)
        {
            const im::ImportedMaterial& m = scene.materials[i];
            out.push_back({ SubAssetKind::Material, i, m.persistentId, m.name, m.sourceKey,
                MaterialFingerprint(m, scene, textureFingerprints) });
        }
        for (std::size_t i = 0; i < scene.textures.size(); ++i)
        {
            const im::ImportedTexture& t = scene.textures[i];
            if (!t.IsEmbedded()) continue; // 외부 텍스처는 자기 .meta가 신원이다
            out.push_back({ SubAssetKind::Texture, i, t.persistentId, t.name, t.sourceKey,
                textureFingerprints[i] });
        }
        for (std::size_t i = 0; i < scene.meshes.size(); ++i)
        {
            const im::ImportedMesh& mesh = scene.meshes[i];
            out.push_back({ SubAssetKind::Mesh, i, mesh.persistentId, mesh.name,
                "mesh/" + std::to_string(i), MeshFingerprint(mesh) });
        }
        for (std::size_t i = 0; i < scene.skins.size(); ++i)
        {
            const im::ImportedSkin& skin = scene.skins[i];
            std::string name = skin.name.empty() ? NodeName(scene, skin.skeletonRoot) : skin.name;
            out.push_back({ SubAssetKind::Skeleton, i, skin.persistentId, std::move(name),
                "skin/" + std::to_string(i), SkinFingerprint(skin, scene) });
        }
        if (const std::optional<std::string> fingerprint =
            AnimationOnlySkeletonFingerprint(scene))
        {
            out.push_back({ SubAssetKind::Skeleton, 0u, {}, {},
                "skeleton/animation-derived", *fingerprint });
        }
        for (std::size_t i = 0; i < scene.clips.size(); ++i)
        {
            const im::ImportedClip& clip = scene.clips[i];
            out.push_back({ SubAssetKind::Animation, i, clip.persistentId, clip.name,
                "animation/" + std::to_string(i), ClipFingerprint(clip, scene) });
        }
        return out;
    }

    StableKeyResult DeriveModelStableKeys(std::span<const StableKeyElement> elements,
        std::span<const ModelSubAssetRecord> prior, AuthoringKeyFactory authoringKeyFactory)
    {
        StableKeyResult result;
        result.assignments.resize(elements.size());
        std::vector<bool> assigned(elements.size(), false);

        // kind별 묶음 — 유일성은 kind 안에서만 본다(§2.3 "동일 kind 안에서").
        std::map<SubAssetKind, KindGroup> groups;
        for (std::size_t i = 0; i < elements.size(); ++i)
            groups[elements[i].kind].elementIndices.push_back(i);

        for (auto& [kind, group] : groups)
        {
            // ── 1·2. exporter persistent ID ─────────────────────────────────
            std::map<std::string, std::size_t> exporterIds;
            for (const std::size_t ei : group.elementIndices)
            {
                const StableKeyElement& e = elements[ei];
                if (e.persistentId.empty()) continue;
                if (!IsUsableText(e.persistentId))
                {
                    AddIssue(result.issues, StableKeyIssueCode::InvalidPersistentId,
                        KindContext(kind, e.index), "exporter id가 NFC UTF-8이 아니다.");
                    continue;
                }
                if (const auto dup = exporterIds.find(e.persistentId); dup != exporterIds.end())
                {
                    AddIssue(result.issues, StableKeyIssueCode::DuplicatePersistentId,
                        KindContext(kind, e.index), "exporter id가 같은 kind 안에서 중복됐다: "
                        + e.persistentId + " (vs " + KindContext(kind, elements[dup->second].index) + ")");
                    continue;
                }
                exporterIds.emplace(e.persistentId, ei);
                result.assignments[ei] = { kind, e.index, StableKeyOrigin::Exporter,
                    std::string(kStableKeyOriginExporter) + e.persistentId,
                    e.binding, e.name, e.fingerprint, false };
                assigned[ei] = true;
            }

            // ── 3. semantic(이름) — kind 안에서 유일한 NFC 이름만 ─────────────
            std::map<std::string, std::vector<std::size_t>> byName;
            for (const std::size_t ei : group.elementIndices)
            {
                if (assigned[ei]) continue;
                const StableKeyElement& e = elements[ei];
                if (e.name.empty()) continue;
                if (!IsUsableText(e.name))
                {
                    AddIssue(result.issues, StableKeyIssueCode::NameNotNfc,
                        KindContext(kind, e.index), "이름이 NFC UTF-8이 아니라 authoring key로 간다.");
                    continue;
                }
                byName[e.name].push_back(ei);
            }
            for (const auto& [name, indices] : byName)
            {
                if (indices.size() != 1u) continue; // 중복 이름은 authoring으로 흘러간다
                const std::size_t ei = indices.front();
                const StableKeyElement& e = elements[ei];
                result.assignments[ei] = { kind, e.index, StableKeyOrigin::Semantic,
                    std::string(kStableKeyOriginName) + name, e.binding, e.name, e.fingerprint, false };
                assigned[ei] = true;
            }

            // ── 4. authoring — 지문으로 prior에서 되찾고, 없으면 새로 발급 ──────
            //
            // 같은 지문(= 바이트 동일 콘텐츠)이 여럿이면 서로 바꿔 끼워도 관측할 수
            // 없다 — 어느 쪽 key를 어느 쪽에 붙여도 "그 key가 가리키는 내용"은 같다.
            // 그래서 지문 그룹 안에서는 binding 순서로 짝지어 되찾는다(scene.glb의
            // 무명 메시 103개 중 동일 지오메트리가 여럿이라 이 규칙이 없으면 변경
            // 없는 재임포트가 거부됐다). 모호한 것은 하나뿐이다: **어떤 prior key도
            // 못 찾은 지문**과 **어떤 prior에도 없는 새 지문**이 동시에 있을 때 — 그때는
            // "내용이 바뀐 것"인지 "하나 지우고 하나 더한 것"인지 증명할 수 없다.
            std::vector<std::size_t> unbound;
            for (const std::size_t ei : group.elementIndices)
                if (!assigned[ei]) unbound.push_back(ei);

            std::map<std::string, std::vector<const ModelSubAssetRecord*>> priorByFingerprint;
            for (const ModelSubAssetRecord& record : prior)
            {
                if (record.kind != kind || !record.stableKey.starts_with(kStableKeyOriginAuthoring))
                    continue;
                priorByFingerprint[record.fingerprint].push_back(&record);
            }
            for (auto& entry : priorByFingerprint)
            {
                std::ranges::sort(entry.second, {}, &ModelSubAssetRecord::binding);
            }
            std::map<std::string, std::vector<std::size_t>> unboundByFingerprint;
            for (const std::size_t ei : unbound)
                unboundByFingerprint[elements[ei].fingerprint].push_back(ei);
            for (auto& entry : unboundByFingerprint)
            {
                std::ranges::sort(entry.second, {}, [&](std::size_t ei) { return elements[ei].binding; });
            }

            std::vector<std::size_t> needNewKey;
            std::size_t newFingerprintElements = 0;
            for (const auto& entry : unboundByFingerprint)
            {
                const auto found = priorByFingerprint.find(entry.first);
                const std::size_t priorCount = found == priorByFingerprint.end() ? 0u : found->second.size();
                for (std::size_t k = 0; k < entry.second.size(); ++k)
                {
                    const std::size_t ei = entry.second[k];
                    const StableKeyElement& e = elements[ei];
                    if (k < priorCount)
                    {
                        result.assignments[ei] = { kind, e.index, StableKeyOrigin::Authoring,
                            found->second[k]->stableKey, e.binding, e.name, e.fingerprint, true };
                        assigned[ei] = true;
                    }
                    else
                    {
                        needNewKey.push_back(ei);
                        if (priorCount == 0u) ++newFingerprintElements;
                    }
                }
            }

            std::size_t orphanPriorFingerprints = 0;
            std::vector<const ModelSubAssetRecord*> retiredRecords;
            for (const auto& entry : priorByFingerprint)
            {
                const auto found = unboundByFingerprint.find(entry.first);
                const std::size_t currentCount = found == unboundByFingerprint.end() ? 0u : found->second.size();
                if (currentCount == 0u) ++orphanPriorFingerprints;
                for (std::size_t k = currentCount; k < entry.second.size(); ++k)
                    retiredRecords.push_back(entry.second[k]);
            }

            // 은퇴 대상이 하나라도 있고 지문이 새로운 요소가 있으면 모호하다 — 지문 그룹이
            // 2→1로 줄면서 새 지문이 나온 경우도 "하나의 내용 변경"과 "삭제+추가"를 가를 수 없다.
            (void)orphanPriorFingerprints;
            if (!retiredRecords.empty() && newFingerprintElements > 0u)
            {
                for (const ModelSubAssetRecord* record : retiredRecords)
                {
                    AddIssue(result.issues, StableKeyIssueCode::AuthoringRebindAmbiguous,
                        std::string(ToKindName(kind)) + "/" + record->stableKey,
                        "이전 authoring key의 지문과 맞는 요소가 없고 지문이 새로운 무명 요소 "
                        + std::to_string(newFingerprintElements)
                        + "개가 있다 — 내용 변경인지 삭제+추가인지 증명할 수 없다.");
                }
            }
            else
            {
                for (const ModelSubAssetRecord* record : retiredRecords)
                {
                    AddIssue(result.issues, StableKeyIssueCode::AuthoringKeyRetired,
                        std::string(ToKindName(kind)) + "/" + record->stableKey,
                        "이전 authoring key에 맞는 요소가 원본에서 사라졌다 — key를 은퇴시킨다.");
                }
            }

            for (const std::size_t ei : needNewKey)
            {
                const StableKeyElement& e = elements[ei];
                std::array<std::uint8_t, kAuthoringKeyBytes> bytes{};
                std::string error;
                if (!authoringKeyFactory || !authoringKeyFactory(bytes, error))
                {
                    AddIssue(result.issues, StableKeyIssueCode::SeedFailure,
                        KindContext(kind, e.index), "authoring key 발급 실패: " + error);
                    continue;
                }
                result.assignments[ei] = { kind, e.index, StableKeyOrigin::Authoring,
                    MakeAuthoringStableKey(bytes), e.binding, e.name, e.fingerprint, false };
                assigned[ei] = true;
            }
        }

        // ── 최종 유일성(kind 안) — 구조상 불가능해야 하지만 검산한다 ────────────
        std::set<std::pair<SubAssetKind, std::string>> seen;
        for (std::size_t i = 0; i < elements.size(); ++i)
        {
            if (!assigned[i]) continue;
            const StableKeyAssignment& a = result.assignments[i];
            if (!seen.emplace(a.kind, a.stableKey).second)
            {
                AddIssue(result.issues, StableKeyIssueCode::DuplicateStableKey,
                    KindContext(a.kind, a.index), "stable key가 kind 안에서 중복됐다: " + a.stableKey);
            }
        }

        // 배정 실패 요소는 결과에서 뺀다(issues에 사유가 있다).
        std::vector<StableKeyAssignment> compact;
        compact.reserve(elements.size());
        for (std::size_t i = 0; i < elements.size(); ++i)
            if (assigned[i]) compact.push_back(std::move(result.assignments[i]));
        result.assignments = std::move(compact);
        return result;
    }
}
