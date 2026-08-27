#include "CookedModelCodec.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <utility>

namespace experiment::cooked
{
    namespace
    {
        // ── 쓰기 쪽 도구 ────────────────────────────────────────────────

        // 이름을 모아 두는 테이블. 같은 문자열은 한 번만 담는다 — 뼈/노드
        // 이름은 실제로 잘 겹친다(mixamorig 계열).
        class StringTable final
        {
        public:
            [[nodiscard]] StringRef Add(std::string_view text)
            {
                if (text.empty()) return StringRef{ 0u, 0u };

                const auto found = lookup_.find(std::string(text));
                if (found != lookup_.end()) return found->second;

                const StringRef ref{
                    static_cast<std::uint32_t>(bytes_.size()),
                    static_cast<std::uint32_t>(text.size()) };
                bytes_.insert(bytes_.end(), text.begin(), text.end());
                lookup_.emplace(std::string(text), ref);
                return ref;
            }

            [[nodiscard]] const std::vector<char>& Bytes() const noexcept { return bytes_; }

        private:
            std::vector<char> bytes_{};
            std::unordered_map<std::string, StringRef> lookup_{};
        };

        // std::filesystem::path 는 u8 로 왕복한다. `.string()` 은 Windows 에서
        // 현재 로캘로 좁히기 때문에 한글 경로에서 손실되거나 던진다.
        [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path)
        {
            const std::u8string value = path.u8string();
            return std::string(reinterpret_cast<const char*>(value.data()), value.size());
        }

        [[nodiscard]] std::filesystem::path Utf8ToPath(std::string_view text)
        {
            if (text.empty()) return {};
            const std::u8string value(
                reinterpret_cast<const char8_t*>(text.data()), text.size());
            return std::filesystem::path(value);
        }

        // 섹션 하나를 만드는 동안 바이트를 모으는 곳.
        class ByteWriter final
        {
        public:
            void Raw(const void* data, std::size_t size)
            {
                if (0 == size) return;
                const auto* first = static_cast<const std::byte*>(data);
                bytes_.insert(bytes_.end(), first, first + size);
            }

            template <typename T>
            void Pod(const T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Raw(&value, sizeof(T));
            }

            template <typename T>
            void PodArray(const std::vector<T>& values)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                Raw(values.data(), values.size() * sizeof(T));
            }

            [[nodiscard]] const std::vector<std::byte>& Bytes() const noexcept { return bytes_; }
            [[nodiscard]] std::size_t Size() const noexcept { return bytes_.size(); }

        private:
            std::vector<std::byte> bytes_{};
        };

        [[nodiscard]] std::uint64_t AlignUp(std::uint64_t value) noexcept
        {
            const std::uint64_t mask = kSectionAlignment - 1u;
            return (value + mask) & ~mask;
        }

        // ── 읽기 쪽 도구 ────────────────────────────────────────────────

        // 가변 길이 섹션(재질)을 훑는 커서. **모든 읽기가 경계를 검사한다** —
        // 캐시는 잘리거나 손상될 수 있고, 그때 경계를 안 보면 조용히 남의
        // 메모리를 읽는다. 실패는 플래그로 남기고 뒤 읽기는 전부 무해해진다.
        class ByteCursor final
        {
        public:
            explicit ByteCursor(std::span<const std::byte> bytes) noexcept
                : bytes_(bytes) {}

            [[nodiscard]] bool Ok() const noexcept { return ok_; }
            [[nodiscard]] bool AtEnd() const noexcept { return offset_ >= bytes_.size(); }

            template <typename T>
            [[nodiscard]] T Pod() noexcept
            {
                static_assert(std::is_trivially_copyable_v<T>);
                T value{};
                if (!ok_ || offset_ + sizeof(T) > bytes_.size()) { ok_ = false; return value; }
                std::memcpy(&value, bytes_.data() + offset_, sizeof(T));
                offset_ += sizeof(T);
                return value;
            }

        private:
            std::span<const std::byte> bytes_{};
            std::size_t offset_{};
            bool ok_{ true };
        };

        void Reject(std::vector<ModelLoadIssue>& issues,
            std::string context, std::string message)
        {
            issues.push_back(ModelLoadIssue{
                // ★ Warning 이다. 캐시 거부는 정상 폴백이지 실패가 아니다.
                //   Error 로 내면 진짜 실패를 찾을 때 잡음이 된다.
                ModelLoadIssueSeverity::Warning,
                ModelLoadIssueCode::CookedPayloadRejected,
                std::move(context), std::move(message) });
        }

