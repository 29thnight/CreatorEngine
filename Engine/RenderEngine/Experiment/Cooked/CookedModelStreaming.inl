// Current CEGE contains authenticated metadata and QMC2 pages only.
namespace
{
MeshletPayload ThinMeshlets(const MeshletPayload& source)
{
    MeshletPayload result;
    result.settings = source.settings; result.geometryDigest = source.geometryDigest;
    result.descriptors = source.descriptors; result.lod0 = source.lod0;
    return result;
}

std::vector<std::vector<std::byte>> GeometryTopologyPages(const ModelGeometryArtifact& value,
                                                         const assets::cluster::Archive& attributes)
{
    std::vector<std::vector<std::byte>> result;
    const auto append = [&](const MeshletPayload& meshlets, uint32_t primitives) {
        for (const auto& descriptor : meshlets.descriptors)
            result.push_back(assets::cluster::EncodeTopologyPage(attributes.packets.at(result.size()), meshlets,
                descriptor, static_cast<uint32_t>(value.mesh.vertices.size()), primitives));
    };
    append(value.mesh.meshlets, static_cast<uint32_t>(value.mesh.indices.size() / 3));
    for (const auto& level : value.mesh.coarseLods.levels)
        append(level.meshlets, static_cast<uint32_t>(level.indices.size() / 3));
    return result;
}

WireWriter GeometryStreamMetadata(const ModelGeometryArtifact& value,
                                  const assets::cluster::Archive& attributes,
                                  const std::vector<std::vector<std::byte>>& pages)
{
    WireWriter wire;
    wire.Integer(kVertexLayoutTableHash);
    wire.Integer(value.mesh.vertices.AttributeMask()); wire.Integer(value.mesh.vertices.Stride());
    wire.Integer(static_cast<uint32_t>(value.mesh.vertices.size()));
    GeometryBounds(wire, value.mesh.bounds);
    const auto cpu = assets::cpu_geometry::Encode(value.mesh.vertices.AttributeMask(),
        static_cast<uint32_t>(value.mesh.vertices.size()), value.mesh.vertices.Bytes(), value.mesh.indices);
    Sha256Digest cpuDigest{}; std::string cpuError;
    Require(ComputeSha256(cpu, cpuDigest, cpuError), "Cannot hash exact CPU geometry");
    wire.Integer(static_cast<uint64_t>(cpu.size())); wire.Bytes(cpuDigest);
    wire.Integer(value.requiredBoneCount); wire.Bytes(value.requiredSkinBindingSha256);
    GeometryMeshlets(wire, ThinMeshlets(value.mesh.meshlets));
    MeshLodChain thin;
    thin.settings = value.mesh.coarseLods.settings; thin.geometryDigest = value.mesh.coarseLods.geometryDigest;
    thin.groups = value.mesh.coarseLods.groups;
    for (const auto& level : value.mesh.coarseLods.levels)
    {
        MeshLodLevel item; item.geometricError = level.geometricError; item.meshlets = ThinMeshlets(level.meshlets);
        thin.levels.push_back(std::move(item));
    }
    GeometryLods(wire, thin);
    wire.Integer(static_cast<uint32_t>(thin.levels.size() + 1));
    const auto counts = [&](const MeshletPayload& meshlets, uint32_t indexCount) {
        wire.Integer(indexCount); wire.Integer(static_cast<uint32_t>(meshlets.vertexRemap.size()));
        wire.Integer(static_cast<uint32_t>(meshlets.triangleIndices.size()));
        wire.Integer(static_cast<uint32_t>(meshlets.primitiveRemap.size()));
    };
    counts(value.mesh.meshlets, static_cast<uint32_t>(value.mesh.indices.size()));
    for (const auto& level : value.mesh.coarseLods.levels) counts(level.meshlets, static_cast<uint32_t>(level.indices.size()));
    wire.Float(attributes.grid.positionError); wire.Integer(static_cast<uint32_t>(pages.size()));
    uint64_t offset{};
    for (const auto& page : pages)
    {
        Sha256Digest digest{}; std::string error;
        Require(ComputeSha256(page, digest, error), "Cannot hash topology page");
        wire.Integer(offset); wire.Integer(static_cast<uint32_t>(page.size())); wire.Bytes(digest);
        offset += page.size();
    }
    return wire;
}

void ValidateStreamDescriptors(ModelGeometryStreamMetadata& value)
{
    auto flat = std::make_shared<std::vector<MeshletDescriptor>>();
    uint64_t primitiveBase{};
    const auto append = [&](const MeshletPayload& payload, uint32_t level) {
        const auto& settings = payload.settings;
        Require(settings.profileVersion == kMeshletProfileVersion && settings.builderVersion == kMeshletBuilderVersion &&
                    settings.meshoptimizerVersion == kMeshletMeshoptimizerVersion && settings.maxVertices &&
                    settings.maxVertices <= kMeshletMaxVertices && settings.maxTriangles && settings.maxTriangles <= kMeshletMaxTriangles &&
                    payload.vertexRemap.empty() && payload.triangleIndices.empty() && payload.primitiveRemap.empty() &&
                    payload.lod0.firstMeshlet == 0 && payload.lod0.meshletCount == payload.descriptors.size() && !payload.descriptors.empty(),
                "Streaming descriptor profile/connectivity policy");
        const auto& counts = value.counts.at(level);
        GeometryHeader(value.attributes, value.stride, value.vertexCount, counts.indexCount);
        uint64_t vertices{}, triangles{}, primitives{};
        for (uint32_t ordinal = 0; ordinal < payload.descriptors.size(); ++ordinal)
        {
            auto descriptor = payload.descriptors[ordinal];
            Require(descriptor.clusterId == ordinal && descriptor.vertexOffset == vertices && descriptor.triangleOffset == triangles &&
                        descriptor.primitiveOffset == primitives && descriptor.vertexCount && descriptor.vertexCount <= settings.maxVertices &&
                        descriptor.triangleCount && descriptor.triangleCount <= settings.maxTriangles && descriptor.sphereRadius >= 0,
                    "Streaming descriptor bounds/range/order");
            vertices += descriptor.vertexCount; triangles += descriptor.triangleCount * 3ull; primitives += descriptor.triangleCount;
            descriptor.clusterId = static_cast<uint32_t>(flat->size()); descriptor.reservedBounds = static_cast<uint32_t>(primitiveBase);
            flat->push_back(descriptor);
        }
        Require(vertices == counts.vertexRemapCount && triangles == counts.triangleByteCount && triangles == counts.indexCount &&
                    primitives == counts.primitiveRemapCount && primitives == counts.indexCount / 3,
                "Streaming descriptor logical extents");
        primitiveBase += primitives;
        Require(primitiveBase <= UINT32_MAX, "Streaming primitive address budget");
    };
    append(value.meshlets, 0);
    for (uint32_t level = 0; level < value.lods.levels.size(); ++level)
    {
        Require(value.lods.levels[level].indices.empty() && value.lods.levels[level].geometricError >= 0,
                "Streaming LOD contains bulk indices or invalid error");
        append(value.lods.levels[level].meshlets, level + 1);
    }
    Require(flat->size() == value.archive->packets.size(), "Streaming descriptor/page count mismatch");
    const auto& groups = value.lods.groups;
    std::vector<bool> covered(flat->size());
    for (uint32_t ordinal = 0; ordinal < groups.size(); ++ordinal)
    {
        const auto& group = groups[ordinal];
        Require(group.meshletCount && uint64_t(group.firstMeshlet) + group.meshletCount <= flat->size() &&
                    group.lodIndex < value.counts.size() && group.geometricError >= 0 && group.sphere[3] >= 0 &&
                    (group.parentGroup == UINT32_MAX ? ordinal + 1 == groups.size() : group.parentGroup > ordinal && group.parentGroup < groups.size()),
                "Streaming DAG range/order/bounds");
        if (group.childGroup == UINT32_MAX)
            Require(group.secondChild == UINT32_MAX && group.lodIndex == 0 && group.geometricError == 0, "Streaming DAG leaf");
        else
            Require(group.childGroup < ordinal && group.secondChild < ordinal && group.childGroup != group.secondChild &&
                        groups[group.childGroup].parentGroup == ordinal && groups[group.secondChild].parentGroup == ordinal &&
                        groups[group.childGroup].geometricError <= group.geometricError && groups[group.secondChild].geometricError <= group.geometricError,
                    "Streaming DAG child ownership/error");
        uint64_t first{}, base{};
        for (uint32_t level = 0; level < group.lodIndex; ++level)
        {
            first += level ? value.lods.levels[level - 1].meshlets.descriptors.size() : value.meshlets.descriptors.size();
            base += value.counts[level].indexCount / 3;
        }
        const auto count = group.lodIndex ? value.lods.levels[group.lodIndex - 1].meshlets.descriptors.size() : value.meshlets.descriptors.size();
        Require(group.levelFirstMeshlet == first && group.primitiveBase == base && group.firstMeshlet >= first &&
                    uint64_t(group.firstMeshlet) + group.meshletCount <= first + count, "Streaming DAG LOD range");
        for (uint32_t index = 0; index < group.meshletCount; ++index)
        {
            const auto meshlet = group.firstMeshlet + index;
            Require(!covered[meshlet], "Streaming DAG overlapping groups");
            covered[meshlet] = true; (*flat)[meshlet].reserved = ordinal;
        }
    }
    Require(groups.empty() ? value.lods.levels.empty() : std::ranges::all_of(covered, [](bool v) { return v; }),
            "Streaming DAG topology coverage");
    value.flatDescriptors = std::move(flat);
}
}

