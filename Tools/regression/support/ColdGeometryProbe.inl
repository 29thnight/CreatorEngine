struct CountedClusterBytes final : experiment::cooked::ArtifactByteSource
{
    ce::dynamic_array<std::byte> bytes;
    std::vector<std::pair<uint64_t, uint64_t>> allowed;
    mutable std::atomic<uint64_t> readBytes{}, reads{}, rejectedBulk{};
    std::atomic<bool> corruptPage{};
    uint64_t prefixBytes{};
    bool Size(std::string_view, uint64_t& result, std::string& error) const override
    { result = bytes.size(); error.clear(); return true; }
    bool ReadAt(std::string_view, uint64_t offset, std::span<std::byte> output, std::string& error) const override
    {
        if (offset > bytes.size() || output.size() > bytes.size() - offset)
        { error = "Counted cluster source range"; return false; }
        bool permitted = false;
        for (const auto& range : allowed)
            permitted |= offset >= range.first && offset <= range.first + range.second &&
                         output.size() <= range.first + range.second - offset;
        if (!permitted)
        { ++rejectedBulk; error = "Cold fixture forbids reading the deferred CPU body"; return false; }
        std::memcpy(output.data(), bytes.data() + offset, output.size());
        if (corruptPage && offset >= prefixBytes && !output.empty()) output.back() ^= std::byte{1};
        readBytes += output.size();
        ++reads;
        error.clear();
        return true;
    }
};

struct ColdGeometryFixture
{
    own::shared_owner<assets::ModelMeshDescriptor> descriptor;
    own::shared_owner<CountedClusterBytes> backing;
    RHIModelMeshView view;
};

