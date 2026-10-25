#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Ownership.h"
#else
import Creator.Core.Ownership;
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Paths.h"
#else
import Creator.Core.Paths;
#endif
#include "DataSystem.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Platform.Windows.h"
#else
import Creator.Platform.Windows;
#endif
#include "Assets/ModelGeometryPayload.h"
#include "Assets/ModelCpuGeometryPayload.h"
#include "Experiment/Cooked/CookedModelSubAssetCodec.h"
#include "Experiment/Import/MeshletBuilder.h"
#include "RHI/IRenderDeviceServices.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace reflgen::generated { void register_RenderEngine(reflgen::registry& target); }
namespace
{
namespace ck = experiment::cooked;
void Check(bool valid, const std::string& message) { if (!valid) throw std::runtime_error(message); }
experiment::AssetId Id(uint8_t seed)
{
    experiment::AssetId value; value.value.data_[0] = seed; value.value.data_[6] = 0x80; value.value.data_[8] = 0x80; return value;
}
struct Source final : ck::ArtifactByteSource
{
    std::vector<std::byte> bytes;
    std::vector<std::byte> cpuBytes;
    std::vector<std::byte> rootBytes;
    std::string cpuPath;
    std::string rootPath{"Derived/Cold/model.cemd"};
    mutable std::atomic<uint64_t> cpuReads{};
    std::atomic<bool> allowCpu{}, corruptCpu{};
    std::vector<std::pair<uint64_t,uint64_t>> allowed;
    mutable std::atomic<uint64_t> reads{}, readBytes{}, bulkReads{}, rejected{};
    std::atomic<bool> allowFull{};
    bool Size(std::string_view path, uint64_t& size, std::string& error) const override
    { size = path == cpuPath ? cpuBytes.size() : path == rootPath ? rootBytes.size() : bytes.size(); error.clear(); return true; }
    bool ReadAt(std::string_view path, uint64_t offset, std::span<std::byte> output, std::string& error) const override
    {
        if (path == rootPath)
        {
            if (offset || output.size() != rootBytes.size()) { error="Root metadata extent"; return false; }
            std::memcpy(output.data(),rootBytes.data(),output.size()); error.clear(); return true;
        }
        if (path == cpuPath)
        {
            if (!allowCpu || offset || output.size() != cpuBytes.size()) { ++rejected; error="Unrequested CPU artifact read"; return false; }
            std::memcpy(output.data(),cpuBytes.data(),output.size());
            if (corruptCpu && !output.empty()) output.back() ^= std::byte{1};
            ++cpuReads; error.clear(); return true;
        }
        if (offset > bytes.size() || output.size() > bytes.size() - offset) { error = "Probe read range"; return false; }
        bool permit = allowFull;
        for (const auto& range : allowed)
            permit |= offset >= range.first && offset <= range.first + range.second && output.size() <= range.first + range.second - offset;
        if (!permit) { ++rejected; error = "Deferred CPU body read was not requested"; return false; }
        if (offset == 0 && output.size() == bytes.size()) ++bulkReads;
        std::memcpy(output.data(), bytes.data() + offset, output.size()); ++reads; readBytes += output.size(); error.clear(); return true;
    }
};
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Expected a fresh private project directory");
        const auto project = std::filesystem::absolute(argv[1]);
        Check(!std::filesystem::exists(project), "Cold DataSystem fixture must own a fresh project");
        std::filesystem::create_directories(project / "Assets");
        // The packaged host requires a source identity table even when this
        // test selects only an independently mounted typed Mesh entry.
        ck::CookedAssetManifest legacy;
        experiment::AssetId unselected;
        Check(uuid::try_parse("00000003-0000-4000-8000-000000000003", unselected.value), "Unselected source identity");
        legacy.sourceAssets.push_back({unselected, "Models/Unselected.glb"});
        std::filesystem::create_directories(project / "Assets/Models");
        { std::ofstream output(project / "Assets/Models/Unselected.glb", std::ios::binary); output << "unselected fixture"; }
        ck::CookedAssetManifestEntry unused;
        unused.assetId = unselected; unused.kind = ck::CookedAssetKind::Model;
        unused.formatVersion = 1; unused.byteSize = 1; unused.contentSha256[0] = 1;
        unused.artifactPath = "Derived/unselected.cemc";
        legacy.entries.push_back(unused);
        const auto legacyBytes = ck::WriteAssetManifest(legacy);
        Check(legacyBytes.Succeeded(), "Packaged metadata-only source catalog");
        std::filesystem::create_directories(project / "Assets/Derived");
        { std::ofstream output(project / "Assets/Derived/asset-manifest.cemf", std::ios::binary);
          output.write(reinterpret_cast<const char*>(legacyBytes.bytes.data()), legacyBytes.bytes.size());
          Check(bool(output), "Packaged source catalog write"); }
        const ce::platform::windows::apartment_scope apartmentScope{ ce::platform::windows::apartment_model::mta };
        const HRESULT com = apartmentScope.result;
        Check(SUCCEEDED(com), "Probe COM initialization");
        reflgen::generated::register_RenderEngine(Meta::Types());
        auto& scheduler = ce::get_job_scheduler(); scheduler.start(4);
        struct JobRelease { ~JobRelease() { ce::get_job_scheduler().shutdown(); } } jobRelease;
        auto* paths = (&ce::paths::get_state()); paths->data_path = project / "Assets";
        paths->base_project_path = project; paths->asset_authoring_enabled = false;
        auto* data = DataSystem::GetInstance(); data->Initialize();
        struct DataRelease { DataSystem* value; ~DataRelease() { if(value) value->Finalize(); } } dataRelease{data};
        ck::ModelGeometryArtifact authored;
        Check(authored.mesh.vertices.SetLayout(experiment::kCoreVertexAttributes), "Core vertex layout");
        for (const auto position : {math::vector3{0,0,0},math::vector3{1,0,0},math::vector3{0,1,0}})
        {
            experiment::Vertex vertex; vertex.position = position; vertex.normal = {0,0,1}; vertex.tangent = {1,0,0,-1};
            Check(authored.mesh.vertices.Append(vertex), "Authored vertex append");
        }
        authored.mesh.indices = {0,1,2}; authored.mesh.bounds = math::aabb::from_min_max({0,0,0},{1,1,0});
        std::string error;
        Check(experiment::importer::BuildMeshlets(authored.mesh, authored.mesh.meshlets, error), "Meshlet build: " + error);
        auto source = own::make_shared<Source>();
        Check(ck::WriteModelGeometryArtifact(authored, source->bytes, error), "CEGE7 encode: " + error);
        uint32_t prefix{};
        Check(ck::ModelGeometryStreamPrefixSize(std::span(source->bytes).first(12), source->bytes.size(), prefix, error), "Thin prefix extent");
        ck::ModelGeometryStreamMetadata table;
        Check(ck::ReadModelGeometryStreamMetadata(std::span(source->bytes).first(prefix), source->bytes.size(), table, error), "Thin table parse: " + error);
        source->cpuBytes = assets::cpu_geometry::Encode(authored.mesh.vertices.AttributeMask(),static_cast<uint32_t>(authored.mesh.vertices.size()),
            authored.mesh.vertices.Bytes(),authored.mesh.indices);
        source->cpuPath = assets::cpu_geometry::Path(table.cpuGeometrySha256);
        source->allowed.push_back({0,prefix});
        for (uint32_t page = 0; page < table.archive->packets.size(); ++page)
            source->allowed.push_back({table.archive->fileOffsets[page], table.archive->packetSizes[page]});
        ck::Sha256Digest metadataHash{};
        Check(ck::ModelGeometryStreamMetadataDigest(source->bytes, metadataHash, error), "Thin metadata digest");
        ck::AssetBlobRecord blob;
        Check(ck::ComputeSha256(source->bytes, blob.contentSha256, error), "Whole blob digest");
        blob.byteSize = source->bytes.size(); blob.kind = ck::CookedAssetKind::Mesh;
        blob.representation = ck::kModelGeometryRepresentation; blob.schemaVersion = ck::kModelGeometryArtifactVersion;
        blob.targetPlatform = "cold-probe"; blob.targetAbi = "cold-probe-v1";
        blob.artifactPath = "Derived/Probe/" + ck::ModelGeometryStreamDirectory(metadataHash) + "/geometry.cege";
        const AssetDepot::AssetLink<assets::ModelMeshDescriptor> link{{Id(1),{}}};
        ck::AssetSetManifest manifest; manifest.assetSetId = Id(2); manifest.revision = 1;
        manifest.targetPlatform = blob.targetPlatform; manifest.targetAbi = blob.targetAbi;
        manifest.blobs.push_back(blob); manifest.entries.push_back({link.ToReference(),0,{}}); manifest.roots.push_back(link.ToReference());
        const AssetDepot::AssetLink<assets::ModelAnimationDescriptor> modelLink{{Id(3),{}}};
        ck::ModelDescriptorArtifact model;
        model.modelAssetId = Id(3); model.name = "ExactCpuConsumer"; model.createMeshCollider = true;
        model.meshes.push_back({Id(1),{},"Triangle",authored.mesh.bounds,authored.mesh.vertices.AttributeMask(),authored.mesh.vertices.Stride(),3,3,false});
        model.nodes.push_back({"Root",{},math::matrix4x4::identity(),{Id(1)}});
        Check(ck::WriteModelDescriptorArtifact(model,source->rootBytes,error),"Model root metadata");
        auto rootBlob = blob; rootBlob.kind = ck::CookedAssetKind::Model; rootBlob.representation = ck::kModelDescriptorRepresentation;
        rootBlob.schemaVersion = ck::kModelDescriptorVersion; rootBlob.byteSize = source->rootBytes.size(); rootBlob.artifactPath = source->rootPath;
        Check(ck::ComputeSha256(source->rootBytes,rootBlob.contentSha256,error),"Model root digest");
        manifest.blobs.push_back(rootBlob);
        manifest.entries.push_back({modelLink.ToReference(),1,{{link.ToReference(),ck::AssetDependencyKind::Loadable,ck::AssetDependencyScope::Internal}}});
        manifest.roots.push_back(modelLink.ToReference());
        const auto encoded = ck::WriteAssetSetManifest(manifest); Check(encoded.Succeeded(), "Manifest encoding");
        std::vector<ck::AssetManifestIssue> issues;
        ck::AssetSetMountOptions options; options.expectedTargetPlatform = blob.targetPlatform; options.expectedTargetAbi = blob.targetAbi;
        const auto mount = data->MountAssetSet(encoded.bytes, source, options, issues);
        Check(mount.IsValid() && issues.empty() && source->reads == 0, "Metadata-only mount performs no geometry read");
        const auto wait = [](const auto& request) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            auto result = request.Snapshot();
            while (result.status == AssetDepot::AssetRequestStatus::Pending && std::chrono::steady_clock::now() < deadline)
            { std::this_thread::sleep_for(std::chrono::milliseconds(1)); result = request.Snapshot(); }
            return result;
        };
        const auto descriptionRequest = data->RequestAsync(link);
        const auto description = wait(descriptionRequest);
        Check(description.status == AssetDepot::AssetRequestStatus::Ready && description.asset && description.asset->streamMetadata,
              "Actual descriptor request failed: " + description.message);
        Check(source->reads == 2 && source->readBytes == uint64_t(prefix) + 12 && !source->bulkReads && !source->rejected,
              "Descriptor admission read more than header plus thin metadata");
        Check(!data->TryAcquire(description.asset) && source->reads == 2, "Descriptor admission creates no raw geometry cache result or read");
        Check(source->cpuReads == 0,"Descriptor/render admission must not read the CPU artifact");
        source->allowCpu = true;
        const auto cpu = assets::LoadModelCpuGeometry(*description.asset,error);
        Check(cpu && cpu->Matches(*description.asset) && source->cpuReads == 1 && !source->bulkReads &&
            cpu->geometry.indices == authored.mesh.indices && cpu->geometry.positions[1] == math::vector3{1,0,0},
            "Exact CPU consumer reads only the dedicated position/index artifact");
        source->corruptCpu = true;
        Check(!assets::LoadModelCpuGeometry(*description.asset,error) && !source->bulkReads,"CPU integrity failure must not fall back to the full body");
        source->corruptCpu = false;
        std::cout << "CPU_GEOMETRY_CONSUMER_OK artifactReads=1 rawReads=0 corruptionRejected=1 exactPositions=1 exactIndices=1\n";
        const auto waitPreparation = [&](const DataSystem::ModelPreparation& preparation) {
            Check(preparation && data->HasPreparedModelScene(preparation),"Actual typed model preparation");
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
            while (!data->ModelPreparationCompletion(preparation).is_complete() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            assets::ModelSceneAssetInputs result;
            Check(data->ReadPreparedModelScene(preparation,result,error),"Actual prepared model scene: " + error);
            return result;
        };
        const auto beforeDisabled = source->cpuReads.load();
        const auto disabledTicket = data->PrepareModelAsset(modelLink,assets::ModelColliderPreparationPolicy::Disabled);
        const auto disabledScene = waitPreparation(disabledTicket);
        Check(!disabledScene.createMeshCollider && disabledScene.geometry.empty() && source->cpuReads == beforeDisabled && !source->bulkReads,
              "Disabled collider must not request CPU data");
        const auto enabledTicket = data->PrepareModelAsset(modelLink,assets::ModelColliderPreparationPolicy::Enabled);
        const auto enabledScene = waitPreparation(enabledTicket);
        Check(enabledScene.createMeshCollider && enabledScene.geometry.size() == 1 && enabledScene.geometry[0] &&
            enabledScene.geometry[0]->Matches(*description.asset) && source->cpuReads == beforeDisabled + 1 && !source->bulkReads &&
            !data->TryAcquire(description.asset),"Collider preparation must consume only the dedicated CPU artifact and leave raw cache empty");
        std::cout << "CPU_COLLIDER_PREPARATION_OK disabledReads=0 enabledReads=1 rawReads=0 rawCache=0\n";
        RHIModelMeshView view;
        Check(BuildRHIModelMeshView(*description.asset, view) && view.IsSceneComplete() && !view.IsComplete() && !view.vertexData && !view.indexData,
              "Actual DataSystem descriptor cannot form an independently complete streamed view");
        source->allowFull = true;
        const auto rawRequest = data->RequestAsync(description.asset); const auto raw = wait(rawRequest);
        Check(raw.status == AssetDepot::AssetRequestStatus::Ready && raw.asset && raw.asset->Matches(*description.asset),
              "Explicit compressed-page expansion request failed: " + raw.message);
        Check(source->bulkReads == 1 && !source->rejected && raw.asset->mesh.vertexBytes.size() == authored.mesh.vertices.Bytes().size() &&
                  raw.asset->mesh.indices == authored.mesh.indices && BuildRHIModelMeshView(*description.asset, *raw.asset, view) && view.IsComplete(),
              "Explicit expansion did not preserve topology and identity");
        Check(data->UnmountAssetSet(mount, issues), "Unmount retains exact externally held owners");
        data->Finalize(); dataRelease.value = nullptr;
        Check(!description.asset->streamMetadata->archive->Read(0).empty() && raw.asset->Matches(*description.asset),
              "Retained metadata/page and raw owners failed after unmount/finalize");
        std::cout << "COLD_GEOMETRY_DATASYSTEM_OK metadataReads=2 initialBulkReads=0 rawCache=0 explicitBulkReads=1 exactCpu=1 retainedOwners=1\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "COLD_GEOMETRY_DATASYSTEM_FAILED " << error.what() << '\n'; return 1; }
}