        // 섹션 하나를 원소 배열로 본다. bytes 가 elementCount * sizeof(T) 와
        // 정확히 맞지 않으면 거부한다 — 딱 맞지 않는데 읽으면 끝이 잘린다.
        template <typename T>
        [[nodiscard]] bool ViewArray(std::span<const std::byte> file,
            const SectionEntry& entry, std::span<const T>& out,
            std::vector<ModelLoadIssue>& issues, const char* what)
        {
            if (entry.offset > file.size() || entry.bytes > file.size() - entry.offset)
            {
                Reject(issues, what, "섹션이 파일 밖을 가리킨다 — 잘렸거나 손상됐다.");
                return false;
            }
            if (entry.bytes != static_cast<std::uint64_t>(entry.elementCount) * sizeof(T))
            {
                Reject(issues, what, "섹션 크기가 원소 수와 맞지 않는다.");
                return false;
            }
            out = std::span<const T>(
                reinterpret_cast<const T*>(file.data() + entry.offset),
                entry.elementCount);
            return true;
        }

        // [begin, count) 가 블록 안인가. 범위 참조는 전부 이걸 통과해야 한다 —
        // 굽는 쪽이 맞게 써도 파일이 손상되면 여기로 들어온다.
        [[nodiscard]] bool InRange(std::uint32_t begin, std::uint32_t count,
            std::size_t size) noexcept
        {
            return static_cast<std::uint64_t>(begin) + count <= size;
        }
    }

    // ════════════════════════════════════════════════════════════════════
    // 쓰기
    // ════════════════════════════════════════════════════════════════════

    std::vector<std::byte> Write(const ModelDraft& draft)
    {
        StringTable strings;

        // ── 노드 ────────────────────────────────────────────────────────
        std::vector<CookedNode> nodes;
        std::vector<std::uint32_t> nodeMeshes;
        nodes.reserve(draft.nodes.size());
        for (const ModelNode& node : draft.nodes)
        {
            CookedNode cooked{};
            cooked.name = strings.Add(node.name);
            cooked.parent = node.parent.Value();
            cooked.meshBegin = static_cast<std::uint32_t>(nodeMeshes.size());
            cooked.meshCount = static_cast<std::uint32_t>(node.meshes.size());
            cooked.localTransform = node.localTransform;
            for (const MeshIndex mesh : node.meshes) nodeMeshes.push_back(mesh.Value());
            nodes.push_back(cooked);
        }

        // ── 메시 ────────────────────────────────────────────────────────
        std::vector<CookedMesh> meshes;
        std::vector<std::byte> vertexBytes;
        std::vector<std::uint32_t> indices;
        VertexAttributeMask vertexAttributeMaskUnion = 0;
        std::uint32_t maxVertexStride = 0;
        meshes.reserve(draft.meshes.size());
        {
            std::size_t vertexByteTotal = 0, indexTotal = 0;
            for (const Mesh& mesh : draft.meshes)
            {
                vertexByteTotal += mesh.vertices.ByteSize();
                indexTotal += mesh.indices.size();
            }
            vertexBytes.reserve(vertexByteTotal);
            indices.reserve(indexTotal);
        }
        for (const Mesh& mesh : draft.meshes)
        {
            CookedMesh cooked{};
            cooked.name = strings.Add(mesh.name);
            cooked.material = mesh.material.Value();
            cooked.vertexByteBegin = static_cast<std::uint32_t>(vertexBytes.size());
            cooked.vertexCount = static_cast<std::uint32_t>(mesh.vertices.size());
            cooked.vertexStride = mesh.vertices.Stride();
            cooked.vertexAttributeMask = mesh.vertices.AttributeMask();
            cooked.indexBegin = static_cast<std::uint32_t>(indices.size());
            cooked.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
            cooked.bounds = mesh.bounds;
            const std::span<const std::byte> packed = mesh.vertices.Bytes();
            vertexBytes.insert(vertexBytes.end(), packed.begin(), packed.end());
            indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
            vertexAttributeMaskUnion |= cooked.vertexAttributeMask;
            maxVertexStride = (std::max)(maxVertexStride, cooked.vertexStride);
            meshes.push_back(cooked);
        }

        // ── 스켈레톤 · 클립 · 키 ────────────────────────────────────────
        std::vector<CookedBone> bones;
        std::vector<CookedClip> clips;
        std::vector<CookedChannel> channels;
        std::vector<TranslationKey> translations;
        std::vector<RotationKey> rotations;
        std::vector<ScaleKey> scales;
        CookedSkeletonHeader skeletonHeader{};

        if (draft.skeleton.has_value())
        {
            const Skeleton& skeleton = *draft.skeleton;
            skeletonHeader.present = 1u;
            skeletonHeader.rootBone = skeleton.rootBone.Value();
            skeletonHeader.rootTransform = skeleton.rootTransform;
            skeletonHeader.globalInverseTransform = skeleton.globalInverseTransform;

            bones.reserve(skeleton.bones.size());
            for (const Bone& bone : skeleton.bones)
            {
                CookedBone cooked{};
                cooked.name = strings.Add(bone.name);
                cooked.parent = bone.parent.Value();
                cooked.inverseBindMatrix = bone.inverseBindMatrix;
                bones.push_back(cooked);
            }

            clips.reserve(skeleton.clips.size());
            for (const AnimationClip& clip : skeleton.clips)
            {
                CookedClip cookedClip{};
                cookedClip.name = strings.Add(clip.name);
                cookedClip.durationTicks = clip.durationTicks;
                cookedClip.ticksPerSecond = clip.ticksPerSecond;
                cookedClip.looping = clip.looping ? 1u : 0u;
                cookedClip.channelBegin = static_cast<std::uint32_t>(channels.size());
                cookedClip.channelCount = static_cast<std::uint32_t>(clip.channels.size());

                for (const AnimationChannel& channel : clip.channels)
                {
                    CookedChannel cookedChannel{};
                    cookedChannel.bone = channel.bone.Value();
                    cookedChannel.translationInterpolation =
                        static_cast<std::uint8_t>(channel.translationInterpolation);
                    cookedChannel.rotationInterpolation =
                        static_cast<std::uint8_t>(channel.rotationInterpolation);
                    cookedChannel.scaleInterpolation =
                        static_cast<std::uint8_t>(channel.scaleInterpolation);

                    cookedChannel.translationBegin =
                        static_cast<std::uint32_t>(translations.size());
                    cookedChannel.translationCount =
                        static_cast<std::uint32_t>(channel.translations.size());
                    translations.insert(translations.end(),
                        channel.translations.begin(), channel.translations.end());

                    cookedChannel.rotationBegin =
                        static_cast<std::uint32_t>(rotations.size());
                    cookedChannel.rotationCount =
                        static_cast<std::uint32_t>(channel.rotations.size());
                    rotations.insert(rotations.end(),
                        channel.rotations.begin(), channel.rotations.end());

                    cookedChannel.scaleBegin = static_cast<std::uint32_t>(scales.size());
                    cookedChannel.scaleCount =
                        static_cast<std::uint32_t>(channel.scales.size());
                    scales.insert(scales.end(),
                        channel.scales.begin(), channel.scales.end());

                    channels.push_back(cookedChannel);
                }
                clips.push_back(cookedClip);
            }
        }

        // ── 애니메이터 ──────────────────────────────────────────────────
        CookedAnimator animator{};
        if (draft.animator.has_value())
        {
            animator.present = 1u;
            animator.defaultClip = draft.animator->defaultClip.Value();
            animator.motionAssetId = draft.animator->motionAssetId.value;
        }

        // ── 재질 (가변 길이) ────────────────────────────────────────────
        // 개수가 자산당 2~6개라 속도와 무관하다. 그래서 POD 배열로 펴는 대신
        // 커서 방식으로 단순하게 둔다 — 여기까지 펴면 코드만 늘고 얻는 게 없다.
        ByteWriter materialBytes;
        for (const Material& material : draft.materials)
        {
            materialBytes.Pod(material.assetId.value);
            materialBytes.Pod(material.shaderAssetId.value);
            materialBytes.Pod(strings.Add(material.name));
            materialBytes.Pod(static_cast<std::uint8_t>(material.blendMode));

            materialBytes.Pod(static_cast<std::uint32_t>(material.properties.size()));
            for (const MaterialProperty& property : material.properties)
            {
                materialBytes.Pod(strings.Add(property.name));
                materialBytes.Pod(static_cast<std::uint8_t>(property.value.index()));
                std::visit([&](const auto& value)
                {
                    using T = std::decay_t<decltype(value)>;
                    if constexpr (std::is_same_v<T, std::string>)
                    {
                        materialBytes.Pod(strings.Add(value));
                    }
                    else if constexpr (std::is_same_v<T, TextureReference>)
                    {
                        materialBytes.Pod(value.assetId.value);
                        materialBytes.Pod(strings.Add(value.logicalName));
                        materialBytes.Pod(strings.Add(PathToUtf8(value.fallbackPath)));
                        materialBytes.Pod(static_cast<std::uint8_t>(value.colorSpace));
                    }
                    else
                    {
                        materialBytes.Pod(value);
                    }
                }, property.value);
            }

            materialBytes.Pod(static_cast<std::uint32_t>(material.keywords.size()));
            for (const std::string& keyword : material.keywords)
                materialBytes.Pod(strings.Add(keyword));

            materialBytes.Pod(static_cast<std::uint32_t>(material.keywordSelections.size()));
            for (const std::uint16_t selection : material.keywordSelections)
                materialBytes.Pod(selection);
        }

        // ── 메타데이터 ──────────────────────────────────────────────────
        // ★ 문자열 테이블을 확정하기 전 마지막으로 담는다. 이 뒤에 Add 를 부르면
        //   테이블이 커져서 이미 계산한 오프셋이 어긋난다.
        CookedMetadata metadata{};
        metadata.assetId = draft.metadata.assetId.value;
        metadata.name = strings.Add(draft.metadata.name);
        metadata.sourcePath = strings.Add(PathToUtf8(draft.metadata.sourcePath));
        metadata.cookedPath = strings.Add(PathToUtf8(draft.metadata.cookedPath));
        metadata.sourceWriteTimeTicks = static_cast<std::int64_t>(
            draft.metadata.sourceWriteTime.time_since_epoch().count());
        metadata.payloadKind = static_cast<std::uint8_t>(draft.metadata.payloadKind);

        // ── 섹션 조립 ───────────────────────────────────────────────────
        struct PendingSection final
        {
            SectionKind kind{};
            std::uint32_t elementCount{};
            const void* data{};
            std::uint64_t bytes{};
        };

        const std::vector<PendingSection> pending{
            { SectionKind::Strings,  static_cast<std::uint32_t>(strings.Bytes().size()),
              strings.Bytes().data(), strings.Bytes().size() },
            { SectionKind::Metadata, 1u, &metadata, sizeof(metadata) },
            { SectionKind::Nodes, static_cast<std::uint32_t>(nodes.size()),
              nodes.data(), nodes.size() * sizeof(CookedNode) },
            { SectionKind::NodeMeshes, static_cast<std::uint32_t>(nodeMeshes.size()),
              nodeMeshes.data(), nodeMeshes.size() * sizeof(std::uint32_t) },
            { SectionKind::Meshes, static_cast<std::uint32_t>(meshes.size()),
              meshes.data(), meshes.size() * sizeof(CookedMesh) },
            { SectionKind::Vertices, static_cast<std::uint32_t>(vertexBytes.size()),
              vertexBytes.data(), vertexBytes.size() },
            { SectionKind::Indices, static_cast<std::uint32_t>(indices.size()),
              indices.data(), indices.size() * sizeof(std::uint32_t) },
            { SectionKind::Materials, static_cast<std::uint32_t>(draft.materials.size()),
              materialBytes.Bytes().data(), materialBytes.Size() },
            { SectionKind::Bones, static_cast<std::uint32_t>(bones.size()),
              bones.data(), bones.size() * sizeof(CookedBone) },
            { SectionKind::Clips, static_cast<std::uint32_t>(clips.size()),
              clips.data(), clips.size() * sizeof(CookedClip) },
            { SectionKind::Channels, static_cast<std::uint32_t>(channels.size()),
              channels.data(), channels.size() * sizeof(CookedChannel) },
            { SectionKind::TranslationKeys, static_cast<std::uint32_t>(translations.size()),
              translations.data(), translations.size() * sizeof(TranslationKey) },
            { SectionKind::RotationKeys, static_cast<std::uint32_t>(rotations.size()),
              rotations.data(), rotations.size() * sizeof(RotationKey) },
            { SectionKind::ScaleKeys, static_cast<std::uint32_t>(scales.size()),
              scales.data(), scales.size() * sizeof(ScaleKey) },
            { SectionKind::Skeleton, 1u, &skeletonHeader, sizeof(skeletonHeader) },
            { SectionKind::Animator, 1u, &animator, sizeof(animator) },
        };

        const std::uint64_t tableBytes = pending.size() * sizeof(SectionEntry);
        std::uint64_t cursor = AlignUp(sizeof(FileHeader) + tableBytes);

        std::vector<SectionEntry> table;
        table.reserve(pending.size());
        for (const PendingSection& section : pending)
        {
            SectionEntry entry{};
            entry.kind = static_cast<std::uint32_t>(section.kind);
            entry.elementCount = section.elementCount;
            entry.offset = cursor;
            entry.bytes = section.bytes;
            table.push_back(entry);
            cursor = AlignUp(cursor + section.bytes);
        }

        FileHeader header{};
        header.vertexLayoutTableHash = VertexLayoutTableHash();
        header.maxVertexStride = maxVertexStride;
        header.vertexAttributeMaskUnion = vertexAttributeMaskUnion;
        header.fileBytes = cursor;
        header.sectionCount = static_cast<std::uint32_t>(pending.size());

        std::vector<std::byte> file(static_cast<std::size_t>(cursor), std::byte{});
        std::memcpy(file.data(), &header, sizeof(header));
        std::memcpy(file.data() + sizeof(header), table.data(),
            static_cast<std::size_t>(tableBytes));
        for (std::size_t i = 0; i < pending.size(); ++i)
        {
            if (0 == pending[i].bytes) continue;
            std::memcpy(file.data() + table[i].offset, pending[i].data,
                static_cast<std::size_t>(pending[i].bytes));
        }
        return file;
    }

    // ════════════════════════════════════════════════════════════════════
    // 읽기
    // ════════════════════════════════════════════════════════════════════

    bool Read(std::span<const std::byte> bytes, ModelDraft& outDraft,
        std::vector<ModelLoadIssue>& issues)
    {
        outDraft = ModelDraft{};
        ModelDraft decoded{};

        if (bytes.size() < sizeof(FileHeader))
        {
            Reject(issues, "header", "파일이 헤더보다 짧다.");
            return false;
        }

        FileHeader header{};
        std::memcpy(&header, bytes.data(), sizeof(header));

        if (kMagic != header.magic)
        {
            Reject(issues, "header", "매직이 다르다 — 쿠킹 파일이 아니다.");
            return false;
        }
        if (kFormatVersion != header.formatVersion)
        {
            Reject(issues, "header", "포맷 버전 " + std::to_string(header.formatVersion)
                + " != " + std::to_string(kFormatVersion) + " — 재임포트 필요.");
            return false;
        }
        // ★ 이 검사가 이 포맷의 존재 이유 절반이다. legacy 는 이게 없어서
        //   레이아웃이 바뀌면 조용히 오독했다.
        if (VertexLayoutTableHash() != header.vertexLayoutTableHash)
        {
            Reject(issues, "header", "정점 레이아웃 표 해시 불일치 — 재임포트 필요.");
            return false;
        }
        if ((header.vertexAttributeMaskUnion & ~kAllVertexAttributes) != 0)
        {
            Reject(issues, "header", "정점 속성 union에 모르는 비트가 있다.");
            return false;
        }
        if (header.maxVertexStride > StrideOf(kAllVertexAttributes))
        {
            Reject(issues, "header", "최대 정점 stride가 기술표 범위를 넘는다.");
            return false;
        }
        if (header.fileBytes != bytes.size())
        {
            Reject(issues, "header", "파일 크기가 헤더와 다르다 — 잘렸거나 덧붙었다.");
            return false;
        }

        const std::uint64_t tableBytes =
            static_cast<std::uint64_t>(header.sectionCount) * sizeof(SectionEntry);
        if (sizeof(FileHeader) + tableBytes > bytes.size())
        {
            Reject(issues, "header", "섹션 표가 파일 밖을 넘어간다.");
            return false;
        }

        std::vector<SectionEntry> table(header.sectionCount);
        if (header.sectionCount)
        {
            std::memcpy(table.data(), bytes.data() + sizeof(FileHeader),
                static_cast<std::size_t>(tableBytes));
        }

        // ★ 없는 섹션을 0개로 읽지 않는다. 그러면 손상된 파일이 "빈 모델"로
        //   통과해 버린다 — 이 저장소가 이미 겪은 거짓 통과 양식이다.
        std::vector<const SectionEntry*> byKind(
            static_cast<std::size_t>(SectionKind::Count), nullptr);
        for (const SectionEntry& entry : table)
        {
            if (entry.kind >= static_cast<std::uint32_t>(SectionKind::Count))
            {
                Reject(issues, "sections", "모르는 섹션 종류 "
                    + std::to_string(entry.kind) + " — 상위 버전 파일이다.");
                return false;
            }
            byKind[entry.kind] = &entry;
        }
        for (std::uint32_t kind = 0; kind < static_cast<std::uint32_t>(SectionKind::Count);
            ++kind)
        {
            if (!byKind[kind])
            {
                Reject(issues, "sections",
                    "섹션 " + std::to_string(kind) + " 이 없다 — 파일이 불완전하다.");
                return false;
            }
        }

        const auto section = [&](SectionKind kind) -> const SectionEntry&
        {
            return *byKind[static_cast<std::size_t>(kind)];
        };

        std::span<const char> stringBytes;
        std::span<const CookedNode> nodes;
        std::span<const std::uint32_t> nodeMeshes;
        std::span<const CookedMesh> meshes;
        std::span<const std::byte> vertexBytes;
        std::span<const std::uint32_t> indices;
        std::span<const CookedBone> bones;
        std::span<const CookedClip> clips;
        std::span<const CookedChannel> channels;
        std::span<const TranslationKey> translations;
        std::span<const RotationKey> rotations;
        std::span<const ScaleKey> scales;
        std::span<const CookedSkeletonHeader> skeletonHeaders;
        std::span<const CookedAnimator> animators;
        std::span<const CookedMetadata> metadatas;

        if (!ViewArray(bytes, section(SectionKind::Strings), stringBytes, issues, "strings")
            || !ViewArray(bytes, section(SectionKind::Metadata), metadatas, issues, "metadata")
            || !ViewArray(bytes, section(SectionKind::Nodes), nodes, issues, "nodes")
            || !ViewArray(bytes, section(SectionKind::NodeMeshes), nodeMeshes, issues, "nodeMeshes")
            || !ViewArray(bytes, section(SectionKind::Meshes), meshes, issues, "meshes")
            || !ViewArray(bytes, section(SectionKind::Vertices), vertexBytes, issues, "vertices")
            || !ViewArray(bytes, section(SectionKind::Indices), indices, issues, "indices")
            || !ViewArray(bytes, section(SectionKind::Bones), bones, issues, "bones")
            || !ViewArray(bytes, section(SectionKind::Clips), clips, issues, "clips")
            || !ViewArray(bytes, section(SectionKind::Channels), channels, issues, "channels")
            || !ViewArray(bytes, section(SectionKind::TranslationKeys), translations, issues, "translationKeys")
            || !ViewArray(bytes, section(SectionKind::RotationKeys), rotations, issues, "rotationKeys")
            || !ViewArray(bytes, section(SectionKind::ScaleKeys), scales, issues, "scaleKeys")
            || !ViewArray(bytes, section(SectionKind::Skeleton), skeletonHeaders, issues, "skeleton")
            || !ViewArray(bytes, section(SectionKind::Animator), animators, issues, "animator"))
        {
            return false;
        }

        if (1 != metadatas.size() || 1 != skeletonHeaders.size() || 1 != animators.size())
        {
            Reject(issues, "sections", "단일 레코드 섹션의 원소 수가 1이 아니다.");
            return false;
        }

        bool stringFailed = false;
        const auto readString = [&](StringRef ref) -> std::string
        {
            if (0 == ref.length) return {};
            if (static_cast<std::uint64_t>(ref.offset) + ref.length > stringBytes.size())
            {
                stringFailed = true;
                return {};
            }
            return std::string(stringBytes.data() + ref.offset, ref.length);
        };

        // ── 메타데이터 ──────────────────────────────────────────────────
        const CookedMetadata& metadata = metadatas[0];
        decoded.metadata.assetId.value = metadata.assetId;
        decoded.metadata.name = readString(metadata.name);
        decoded.metadata.sourcePath = Utf8ToPath(readString(metadata.sourcePath));
        decoded.metadata.cookedPath = Utf8ToPath(readString(metadata.cookedPath));
        decoded.metadata.sourceWriteTime = std::filesystem::file_time_type(
            std::filesystem::file_time_type::duration(metadata.sourceWriteTimeTicks));
        decoded.metadata.payloadKind = ModelPayloadKind::Cooked;

        // ── 노드 ────────────────────────────────────────────────────────
        decoded.nodes.resize(nodes.size());
        for (std::size_t i = 0; i < nodes.size(); ++i)
        {
            const CookedNode& source = nodes[i];
            ModelNode& node = decoded.nodes[i];
            node.name = readString(source.name);
            node.parent = NodeIndex(source.parent);
            node.localTransform = source.localTransform;
            if (!InRange(source.meshBegin, source.meshCount, nodeMeshes.size()))
            {
                Reject(issues, "nodes", "노드 메시 범위가 블록 밖이다.");
                return false;
            }
            node.meshes.resize(source.meshCount);
            for (std::uint32_t m = 0; m < source.meshCount; ++m)
                node.meshes[m] = MeshIndex(nodeMeshes[source.meshBegin + m]);
        }

        // ── 메시 ────────────────────────────────────────────────────────
        decoded.meshes.resize(meshes.size());
        VertexAttributeMask actualMaskUnion = 0;
        std::uint32_t actualMaxStride = 0;
        for (std::size_t i = 0; i < meshes.size(); ++i)
        {
            const CookedMesh& source = meshes[i];
            Mesh& mesh = decoded.meshes[i];
            mesh.name = readString(source.name);
            mesh.material = MaterialIndex(source.material);
            mesh.bounds = source.bounds;

            if (!VertexBuffer::IsSupportedLayout(source.vertexAttributeMask)
                || source.vertexStride != StrideOf(source.vertexAttributeMask))
            {
                Reject(issues, "meshes", "메시의 정점 mask/stride 계약이 어긋났다.");
                return false;
            }
            const std::uint64_t vertexByteCount =
                static_cast<std::uint64_t>(source.vertexCount) * source.vertexStride;
            if (source.vertexByteBegin > vertexBytes.size()
                || vertexByteCount > vertexBytes.size() - source.vertexByteBegin
                || !InRange(source.indexBegin, source.indexCount, indices.size()))
            {
                Reject(issues, "meshes", "메시의 정점/인덱스 범위가 블록 밖이다.");
                return false;
            }
            // ★ 정점은 mesh별 stride를 유지한 packed 블록 그대로 한 번에 복사한다.
            const auto packed = vertexBytes.subspan(source.vertexByteBegin,
                static_cast<std::size_t>(vertexByteCount));
            if (!mesh.vertices.AssignPacked(source.vertexAttributeMask,
                source.vertexCount, packed))
            {
                Reject(issues, "meshes", "packed 정점 블록을 구성하지 못했다.");
                return false;
            }
            mesh.indices.assign(indices.begin() + source.indexBegin,
                indices.begin() + source.indexBegin + source.indexCount);
            actualMaskUnion |= source.vertexAttributeMask;
            actualMaxStride = (std::max)(actualMaxStride, source.vertexStride);
        }
        if (actualMaskUnion != header.vertexAttributeMaskUnion
            || actualMaxStride != header.maxVertexStride)
        {
            Reject(issues, "header", "정점 mask union/max stride가 mesh 레코드와 다르다.");
            return false;
        }

        // ── 스켈레톤 ────────────────────────────────────────────────────
        const CookedSkeletonHeader& skeletonHeader = skeletonHeaders[0];
        if (skeletonHeader.present)
        {
            Skeleton skeleton{};
            skeleton.rootBone = BoneIndex(skeletonHeader.rootBone);
            skeleton.rootTransform = skeletonHeader.rootTransform;
            skeleton.globalInverseTransform = skeletonHeader.globalInverseTransform;

            skeleton.bones.resize(bones.size());
            for (std::size_t i = 0; i < bones.size(); ++i)
            {
                skeleton.bones[i].name = readString(bones[i].name);
                skeleton.bones[i].parent = BoneIndex(bones[i].parent);
                skeleton.bones[i].inverseBindMatrix = bones[i].inverseBindMatrix;
            }

            skeleton.clips.resize(clips.size());
            for (std::size_t c = 0; c < clips.size(); ++c)
            {
                const CookedClip& sourceClip = clips[c];
                AnimationClip& clip = skeleton.clips[c];
                clip.name = readString(sourceClip.name);
                clip.durationTicks = sourceClip.durationTicks;
                clip.ticksPerSecond = sourceClip.ticksPerSecond;
                clip.looping = 0 != sourceClip.looping;

                if (!InRange(sourceClip.channelBegin, sourceClip.channelCount,
                    channels.size()))
                {
                    Reject(issues, "clips", "클립의 채널 범위가 블록 밖이다.");
                    return false;
                }
                clip.channels.resize(sourceClip.channelCount);
                for (std::uint32_t n = 0; n < sourceClip.channelCount; ++n)
                {
                    const CookedChannel& sourceChannel =
                        channels[sourceClip.channelBegin + n];
                    AnimationChannel& channel = clip.channels[n];
                    channel.bone = BoneIndex(sourceChannel.bone);
                    channel.translationInterpolation = static_cast<InterpolationMode>(
                        sourceChannel.translationInterpolation);
                    channel.rotationInterpolation = static_cast<InterpolationMode>(
                        sourceChannel.rotationInterpolation);
                    channel.scaleInterpolation = static_cast<InterpolationMode>(
                        sourceChannel.scaleInterpolation);

                    if (!InRange(sourceChannel.translationBegin,
                            sourceChannel.translationCount, translations.size())
                        || !InRange(sourceChannel.rotationBegin,
                            sourceChannel.rotationCount, rotations.size())
                        || !InRange(sourceChannel.scaleBegin,
                            sourceChannel.scaleCount, scales.size()))
                    {
                        Reject(issues, "channels", "채널의 키 범위가 블록 밖이다.");
                        return false;
                    }

                    // ★ 이 세 줄이 legacy 가 시간의 80% 를 쓰던 자리다.
                    //   표현이 같으니 키마다 도는 루프가 아예 없다.
                    channel.translations.assign(
                        translations.begin() + sourceChannel.translationBegin,
                        translations.begin() + sourceChannel.translationBegin
                            + sourceChannel.translationCount);
                    channel.rotations.assign(
                        rotations.begin() + sourceChannel.rotationBegin,
                        rotations.begin() + sourceChannel.rotationBegin
                            + sourceChannel.rotationCount);
                    channel.scales.assign(
                        scales.begin() + sourceChannel.scaleBegin,
                        scales.begin() + sourceChannel.scaleBegin
                            + sourceChannel.scaleCount);
                }
            }
            decoded.skeleton = std::move(skeleton);
        }
        else if (!bones.empty() || !clips.empty())
        {
            Reject(issues, "skeleton", "스켈레톤이 없다는데 뼈/클립이 있다.");
            return false;
        }

        // ── 애니메이터 ──────────────────────────────────────────────────
        const CookedAnimator& animator = animators[0];
        if (animator.present)
        {
            AnimatorData data{};
            data.motionAssetId.value = animator.motionAssetId;
            data.defaultClip = AnimationClipIndex(animator.defaultClip);
            decoded.animator = data;
        }

        // ── 재질 ────────────────────────────────────────────────────────
        {
            const SectionEntry& entry = section(SectionKind::Materials);
            if (entry.offset > bytes.size() || entry.bytes > bytes.size() - entry.offset)
            {
                Reject(issues, "materials", "재질 섹션이 파일 밖을 가리킨다.");
                return false;
            }
            ByteCursor cursor(bytes.subspan(
                static_cast<std::size_t>(entry.offset),
                static_cast<std::size_t>(entry.bytes)));

            decoded.materials.resize(entry.elementCount);
            for (std::uint32_t i = 0; i < entry.elementCount; ++i)
            {
                Material& material = decoded.materials[i];
                material.assetId.value = cursor.Pod<Uuid::Uuid16>();
                material.shaderAssetId.value = cursor.Pod<Uuid::Uuid16>();
                material.name = readString(cursor.Pod<StringRef>());
                material.blendMode =
                    static_cast<MaterialBlendMode>(cursor.Pod<std::uint8_t>());

                const std::uint32_t propertyCount = cursor.Pod<std::uint32_t>();
                if (!cursor.Ok()) break;
                material.properties.resize(propertyCount);
                for (std::uint32_t p = 0; p < propertyCount && cursor.Ok(); ++p)
                {
                    MaterialProperty& property = material.properties[p];
                    property.name = readString(cursor.Pod<StringRef>());
                    const std::uint8_t typeIndex = cursor.Pod<std::uint8_t>();
                    switch (typeIndex)
                    {
                    case 0: property.value = 0 != cursor.Pod<std::uint8_t>(); break;
                    case 1: property.value = cursor.Pod<std::int32_t>(); break;
                    case 2: property.value = cursor.Pod<std::uint32_t>(); break;
                    case 3: property.value = cursor.Pod<float>(); break;
                    case 4: property.value = cursor.Pod<math::vector2>(); break;
                    case 5: property.value = cursor.Pod<math::vector3>(); break;
                    case 6: property.value = cursor.Pod<math::vector4>(); break;
                    case 7: property.value = readString(cursor.Pod<StringRef>()); break;
                    case 8:
                    {
                        TextureReference reference{};
                        reference.assetId.value = cursor.Pod<Uuid::Uuid16>();
                        reference.logicalName = readString(cursor.Pod<StringRef>());
                        reference.fallbackPath =
                            Utf8ToPath(readString(cursor.Pod<StringRef>()));
                        reference.colorSpace =
                            static_cast<TextureColorSpace>(cursor.Pod<std::uint8_t>());
                        property.value = std::move(reference);
                        break;
                    }
                    default:
                        Reject(issues, "materials",
                            "모르는 property 타입 " + std::to_string(typeIndex) + ".");
                        return false;
                    }
                }

                const std::uint32_t keywordCount = cursor.Pod<std::uint32_t>();
                if (!cursor.Ok()) break;
                material.keywords.resize(keywordCount);
                for (std::uint32_t k = 0; k < keywordCount && cursor.Ok(); ++k)
                    material.keywords[k] = readString(cursor.Pod<StringRef>());

                const std::uint32_t selectionCount = cursor.Pod<std::uint32_t>();
                if (!cursor.Ok()) break;
                material.keywordSelections.resize(selectionCount);
                for (std::uint32_t k = 0; k < selectionCount && cursor.Ok(); ++k)
                    material.keywordSelections[k] = cursor.Pod<std::uint16_t>();
            }

            if (!cursor.Ok())
            {
                Reject(issues, "materials", "재질 섹션이 도중에 끝났다.");
                return false;
            }
        }

        // ★ 문자열 참조 실패는 마지막에 한 번에 본다. 중간에 던지지 않는 이유는
        //   손상된 파일에서 어디까지 읽혔는지가 아니라 **거부했다는 사실**만
        //   중요하기 때문이다.
        if (stringFailed)
        {
            Reject(issues, "strings", "문자열 참조가 테이블 밖을 가리킨다.");
            return false;
        }
        outDraft = std::move(decoded);
        return true;
    }

    // ════════════════════════════════════════════════════════════════════
    // 디코더
    // ════════════════════════════════════════════════════════════════════

    ModelDecodeResult CookedModelDecoder::Decode(const ModelLoadRequest& request)
    {
        ModelDecodeResult result{};

        if (request.cookedPath.empty())
        {
            // 경로를 **유추하지 않는다**. legacy 는 쓰기·읽기·판정이 세 군데로
            // 갈라져 있고 그중 판정만 다른 곳을 봐서, Assets/Models/ 밖의 모델은
            // 쿠킹이 있어도 못 쓴다. 그 함정을 물려받지 않는다.
            Reject(result.issues, "request", "cookedPath 가 비어 있다.");
            return result;
        }

        std::error_code errorCode;
        const auto size = std::filesystem::file_size(request.cookedPath, errorCode);
        if (errorCode)
        {
            Reject(result.issues, "file", "쿠킹 파일을 열 수 없다: "
                + request.cookedPath.filename().string());
            return result;
        }

        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        {
            // ★ std::ifstream 을 쓰지 않는다. 실측(Gunner 1.85MB, 30회 최솟값):
            //
            //     ifstream 1회 read   1.123ms
            //     fopen/fread         0.159ms   ← 7.1배 빠르다
            //     ReadFile 1회        0.161ms   (같다 — Windows API 를 쓸 이유가 없다)
            //
            //   처음엔 ifstream 으로 짰고, 그 상태에서 쿠킹 로드 2.827ms 중
            //   1.762ms 가 파일 읽기였다(파싱은 0.318ms 뿐이었다). 포맷을 아무리
            //   잘 만들어도 읽는 수단이 병목이면 소용이 없다.
            //
            // ★ 경로는 반드시 wide 로 연다. std::fopen 은 narrow 만 받아서
            //   한글 경로에서 조용히 실패한다.
            std::FILE* file = nullptr;
#if defined(_WIN32)
            // _s 변형을 쓴다 — MSVC 가 비-_s 를 C4996 으로 막는데, 여기서
            // 경고를 끄면 이 TU 의 다른 실수까지 같이 숨는다.
            if (0 != ::_wfopen_s(&file, request.cookedPath.c_str(), L"rb")) file = nullptr;
#else
            file = std::fopen(request.cookedPath.c_str(), "rb");
#endif
            if (!file)
            {
                Reject(result.issues, "file", "쿠킹 파일 열기 실패.");
                return result;
            }
            const std::size_t got = std::fread(bytes.data(), 1, bytes.size(), file);
            std::fclose(file);
            if (got != bytes.size())
            {
                Reject(result.issues, "file", "쿠킹 파일을 끝까지 읽지 못했다.");
                return result;
            }
        }

        ModelDraft draft{};
        if (!Read(bytes, draft, result.issues)) return result;

        result.draft = std::move(draft);
        return result;
    }
}