ColdGeometryFixture MakeColdGeometryFixture(const experiment::cooked::ModelGeometryArtifact& artifact,
                                           const assets::ModelMeshDescriptor& previous)
{
    namespace ck = experiment::cooked;
    ColdGeometryFixture result;
    result.backing = own::make_shared<CountedClusterBytes>();
    auto& source = *result.backing;
    std::string error;
    Check(ck::WriteModelGeometryArtifact(artifact, source.bytes, error), "Cold geometry cook " + error);
    uint32_t prefix{};
    Check(ck::ModelGeometryStreamPrefixSize(std::span(source.bytes).first(12), source.bytes.size(), prefix, error), "Cold prefix size");
    ck::ModelGeometryStreamMetadata table;
    Check(ck::ReadModelGeometryStreamMetadata(std::span(source.bytes).first(prefix), source.bytes.size(), table, error), "Cold page table " + error);
    Check(!table.archive->fileOffsets.empty() && table.archive->fileOffsets.front() == prefix,
          "Current geometry contains no deferred body between metadata and pages");
    uint64_t end = prefix;
    for (uint32_t page = 0; page < table.archive->packetSizes.size(); ++page)
    {
        Check(table.archive->fileOffsets[page] == end,"Contiguous page-only geometry storage");
        end += table.archive->packetSizes[page];
    }
    Check(end == source.bytes.size(),"Geometry artifact is exactly metadata plus pages");
    for (uint32_t version : {6u,7u,8u})
    {
        auto old = source.bytes; std::memcpy(old.data() + 4,&version,4);
        ck::ModelGeometryArtifact rejected;
        Check(!ck::ExpandModelGeometryPages(old,rejected,error),"Legacy geometry reader must not exist");
    }
    std::cout << "METEOR_GEOMETRY_PAGE_ONLY_OK schema=9 bodyBytes=0 oldSchemasRejected=3\n";
    source.prefixBytes = prefix;
    source.allowed.push_back({0, prefix});
    for (uint32_t page = 0; page < table.archive->packets.size(); ++page)
        source.allowed.push_back({table.archive->fileOffsets[page], table.archive->packetSizes[page]});
    ck::Sha256Digest metadataHash{}, contentHash{};
    Check(ck::ModelGeometryStreamMetadataDigest(source.bytes, metadataHash, error) &&
              ck::ComputeSha256(source.bytes, contentHash, error), "Cold independent integrity roots");
    auto descriptor = own::make_shared<assets::ModelMeshDescriptor>(previous);
    descriptor->origin.byteSource = result.backing;
    descriptor->origin.blob.artifactPath = "Derived/Cold/" + ck::ModelGeometryStreamDirectory(metadataHash) + "/geometry.cege";
    descriptor->origin.blob.byteSize = source.bytes.size();
    descriptor->origin.blob.contentSha256 = contentHash;
    descriptor->origin.blob.schemaVersion = ck::kModelGeometryArtifactVersion;
    descriptor->geometryKey.byteSize = source.bytes.size();
    descriptor->geometryKey.contentSha256 = contentHash;
    descriptor->geometryKey.schemaVersion = ck::kModelGeometryArtifactVersion;
    descriptor->bounds = artifact.mesh.bounds;
    Check(ck::LoadModelGeometryStreamMetadata(descriptor->origin, descriptor->streamMetadata, error), "Cold authenticated metadata load " + error);
    Check(source.reads == 2 && source.readBytes == uint64_t(prefix) + 12 && !source.rejectedBulk,
          "Cold load reads exactly header plus metadata and no CPU body/page");
    Check(std::ranges::all_of(descriptor->streamMetadata->archive->packets, [](const auto& packet) { return packet.empty(); }),
          "Cold metadata owns no compressed page bytes");
    Check(descriptor->streamMetadata->meshlets.vertexRemap.empty() && descriptor->streamMetadata->meshlets.triangleIndices.empty() &&
              descriptor->streamMetadata->meshlets.primitiveRemap.empty() &&
              std::ranges::all_of(descriptor->streamMetadata->lods.levels, [](const auto& level) {
                  return level.indices.empty() && level.meshlets.vertexRemap.empty() && level.meshlets.triangleIndices.empty() &&
                         level.meshlets.primitiveRemap.empty();
              }), "Thin prefix owns no index or meshlet connectivity arrays");
    {
        const auto cpuBytes = assets::cpu_geometry::Encode(artifact.mesh.vertices.AttributeMask(),static_cast<uint32_t>(artifact.mesh.vertices.size()),
            artifact.mesh.vertices.Bytes(),artifact.mesh.indices);
        const auto cpuPath = assets::cpu_geometry::Path(table.cpuGeometrySha256);
        const auto directory = std::filesystem::current_path() / "Artifacts" / "sparse-streaming-20261016" / "BackingFixtures" /
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        Check(!std::filesystem::exists(directory), "Cold backing fixture owns a fresh directory");
        const auto file = directory / descriptor->origin.blob.artifactPath;
        std::filesystem::create_directories(file.parent_path());
        { std::ofstream output(file, std::ios::binary); output.write(reinterpret_cast<const char*>(source.bytes.data()), source.bytes.size());
          Check(bool(output), "Cold loose fixture write"); }
        std::filesystem::create_directories((directory / cpuPath).parent_path());
        { std::ofstream output(directory / cpuPath,std::ios::binary); output.write(reinterpret_cast<const char*>(cpuBytes.data()),cpuBytes.size());
          Check(bool(output),"Exact CPU loose artifact write"); }
        const auto rootPage = table.lods.groups.empty() ? 0u : table.lods.groups.back().firstMeshlet;
        const auto expectedPage = std::span<const std::byte>(source.bytes).subspan(table.archive->fileOffsets[rootPage], table.archive->packetSizes[rootPage]);
        auto loose = descriptor->origin;
        loose.byteSource = own::make_shared<ck::LooseArtifactByteSource>(directory);
        own::shared_owner<const ck::ModelGeometryStreamMetadata> looseMetadata;
        Check(ck::LoadModelGeometryStreamMetadata(loose, looseMetadata, error), "Actual loose thin metadata load " + error);
        loose.byteSource.reset();
        Check(std::ranges::equal(looseMetadata->archive->Read(rootPage), expectedPage), "Captured loose page survives root owner release");
        Check(looseMetadata->cpuReader() == cpuBytes,"Dedicated loose CPU artifact survives root owner release");
        Pak::BuildOptions options; options.chunkSize = 64 * 1024; options.encrypt = true; options.compress = true;
        const auto pakFile = directory / "geometry.pak";
        Pak::Builder builder(pakFile, options);
        builder.addMemory("Assets/" + descriptor->origin.blob.artifactPath, source.bytes);
        builder.addMemory("Assets/" + cpuPath,cpuBytes); builder.finish();
        auto pak = own::make_shared<const Pak::Archive>(pakFile, Pak::OpenOptions{.key = builder.key()});
        auto packed = descriptor->origin; packed.byteSource = own::make_shared<ck::PakAudioClipByteSource>(pak);
        own::shared_owner<const ck::ModelGeometryStreamMetadata> pakMetadata;
        Check(ck::LoadModelGeometryStreamMetadata(packed, pakMetadata, error), "Encrypted/compressed pak thin metadata load " + error);
        packed.byteSource.reset(); pak.reset();
        Check(std::ranges::equal(pakMetadata->archive->Read(rootPage), expectedPage), "Captured pak page survives archive/root owner release");
        Check(pakMetadata->cpuReader() == cpuBytes,"Dedicated pak CPU artifact survives archive/root owner release");
        std::cout << "METEOR_COLD_BACKING_OK loose=1 pak=1 capturedLifetime=1 pageHashes=1\n";
    }
    auto damaged = own::make_shared<CountedClusterBytes>();
    damaged->bytes = source.bytes; damaged->allowed = source.allowed;
    damaged->bytes[prefix - 1] ^= std::byte{1};
    auto bad = descriptor->origin; bad.byteSource = damaged;
    own::shared_owner<const ck::ModelGeometryStreamMetadata> rejected;
    Check(!ck::LoadModelGeometryStreamMetadata(bad, rejected, error) && !rejected && !damaged->rejectedBulk,
          "Corrupt metadata rejects before deferred geometry access");
    source.corruptPage = true;
    bool pageRejected{};
    try { descriptor->streamMetadata->archive->Read(0); } catch (const std::exception&) { pageRejected = true; }
    source.corruptPage = false;
    Check(pageRejected && !source.rejectedBulk, "Corrupt deferred page fails its authenticated hash");
    ck::ModelGeometryArtifact cpu;
    Check(ck::ExpandModelGeometryPages(source.bytes, cpu, error) &&
              cpu.mesh.vertices.Bytes().size() == artifact.mesh.vertices.Bytes().size() && cpu.mesh.indices == artifact.mesh.indices,
          "Current compressed-page expansion preserves vertex extent and connectivity " + error);
    Check(BuildRHIModelMeshView(*descriptor, result.view) && result.view.IsSceneComplete() &&
              !result.view.IsComplete() && !result.view.vertexData && !result.view.sourcePayload,
          "Cold render view owns authenticated topology and no raw geometry payload");
    result.descriptor = std::move(descriptor);
    std::cout << "METEOR_COLD_METADATA_OK metadataBytes=" << prefix << " totalBytes=" << source.bytes.size()
              << " bulkReads=0 corruption=2 exactCpu=1\n";
    return result;
}
