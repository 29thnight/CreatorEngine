#include "Experiment/Import/ImporterModelDecoder.h"
#include "../../Engine/Utility_Framework/WindowsApartment.h"
#include "Assets/ModelAssetAuthoringTransaction.h"
#include "Assets/ModelAssetGeneration.h"
#include "Assets/ModelGenerationStreaming.h"
#include "Assets/ModelCpuGeometryPayload.h"
#include "RHI/IRenderDeviceServices.h"
#include "Render/Scene/EnhancedDrawReplayInput.h"
#include "../../Editor/EngineEntry/TextureSourceProcessing.h"
#include "PathFinder.h"
#include "../../Engine/Utility_Framework/StandardLibrary.h"
#include <objbase.h>

namespace
{
void Check(bool condition, const std::string& message) { if (!condition) throw std::runtime_error(message); }
void Verify(const assets::ModelAssetGeneration& generation)
{
    Check(!generation.Meshes().empty(),"Authoring stream must retain mesh descriptions");
    for (uint32_t index = 0; index < generation.Meshes().size(); ++index)
    {
        const auto& mesh = generation.Meshes()[index];
        Check(mesh.streamDescriptor && mesh.streamDescriptor->streamMetadata && mesh.vertexBytes.empty() && mesh.indices.empty() &&
            mesh.meshlets.vertexRemap.empty() && mesh.coarseLods.IsEmpty(),"Runtime generation retained CPU geometry");
        const auto& metadata = *mesh.streamDescriptor->streamMetadata;
        uint64_t end = metadata.metadataBytes;
        for (uint32_t page = 0; page < metadata.archive->packetSizes.size(); ++page)
        {
            Check(metadata.archive->fileOffsets[page] == end,"Authoring geometry retained a full body or gaps");
            end += metadata.archive->packetSizes[page];
        }
        Check(end == mesh.streamDescriptor->origin.blob.byteSize,"Authoring geometry must contain metadata and pages only");
        RHIModelMeshView view;
        Check(BuildRHIModelMeshView(generation,index,view) && view.IsSceneComplete() && !view.IsComplete() &&
            !view.vertexData && !view.indexData && !view.sourceGeneration && view.StreamMetadata(),"Authoring stream view provenance");
        Check(EnhancedDrawReplayInput::GeometryDigest(view) != 0,"Metadata-only replay digest must not dereference CPU geometry");
    }
}
}
int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3 || argc == 4,"Expected source fixture project, fresh output project and optional model filename");
        const auto source = std::filesystem::absolute(argv[1]), project = std::filesystem::absolute(argv[2]);
        Check(!std::filesystem::exists(project),"Consumer probe requires a fresh private project");
        std::filesystem::create_directories(project);
        std::filesystem::copy(source / "Assets",project / "Assets",std::filesystem::copy_options::recursive);
        std::filesystem::copy(source / "ProjectSetting",project / "ProjectSetting",std::filesystem::copy_options::recursive);
        const ce::platform::windows::apartment_scope apartmentScope{ ce::platform::windows::apartment_model::mta };
        const HRESULT com = apartmentScope.result; Check(SUCCEEDED(com),"COM initialization");
        auto* paths = InternalPath::GetInstance(); paths->AssetAuthoringEnabled = true;
        paths->BaseProjectPath = project; paths->DataPath = project / "Assets";
        Authoring::InstallTextureSourceProcessing();
        struct Sources { ~Sources(){Authoring::UninstallTextureSourceProcessing();} } sourceProcessing;
        assets::ModelAssetAuthoringRequest request;
        request.assetRoot = project / "Assets"; request.sourcePath = request.assetRoot / "Models" / (argc == 4 ? argv[3] : "Prim_Suzanne.glb");
        request.identityHeaderPath = project / "ProjectSetting/AssetIdentity.asset";
        request.generationRoot = project / "Library/ModelAssetGenerations";
        const auto authored = assets::AuthorModelAsset(request);
        std::string errors;
        for (const auto& issue : authored.issues) errors += issue.stage + ": " + issue.message + "; ";
        Check(authored.Succeeded(),"Real authoring publication failed: " + errors);
        Check(!std::filesystem::exists(authored.generationPath / "model.cemc"),"Published generation duplicated full geometry");
        experiment::importer::ImporterDecoderOptions options;
        options.resolveModelAsset = [&](const auto&){return experiment::AssetId{authored.modelAssetId};};
        experiment::importer::ImporterModelDecoder decoder(options);
        experiment::ModelLoadRequest referenceRequest; referenceRequest.sourcePath = request.sourcePath;
        auto imported = decoder.Decode(referenceRequest); Check(imported.draft.has_value(),"Independent source reference import");
        auto reference = std::move(*imported.draft);
        assets::ModelAssetGenerationLoadRequest load;
        load.identityHeaderPath = request.identityHeaderPath; load.generationRoot = request.generationRoot;
        load.generationPath = authored.generationPath; load.canonicalSidecarPath = authored.sidecarPath;
        load.expectedModelId = authored.modelAssetId; load.allowSourceTextureProcessing = true;
        const auto accepted = assets::LoadModelAssetGeneration(load);
        errors.clear(); for (const auto& issue : accepted.issues) errors += issue.context + ": " + issue.message + "; ";
        Check(accepted.Succeeded(),"Runtime metadata-only generation load: " + errors); Verify(*accepted.generation);
        const auto skinnedMeshes = std::ranges::count_if(accepted.generation->Meshes(),[](const auto& mesh){return mesh.streamDescriptor->skinned;});
        if (argc == 4) Check(skinnedMeshes > 0 && accepted.generation->Skeleton() && !accepted.generation->Skeleton()->bones.empty(),
                            "Skinned authoring fixture lost its skeleton or skin layout");
        Check(reference.meshes.size() == accepted.generation->Meshes().size(),"Source mesh count");
        size_t vertices{}, indices{};
        for (size_t index = 0; index < reference.meshes.size(); ++index)
        {
            const auto& original = reference.meshes[index]; const auto& descriptor = accepted.generation->Meshes()[index].streamDescriptor;
            std::string error; const auto cpu = assets::LoadModelCpuGeometry(*descriptor,error);
            Check(cpu && cpu->Matches(*descriptor),"Dedicated exact CPU request: " + error);
            Check(cpu->geometry.indices == original.indices && cpu->geometry.positions.size() == original.vertices.size(),"Exact CPU connectivity/count");
            const auto offset = assets::OffsetOf(original.vertices.AttributeMask(),assets::VertexAttribute::Position);
            for (size_t vertex = 0; vertex < cpu->geometry.positions.size(); ++vertex)
                Check(std::memcmp(&cpu->geometry.positions[vertex],original.vertices.Bytes().data() + vertex * original.vertices.Stride() + offset,12) == 0,
                    "CPU collision input lost original position bits");
            vertices += cpu->geometry.positions.size(); indices += cpu->geometry.indices.size();
        }
        const auto& first = accepted.generation->Meshes().front().streamDescriptor;
        const auto missingCpu = project / "MissingCpuGeneration";
        std::filesystem::create_directories(missingCpu);
        for (const auto& entry : std::filesystem::recursive_directory_iterator(authored.generationPath))
        {
            if (!entry.is_regular_file() || entry.path().extension() == ".cecpu") continue;
            const auto destination = missingCpu / std::filesystem::relative(entry.path(),authored.generationPath);
            std::filesystem::create_directories(destination.parent_path()); std::filesystem::copy_file(entry.path(),destination);
        }
        load.generationPath = missingCpu;
        const auto withoutCpu = assets::LoadModelAssetGeneration(load);
        Check(withoutCpu.Succeeded(),"Absent optional CPU artifact must not block render metadata"); Verify(*withoutCpu.generation);
        std::string error;
        Check(!assets::LoadModelCpuGeometry(*withoutCpu.generation->Meshes().front().streamDescriptor,error),"Missing CPU data must fail without full-body fallback");
        Check(assets::LoadModelCpuGeometry(*first,error) != nullptr,"Original captured CPU owner was invalidated");
        Check(!first->streamMetadata->archive->Read(0).empty(),"Retained authoring page source");
        const auto oldRecord = project / "OldRecordGeneration";
        std::filesystem::copy(missingCpu,oldRecord,std::filesystem::copy_options::recursive);
        auto oldBytes = assets::generation_streaming::Read(oldRecord / "generation.asset",16ull << 20);
        std::string oldText(reinterpret_cast<const char*>(oldBytes.data()),oldBytes.size());
        const auto version = oldText.find("schemaVersion: 2"); Check(version != std::string::npos,"Current generation record schema");
        oldText.replace(version,16,"schemaVersion: 1");
        {std::ofstream file(oldRecord / "generation.asset",std::ios::binary);file << oldText;}
        load.generationPath = oldRecord; Check(!assets::LoadModelAssetGeneration(load).Succeeded(),"Old generation reader must be retired");
        const auto reimported = assets::AuthorModelAsset(request);
        errors.clear(); for (const auto& issue : reimported.issues) errors += issue.stage + ": " + issue.message + "; ";
        Check(reimported.Succeeded() && reimported.modelAssetId == authored.modelAssetId && reimported.generation > authored.generation,
              "Reimport must retain IDs and publish new immutable sources: " + errors);
        load.generationPath = reimported.generationPath;
        const auto latest = assets::LoadModelAssetGeneration(load); Check(latest.Succeeded(),"Reimport metadata load"); Verify(*latest.generation);
        Check(assets::LoadModelCpuGeometry(*first,error) != nullptr && !first->streamMetadata->archive->Read(0).empty(),"Reimport invalidated retained old sources");
        std::cout << "GEOMETRY_CONSUMER_CUTOVER_OK meshes=" << reference.meshes.size() << " vertices=" << vertices << " indices=" << indices
                  << " skinnedMeshes=" << skinnedMeshes << " metadataOnly=1 fullBodyAbsent=1 cpuExact=1 optionalCpu=1 retainedSources=1 reimport=1 replay=1 oldRecordRejected=1\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "GEOMETRY_CONSUMER_CUTOVER_FAILED " << error.what() << '\n'; return 1; }
}