bool ModelGeometryStreamPrefixSize(std::span<const std::byte> header, uint64_t totalBytes, uint32_t& prefixBytes, std::string& failure)
{
    return Checked([&] {
        WireReader reader(header); reader.Header(kGeometryMagic, kModelGeometryArtifactVersion);
        const auto size = reader.Integer<uint32_t>(); reader.End();
        Require(size && size <= kModelSubAssetMaxBytes - 12 && totalBytes <= kModelSubAssetMaxBytes && totalBytes > uint64_t(size) + 12,
                "Streaming metadata prefix extent");
        prefixBytes = size + 12;
    }, failure);
}

bool ReadModelGeometryStreamMetadata(std::span<const std::byte> prefix, uint64_t totalBytes,
                                     ModelGeometryStreamMetadata& out, std::string& failure)
{
    return Checked([&] {
        uint32_t prefixBytes{};
        Require(prefix.size() >= 12 && ModelGeometryStreamPrefixSize(prefix.first(12), totalBytes, prefixBytes, failure) && prefix.size() == prefixBytes,
                "Malformed thin metadata prefix");
        WireReader reader(prefix.subspan(12)); ModelGeometryStreamMetadata value;
        Require(reader.Integer<uint64_t>() == kVertexLayoutTableHash, "Streaming vertex ABI");
        value.attributes = reader.Integer<uint32_t>(); value.stride = reader.Integer<uint32_t>(); value.vertexCount = reader.Integer<uint32_t>();
        value.bounds = GeometryBounds(reader);
        value.cpuGeometryBytes = reader.Integer<uint64_t>(); reader.Bytes(value.cpuGeometrySha256);
        Require(value.cpuGeometryBytes >= 24 && value.cpuGeometryBytes <= assets::cpu_geometry::MaxBytes &&
                value.cpuGeometrySha256 != Sha256Digest{}, "Exact CPU geometry locator");
        value.requiredBoneCount = reader.Integer<uint32_t>(); reader.Bytes(value.requiredSkinBindingSha256);
        const bool skin = Has(value.attributes, VertexAttribute::BoneIndices);
        Require(skin ? value.requiredBoneCount && value.requiredBoneCount <= kModelSubAssetMaxBones && value.requiredSkinBindingSha256 != Sha256Digest{}
                     : !value.requiredBoneCount && value.requiredSkinBindingSha256 == Sha256Digest{}, "Streaming skin identity");
        GeometryMeshlets(reader, value.meshlets); GeometryLods(reader, value.lods);
        const auto levels = reader.Count(kMeshLodMaxLevels + 1, 16);
        Require(levels == value.lods.levels.size() + 1, "Streaming LOD count");
        for (uint32_t level = 0; level < levels; ++level)
            value.counts.push_back({reader.Integer<uint32_t>(), reader.Integer<uint32_t>(), reader.Integer<uint32_t>(), reader.Integer<uint32_t>()});
        auto archive = std::make_shared<assets::cluster::Archive>(); archive->mask = value.attributes; archive->sourceVertices = value.vertexCount;
        archive->pagedTopology = true; archive->grid.positionError = reader.Float();
        Require(archive->grid.positionError >= 0, "Streaming quantization error");
        const auto pages = reader.Count(kModelSubAssetMaxBytes / 44, 44); archive->packets.resize(pages);
        uint64_t cursor = prefixBytes;
        const auto pageStart = cursor;
        for (uint32_t page = 0; page < pages; ++page)
        {
            const auto offset = reader.Integer<uint64_t>(); const auto size = reader.Integer<uint32_t>();
            Require(offset == cursor - pageStart && size >= 64 && size <= totalBytes - cursor, "Streaming page range/order");
            archive->fileOffsets.push_back(cursor); archive->packetSizes.push_back(size); cursor += size;
            Sha256Digest hash{}; reader.Bytes(hash); Require(hash != Sha256Digest{}, "Streaming page digest"); value.pageHashes.push_back(hash);
        }
        Require(cursor == totalBytes, "Streaming trailing/missing page bytes"); reader.End();
        value.archive = std::move(archive); value.metadataBytes = prefix.size(); ValidateStreamDescriptors(value); out = std::move(value);
    }, failure);
}

bool WriteModelGeometryArtifact(const ModelGeometryArtifact& value, std::vector<std::byte>& out, std::string& failure)
{
    return Checked([&] {
        GeometryBase(value);
        std::string diagnostic;
        Require(importer::ValidateMeshlets(value.mesh, value.mesh.meshlets, diagnostic), diagnostic.c_str());
        Require(importer::ValidateMeshLods(value.mesh, value.mesh.coarseLods, diagnostic), diagnostic.c_str());
        const auto attributes = GeometryClusterArchive(value.mesh);
        const auto pages = GeometryTopologyPages(value, *attributes);
        const auto metadata = GeometryStreamMetadata(value, *attributes, pages);
        WireWriter wire; wire.Header(kGeometryMagic, kModelGeometryArtifactVersion); GeometrySection(wire, metadata);
        for (const auto& page : pages) wire.Bytes(page);
        out = std::move(wire.bytes);
    }, failure);
}

bool ExpandModelGeometryPages(std::span<const std::byte> bytes, ModelGeometryArtifact& out, std::string& failure, std::vector<std::string>* warnings)
{
    return Checked([&] {
        uint32_t prefix{};
        Require(bytes.size() >= 12 && ModelGeometryStreamPrefixSize(bytes.first(12), bytes.size(), prefix, failure), "Unsupported geometry envelope; recook required");
        ModelGeometryStreamMetadata metadata;
        Require(ReadModelGeometryStreamMetadata(bytes.first(prefix), bytes.size(), metadata, failure), "Invalid geometry metadata");
        ModelGeometryArtifact value;
        value.mesh.bounds = metadata.bounds; value.mesh.meshlets = metadata.meshlets; value.mesh.coarseLods = metadata.lods;
        value.requiredBoneCount = metadata.requiredBoneCount; value.requiredSkinBindingSha256 = metadata.requiredSkinBindingSha256;
        std::vector<std::byte> vertices(uint64_t(metadata.vertexCount) * metadata.stride);
        std::vector<bool> seen(metadata.vertexCount);
        auto archive = std::make_shared<assets::cluster::Archive>(*metadata.archive);
        archive->packets.clear();
        archive->pagedTopology = false; // Explicit CPU expansion uses QMC1 attribute packets.
        uint32_t page{};
        const auto level = [&](MeshletPayload& topology, std::vector<uint32_t>& indices, uint32_t ordinal) {
            const auto& counts = metadata.counts.at(ordinal);
            indices.resize(counts.indexCount); std::vector<bool> primitives(counts.primitiveRemapCount);
            for (const auto& descriptor : topology.descriptors)
            {
                const auto stored = bytes.subspan(archive->fileOffsets.at(page), archive->packetSizes.at(page));
                Sha256Digest digest{};
                Require(ComputeSha256(stored,digest,failure) && digest == metadata.pageHashes.at(page), "Geometry page SHA-256");
                assets::cluster::ValidateTopologyPageAttributes(stored,metadata.attributes,metadata.vertexCount,
                    counts.indexCount / 3,metadata.requiredBoneCount,descriptor,archive->grid.positionError);
                const auto view = assets::cluster::InspectTopologyPage(stored,metadata.vertexCount,counts.indexCount / 3);
                std::vector<std::byte> raw;
                Require(assets::cluster::Decode(view.attributes,raw,failure),failure.c_str());
                for (uint32_t vertex = 0; vertex < view.vertices; ++vertex)
                {
                    const auto source = assets::quantized::Word(stored,view.sourceOffset + vertex * 4);
                    auto* destination = vertices.data() + uint64_t(source) * metadata.stride;
                    const auto* decoded = raw.data() + uint64_t(vertex) * metadata.stride;
                    Require(!seen[source] || std::memcmp(destination,decoded,metadata.stride) == 0, "Inconsistent shared geometry vertex");
                    std::memcpy(destination,decoded,metadata.stride); seen[source] = true;
                    topology.vertexRemap.push_back(source);
                }
                for (uint32_t triangle = 0; triangle < view.triangles; ++triangle)
                {
                    const auto primitive = assets::quantized::Word(stored,view.primitiveOffset + triangle * 4);
                    Require(!primitives[primitive], "Duplicate geometry primitive"); primitives[primitive] = true;
                    topology.primitiveRemap.push_back(primitive);
                    for (uint32_t corner = 0; corner < 3; ++corner)
                    {
                        const auto local = std::to_integer<uint8_t>(stored[view.triangleOffset + triangle * 3 + corner]);
                        topology.triangleIndices.push_back(local);
                        indices[primitive * 3 + corner] = assets::quantized::Word(stored,view.sourceOffset + local * 4);
                    }
                }
                archive->packets.emplace_back(view.attributes.begin(),view.attributes.end());
                archive->fileOffsets[page] += 32;
                archive->packetSizes[page] = static_cast<uint32_t>(view.attributes.size()); ++page;
            }
            Require(std::ranges::all_of(primitives,[](bool present){return present;}), "Missing geometry primitive");
        };
        level(value.mesh.meshlets,value.mesh.indices,0);
        for (uint32_t index = 0; index < value.mesh.coarseLods.levels.size(); ++index)
            level(value.mesh.coarseLods.levels[index].meshlets,value.mesh.coarseLods.levels[index].indices,index + 1);
        Require(value.mesh.vertices.AssignPacked(metadata.attributes,metadata.vertexCount,vertices), "Expanded geometry vertex layout");
        if (warnings) warnings->clear();
        value.mesh.clusterArchive = std::move(archive); out = std::move(value);
    }, failure);
}

bool ModelGeometryStreamMetadataDigest(std::span<const std::byte> bytes, Sha256Digest& digest, std::string& failure)
{
    uint32_t prefix{};
    return bytes.size() >= 12 && ModelGeometryStreamPrefixSize(bytes.first(12), bytes.size(), prefix, failure) &&
           prefix <= bytes.size() && ComputeSha256(bytes.first(prefix), digest, failure);
}

std::string ModelGeometryStreamDirectory(const Sha256Digest& digest)
{
    constexpr char hex[] = "0123456789abcdef"; std::string result = "m";
    for (const auto byte : digest) { result += hex[byte >> 4]; result += hex[byte & 15]; }
    return result;
}
bool ModelGeometryStreamIdentity(std::string_view path, Sha256Digest& digest)
{
    const auto end = path.find_last_of('/');
    if (end == std::string_view::npos || !end || !path.ends_with(".cege")) return false;
    const auto start = path.find_last_of('/', end - 1);
    const auto begin = start == std::string_view::npos ? 0 : start + 1;
    const auto directory = path.substr(begin, end - begin);
    if (directory.size() != 65 || directory.front() != 'm') return false;
    Sha256Digest value{};
    const auto digit = [](char c) -> int { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
    for (uint32_t index = 0; index < value.size(); ++index)
    {
        const auto hi = digit(directory[1 + index * 2]), lo = digit(directory[2 + index * 2]);
        if (hi < 0 || lo < 0) return false;
        value[index] = static_cast<uint8_t>((hi << 4) | lo);
    }
    digest = value; return true;
}

bool LoadModelGeometryStreamMetadata(ResolvedAssetEntry& resolved, std::shared_ptr<const ModelGeometryStreamMetadata>& out,
                                     std::string& failure, std::atomic<std::size_t>* inputStaging)
{
    return Checked([&] {
        Sha256Digest expected{}; uint64_t total{};
        auto cpuSource = resolved.byteSource;
        Require(ModelGeometryStreamIdentity(resolved.blob.artifactPath, expected), "No manifest-bound thin metadata identity");
        Require(CaptureArtifactSource(resolved.byteSource, resolved.blob.artifactPath, failure) &&
                    resolved.byteSource->Size(resolved.blob.artifactPath, total, failure) && total == resolved.blob.byteSize,
                "Cannot capture exact streaming artifact");
        std::array<std::byte, 12> header{}; uint32_t prefix{};
        Require(resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0, header, failure) && ModelGeometryStreamPrefixSize(header, total, prefix, failure),
                "Cannot read streaming header");
        struct StagingObservation
        {
            std::atomic<std::size_t>* counter;
            ~StagingObservation() { if (counter) counter->store(0, std::memory_order_relaxed); }
        } observation{inputStaging};
        if (inputStaging) inputStaging->store(prefix, std::memory_order_relaxed);
        std::vector<std::byte> bytes(prefix); Sha256Digest actual{};
        Require(resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0, bytes, failure) && ComputeSha256(bytes, actual, failure) && actual == expected,
                "Thin metadata SHA-256 differs from trusted manifest locator");
        auto value = std::make_shared<ModelGeometryStreamMetadata>();
        Require(ReadModelGeometryStreamMetadata(bytes, total, *value, failure), "Malformed thin metadata");
        const auto cpuPath = assets::cpu_geometry::Path(value->cpuGeometrySha256);
        // Pin an available loose file without reading its payload. An absent
        // optional CPU artifact never prevents rendering; its request fails closed.
        std::string captureError;
        auto capturedCpu = cpuSource;
        if (CaptureArtifactSource(capturedCpu, cpuPath, captureError)) cpuSource = std::move(capturedCpu);
        else cpuSource.reset(); // An optional missing owner never falls back to a pathname source.
        value->cpuReader = [source = std::move(cpuSource), cpuPath, size = value->cpuGeometryBytes,
                            digest = value->cpuGeometrySha256]() {
            std::string error; uint64_t actualSize{};
            assets::quantized::Require(source && source->Size(cpuPath, actualSize, error) && actualSize == size,
                                      "Exact CPU geometry artifact size");
            std::vector<std::byte> result(static_cast<size_t>(size)); Sha256Digest actual{};
            assets::quantized::Require(source->ReadAt(cpuPath, 0, result, error) && ComputeSha256(result, actual, error) && actual == digest,
                                      "Exact CPU geometry artifact SHA-256");
            return result;
        };
        auto archive = std::make_shared<assets::cluster::Archive>(*value->archive);
        archive->pageReader = [source = resolved.byteSource, path = resolved.blob.artifactPath, offsets = archive->fileOffsets,
            sizes = archive->packetSizes, hashes = value->pageHashes, descriptors = value->flatDescriptors, counts = value->counts,
            groups = value->lods.groups, mask = value->attributes, vertices = value->vertexCount, bones = value->requiredBoneCount,
            positionError = archive->grid.positionError](uint32_t page) {
            assets::quantized::Require(page < offsets.size(), "Streaming page ordinal");
            std::vector<std::byte> packet(sizes[page]); Sha256Digest hash{}; std::string error;
            if (!source->ReadAt(path, offsets[page], packet, error) || !ComputeSha256(packet, hash, error) || hash != hashes[page])
                throw std::runtime_error("Streaming page read/hash failed: " + error);
            const auto& descriptor = descriptors->at(page);
            const auto level = groups.empty() ? 0 : groups.at(descriptor.reserved).lodIndex;
            assets::cluster::ValidateTopologyPageAttributes(packet, mask, vertices, counts.at(level).indexCount / 3, bones, descriptor, positionError);
            return packet;
        };
        value->archive = std::move(archive); out = std::move(value);
    }, failure);
}
