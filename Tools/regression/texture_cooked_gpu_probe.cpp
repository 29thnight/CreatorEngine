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
#include "Experiment/Cooked/ArtifactByteSource.h"
#include "Experiment/Cooked/CookedTexture.h"
#include "Experiment/MaterialResolver.h"
#include "AssetDepot/MaterialAssetRuntime.h"
#ifdef CECT_PROBE_VULKAN
#include "RHI/Vulkan/VulkanDeviceResources.h"
#include "RHI/Vulkan/VulkanCommandBufferPool.h"
#include "RHI/Vulkan/VulkanPipelineCache.h"
#else
#include "RHI/DX12/DX12DeviceResources.h"
#include "RHI/DX12/DX12CommandListPool.h"
#include "RHI/DX12/DX12RootSignatureCache.h"
#include "RHI/DX12/DX12PSOManager.h"
#include "RHI/DX12/DX12TextureCache.h"
#endif
#include "RHI/RHIShaderCompiler.h"
#include "RHI/RHISubmissionThread.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    namespace Cooked = experiment::cooked;
#ifdef CECT_PROBE_VULKAN
    using ProbeDevice = VulkanDeviceResources;
    using ProbeTextures = VulkanTextureCache;
    using ProbePool = VulkanCommandBufferPool;
    constexpr auto kBackend = "vulkan";
    constexpr auto kShaderBinary = RHIShaderBinary::SpirV;
    constexpr auto kUploadedState = RHIResourceState::ShaderResource;
#else
    using ProbeDevice = DX12DeviceResources;
    using ProbeTextures = DX12TextureCache;
    using ProbePool = DX12CommandListPool;
    constexpr auto kBackend = "dx12";
    constexpr auto kShaderBinary = RHIShaderBinary::Dxil;
    constexpr auto kUploadedState = RHIResourceState::PixelShaderResource;
#endif

    void Check(bool value, const std::string& message)
    {
        if (!value)
        {
            throw std::runtime_error(message);
        }
    }

    void VerifyImageIdentity()
    {
        Cooked::AssetBlobRecord blob;
        blob.byteSize = 34u;
        blob.kind = Cooked::CookedAssetKind::Texture;
        blob.representation = Cooked::kCookedTextureRepresentationVersion;
        blob.schemaVersion = Cooked::kCookedTextureSchemaVersion;
        blob.targetPlatform = "probe";
        blob.targetAbi = "probe";
        const auto key = AssetDepot::MakeTextureImageKey(blob, {});
        unsigned variants{};
        for (const auto color : { AssetDepot::TextureAssetColorSpace::Source,
            AssetDepot::TextureAssetColorSpace::Linear, AssetDepot::TextureAssetColorSpace::Srgb })
        {
            for (const bool compress : { false, true })
            {
                for (const bool rgba : { false, true })
                {
                    for (const auto mip : { AssetDepot::TextureMipPolicy::PreserveAuthored,
                        AssetDepot::TextureMipPolicy::GenerateFull })
                    {
                        for (const auto role : { 0u, 99u })
                        {
                            const AssetDepot::TextureAssetVariant variant{ color, compress, role, mip, rgba };
                            Check(key == AssetDepot::MakeTextureImageKey(blob, variant),
                                "Descriptor variant split identical cooked image bytes");
                            ++variants;
                        }
                    }
                }
            }
        }
        auto changed = blob;
        changed.artifactPath = "Derived/another.cetex";
        Check(key == AssetDepot::MakeTextureImageKey(changed, {}), "Path entered image identity");
        const auto checkPartition = [&](const auto& mutate)
        {
            auto other = blob;
            mutate(other);
            Check(key != AssetDepot::MakeTextureImageKey(other, {}), "Cooked image identity lost a partition");
        };
        checkPartition([](auto& other) { other.contentSha256[0] ^= 1u; });
        checkPartition([](auto& other) { ++other.byteSize; });
        checkPartition([](auto& other) { other.kind = Cooked::CookedAssetKind::Material; });
        checkPartition([](auto& other) { ++other.representation; });
        checkPartition([](auto& other) { ++other.schemaVersion; });
        checkPartition([](auto& other) { other.targetPlatform += "-other"; });
        checkPartition([](auto& other) { other.targetAbi += "-other"; });
        auto other = key;
        ++other.recipe.version;
        Check(key != other, "Decoder compatibility version lost its partition");
        std::cout << "TEXTURE_IMAGE_IDENTITY_OK variants=" << variants << " partitions=8 pathIgnored=1\n";
    }

    std::vector<std::byte> Read(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        Check(!!stream, "Fixture missing: " + path.string());
        const auto size = stream.tellg();
        Check(size > 0 && size < 1024 * 1024, "Fixture size invalid");
        std::vector<std::byte> bytes(static_cast<size_t>(size));
        stream.seekg(0);
        stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
        Check(!!stream, "Fixture read failed");
        return bytes;
    }

    class Source final : public Cooked::ArtifactByteSource
    {
    public:
        explicit Source(std::vector<std::byte> bytes) : bytes_(std::move(bytes)) {}
        bool Size(std::string_view path, uint64_t& size, std::string& failure) const override
        {
            if (path != "Derived/probe.cetex")
            {
                failure = "Unexpected artifact path";
                return false;
            }
            size = bytes_.size();
            return true;
        }
        bool ReadAt(std::string_view path, uint64_t offset, std::span<std::byte> out, std::string& failure) const override
        {
            uint64_t size{};
            if (!Size(path, size, failure) || offset > size || out.size() > size - offset)
            {
                return false;
            }
            std::memcpy(out.data(), bytes_.data() + offset, out.size());
            return true;
        }
    private:
        const std::vector<std::byte> bytes_;
    };

    experiment::AssetId Id(uint8_t value)
    {
        experiment::AssetId result;
        result.value.data_[6] = 0x40;
        result.value.data_[8] = 0x80;
        result.value.data_[15] = value;
        return result;
    }

    template<class T>
    AssetDepot::AssetRequestSnapshot<T> Finish(const AssetDepot::AssetRequest<T>& request)
    {
        const auto completion = request.Completion();
        if (completion.valid())
        {
            completion.wait();
        }
        return request.Snapshot();
    }

    void VerifyPreparedBinding(const own::shared_owner<const Texture>& texture,
        const own::shared_owner<const Texture>& labeled)
    {
        const auto origin = texture->GetAssetOrigin();
        Check(!!origin && origin->variant == AssetDepot::TextureAssetVariant{}, "Expected untransformed cooked descriptor");
        experiment::TextureReference reference;
        reference.assetId = origin->resolved.entry.asset.key.assetId;
        experiment::Material material;
        material.properties.push_back({ "probe", reference });
        auto captured = own::make_shared<AssetDepot::MaterialDocumentAssetOrigin>();
        captured->resolved.resolverRevision = origin->resolved.resolverRevision;
        material.assetOrigin = captured;
        const std::array supplied{ texture };
        std::string error;
        Check(!experiment::BindPreparedMaterialTextures(material, {}, error) && !error.empty()
            && std::addressof(*material.assetOrigin) == std::addressof(*captured), "Missing prepared owner changed captured material");
        const std::array labeledOnly{ labeled };
        Check(labeled && labeled->GetAssetOrigin()->variant.role == 99u
            && !experiment::BindPreparedMaterialTextures(material, labeledOnly, error) && !error.empty()
            && std::addressof(*material.assetOrigin) == std::addressof(*captured), "Transformed descriptor accepted as untransformed owner");
        auto wrongSnapshot = own::make_shared<AssetDepot::MaterialDocumentAssetOrigin>(*captured);
        ++wrongSnapshot->resolved.resolverRevision;
        material.assetOrigin = wrongSnapshot;
        Check(!experiment::BindPreparedMaterialTextures(material, supplied, error) && !error.empty()
            && std::addressof(*material.assetOrigin) == std::addressof(*wrongSnapshot), "Wrong resolver revision accepted or changed captured material");
        material.assetOrigin = captured;
        auto wrongReference = reference;
        wrongReference.assetId = Id(250u);
        material.properties.front().value = wrongReference;
        Check(!experiment::BindPreparedMaterialTextures(material, supplied, error) && !error.empty()
            && std::addressof(*material.assetOrigin) == std::addressof(*captured), "Wrong texture ID accepted or changed captured material");
        material.properties.front().value = reference;
        Check(experiment::BindPreparedMaterialTextures(material, supplied, error) && error.empty()
            && std::addressof(*material.assetOrigin) != std::addressof(*captured) && captured->textures.empty()
            && material.assetOrigin->textures.size() == 1u
            && std::addressof(*material.assetOrigin->textures.front().owner) == std::addressof(*texture),
            "Prepared binding lost exact owner or mutated shared captured origin");
        const auto accepted = material.assetOrigin;
        Check(experiment::BindPreparedMaterialTextures(material, supplied, error) && error.empty()
            && std::addressof(*material.assetOrigin) == std::addressof(*accepted) && material.assetOrigin->textures.size() == 1u,
            "Repeated prepared binding duplicated ownership");
        std::cout << "TEXTURE_PREPARED_BINDING_OK cases=6 unmountedOwner=1\n";
    }

    float Bc5Tolerance(TextureImageView image, unsigned mip, unsigned x, unsigned y, unsigned component, float expected)
    {
        // D3D functional spec 19.5.2/19.5.9/19.5.11: BC5 has a
        // per-block endpoint-span tolerance; BC7 does not inherit this rule.
        if (component >= 2 || expected == 0.0f || expected == 1.0f)
        {
            return 0.0000001f;
        }
        const auto* sub = image.Find(mip, 0);
        const auto* block = sub->pixels + (y / 4) * sub->rowPitch + (x / 4) * 16 + component * 8;
        const auto first = std::to_integer<unsigned>(block[0]);
        const auto second = std::to_integer<unsigned>(block[1]);
        uint64_t indices{};
        for (unsigned byte = 0; byte < 6; ++byte)
        {
            indices |= uint64_t(std::to_integer<unsigned>(block[byte + 2])) << (byte * 8);
        }
        const auto index = (indices >> (((y % 4) * 4 + x % 4) * 3)) & 7;
        if (index < 2)
        {
            return 0.0000001f;
        }
        return 1.0f / 65535.0f + 0.03f * std::abs(float(first) - float(second)) / 255.0f + 0.0000001f;
    }

    float ToSrgbCode(float value)
    {
        // D3D functional spec 3.2.3.7 measures conversion error on
        // the unrounded 8-bit sRGB side, not with a fixed linear epsilon.
        return 255.0f * (value <= 0.0031308f ? 12.92f * value :
            1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f);
    }

    own::shared_owner<const Texture> Acquire(DataSystem& data, const std::vector<std::byte>& original,
        uint8_t identity, bool corrupt)
    {
        Cooked::AssetSetManifest manifest;
        manifest.assetSetId = Id(identity);
        manifest.revision = 1;
        manifest.targetPlatform = "windows-x64";
        manifest.targetAbi = "cect-gpu-probe";
        Cooked::AssetBlobRecord blob;
        blob.artifactPath = "Derived/probe.cetex";
        blob.byteSize = original.size();
        blob.kind = Cooked::CookedAssetKind::Texture;
        blob.representation = Cooked::kCookedTextureRepresentationVersion;
        blob.schemaVersion = Cooked::kCookedTextureSchemaVersion;
        blob.targetPlatform = manifest.targetPlatform;
        blob.targetAbi = manifest.targetAbi;
        std::string error;
        Check(Cooked::ComputeSha256(original, blob.contentSha256, error), error);
        manifest.blobs.push_back(blob);
        const Cooked::TypedAssetReference reference{ { Id(identity + 1), {} }, Cooked::CookedAssetKind::Texture };
        manifest.entries.push_back({ reference, 0, {} });
        manifest.roots.push_back(reference);
        auto bytes = original;
        if (corrupt)
        {
            bytes.back() ^= std::byte{1};
            Check(bytes.size() == original.size(), "Corruption changed artifact size");
            Check(Cooked::ValidateCookedTexture(bytes, error), "Payload mutation should retain valid CECT structure");
        }
        const auto encoded = Cooked::WriteAssetSetManifest(manifest);
        Check(encoded.Succeeded(), "AssetSet manifest encoding");
        const own::shared_owner<const Cooked::ArtifactByteSource> source = own::make_shared<const Source>(std::move(bytes));
        std::vector<Cooked::AssetManifestIssue> issues;
        const auto mount = data.MountAssetSet(encoded.bytes, source,
            { manifest.targetPlatform, manifest.targetAbi, {} }, issues);
        Check(mount.IsValid(), "Metadata mount failed: " + (issues.empty() ? std::string{} : issues.front().message));
        const AssetDepot::AssetLink<Texture> link{ reference.key };
        const auto snapshot = Finish(data.RequestAsync<Texture>(link));
        own::shared_owner<const Texture> labeled;
        if (corrupt)
        {
            Check(snapshot.status == AssetDepot::AssetRequestStatus::Failed &&
                snapshot.error == AssetDepot::AssetRequestError::IntegrityFailed && !snapshot.asset &&
                snapshot.message.find("SHA-256") != std::string::npos,
                "Same-size payload must fail typed acquisition with IntegrityFailed: " + snapshot.message);
        }
        else
        {
            Check(snapshot.status == AssetDepot::AssetRequestStatus::Ready && !!snapshot.asset,
                "Cooked typed acquisition failed: " + snapshot.message);
            AssetDepot::TextureAssetVariant variant;
            variant.role = 99u;
            const auto labeledState = Finish(data.RequestAsync<Texture>(link, variant));
            Check(labeledState.status == AssetDepot::AssetRequestStatus::Ready && !!labeledState.asset,
                "Labeled cooked typed acquisition failed: " + labeledState.message);
            labeled = labeledState.asset;
        }
        Check(data.UnmountAssetSet(mount, issues), "Unmount failed");
        if (!corrupt)
        {
            VerifyPreparedBinding(snapshot.asset, labeled);
        }
        return snapshot.asset;
    }
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 3, "fixture root and absolute shader path required");
        std::cout << std::unitbuf;
        VerifyImageIdentity();
        const std::filesystem::path root = argv[1];
        const auto repo = std::filesystem::path(argv[2]).parent_path().parent_path().parent_path();
        auto* paths = (&ce::paths::get_state());
        paths->base_project_path = repo / "Dynamic_CPP";
        paths->shader_source_path = repo / "Dynamic_CPP/Assets/Shaders";
        paths->cache_root = root / "Cache";
        paths->asset_authoring_enabled = false;
        ce::get_job_scheduler().start(2);
        auto& data = *DataSystem::GetInstance();
        std::string error;
        RHIShaderCompiler::VerifiedShader shader;
        Check(RHIShaderCompiler::VerifyFile(argv[2], "VerifyCookedTexture", "cs_6_0", kShaderBinary,
            {}, shader, error), error);
        ProbeDevice device;
#ifdef CECT_PROBE_VULKAN
        Check(device.Initialize(8, 32, true, error) && device.IsValidationEnabled(), error);
        VulkanPipelineCache pipelines;
        pipelines.Initialize(device.GetDevice());
        device.SetPipelineCache(&pipelines);
        auto& roots = pipelines;
        std::cout << "VULKAN_VALIDATION_ENABLED=true\n";
#else
        Check(device.Initialize(8, 32, error), error);
        DX12RootSignatureCache roots;
        DX12PSOManager pipelines;
#endif
        ProbeTextures textures;
        ProbePool pool;
#ifdef CECT_PROBE_VULKAN
        Check(textures.Initialize(&device, error) && pool.Initialize(device, 1, ProbeDevice::kFrameCount, error), error);
#else
        Check(roots.Initialize(&device, error) && pipelines.Initialize(&device, L"", error) &&
            textures.Initialize(&device, error) && pool.Initialize(device, 1, DX12DeviceResources::kFrameCount, error), error);
#endif
        const RHIPipelineLayoutParam parameters[]{ RHILayout::SrvTable(1), RHILayout::UavTable(1) };
        const RHIStaticSamplerDesc sampler{ RHISampler::Point(), 0, RHIShaderVisibility::All };
        const auto layout = roots.GetOrCreate({ parameters, { &sampler, 1 } }, error);
        RHIComputePipelineDesc desc;
        desc.layout = layout;
        desc.csBytecode = shader.bytecode.Data();
        desc.csSize = shader.bytecode.Size();
        const auto pipeline = pipelines.GetOrCreateCompute(desc, error);
        Check(pipeline.IsValid(), error);
        uint8_t identity = 10;
        unsigned comparisons{};
        for (const std::string name : { "bc5", "bc7", "bc7-srgb" })
        {
            const auto artifact = Read(root / (name + ".cetex"));
            Check(!Acquire(data, artifact, identity, true), "Corrupt descriptor published");
            identity += 2;
            std::cout << "INTEGRITY_OK format=" << name << " sameSize=true structureValid=true error=IntegrityFailed\n";
            const auto texture = Acquire(data, artifact, identity, false);
            identity += 2;
            const auto imageState = Finish(data.RequestAsync<Texture::CodecImage>(texture));
            Check(imageState.status == AssetDepot::AssetRequestStatus::Ready && !!imageState.asset, "Exact image reacquisition");
            const auto expectedFormat = name == "bc5" ? RHIFormat::BC5Unorm :
                name == "bc7" ? RHIFormat::BC7Unorm : RHIFormat::BC7UnormSrgb;
            Check(texture->GetImageDescription().Format() == expectedFormat, "Cooked format drift");
            Check(device.BeginFrame(error), error);
            pool.BeginFrame(0);
            textures.BeginFrame(identity);
            const auto uploaded = textures.GetOrUpload(&*texture.borrow(), imageState.asset, error);
            Check(uploaded.IsValid() && uploaded.format == expectedFormat && uploaded.mipLevels == 4 &&
                textures.GetUploadFailureCount() == 0, "Exact prepared upload failed: " + error);
            RHIReadback readback;
            Check(device.CreateReadback(8, 32, RHIFormat::RGBA32Float, 1, readback, error), error);
            {
                EnhancedRenderGraph graph(static_cast<IRenderDeviceServices&>(device));
                const auto input = graph.ImportTexture(uploaded.handle, kUploadedState, name);
                RGTextureDesc target;
                target.width = 8;
                target.height = 32;
                target.format = RHIFormat::RGBA32Float;
                target.allowUnorderedAccess = true;
                const auto output = graph.CreateTexture(target);
                graph.AddPass("CECT.SampleAllMips", {{input, RHIResourceState::ShaderResource}, {output, RHIResourceState::UnorderedAccess}},
                    [&](const auto& context) {
                        const auto srv = RHIBindingDesc::Srv2D(uploaded.handle, uploaded.format, 0, 4);
                        const auto uav = RHIBindingDesc::Uav2D(context.ResolveHandle(output), RHIFormat::RGBA32Float);
                        const auto sourceTable = device.CreateBindings({ &srv, 1 });
                        const auto outputTable = device.CreateBindings({ &uav, 1 });
                        Check(sourceTable.IsValid() && outputTable.IsValid(), "Descriptor binding failed");
                        context.encoder->SetPipeline(RHIBindPoint::Compute, pipeline);
                        context.encoder->SetBindings(RHIBindPoint::Compute, 0, sourceTable);
                        context.encoder->SetBindings(RHIBindPoint::Compute, 1, outputTable);
                        context.encoder->Dispatch(1, 4, 1);
                    });
                graph.AddPass("CECT.Readback", {{output, RHIResourceState::CopySource}},
                    [&](const auto& context) { context.encoder->CopyToReadback(readback, context.ResolveHandle(output)); }, true);
                Check(graph.Compile(error), error);
                RHIRecordedBatchDesc batchDesc;
                batchDesc.frameId = identity;
                batchDesc.backendGeneration = GetRHISubmissionThread().GetOwnerGeneration(&device);
                batchDesc.lifetimeToken = std::make_shared<unsigned>(identity);
                RHIRecordedBatch batch;
                RHISubmissionTicket ticket;
                Check(graph.RecordParallel(pool, 1, batchDesc, batch, error) &&
                    GetRHISubmissionThread().EnqueueRecordedBatch(&device, device, std::move(batch), ticket, error) &&
                    device.EndFrame(error) && GetRHISubmissionThread().Wait(ticket, error), error);
                device.WaitForGpu();
                RHIReadbackImage result;
                Check(device.MapReadback(readback, result, error), error);
                const auto reference = Read(root / (name + ".reference"));
                Check(reference.size() == 340 * sizeof(float), "CPU reference size");
                unsigned index{};
                float maximum{};
                for (unsigned mip = 0; mip < 4; ++mip)
                {
                    const auto size = 8u >> mip;
                    for (unsigned y = 0; y < size; ++y)
                    {
                        for (unsigned x = 0; x < size; ++x)
                        {
                            for (unsigned component = 0; component < 4; ++component)
                            {
                                float expected;
                                std::memcpy(&expected, reference.data() + index++ * sizeof(float), sizeof(float));
                                const auto actual = result.At(x, mip * 8 + y, component);
                                const auto delta = std::abs(actual - expected);
                                maximum = std::max(maximum, delta);
                                const auto srgb = name == "bc7-srgb" && component != 3 && expected != 0 && expected != 1;
                                const auto tolerance = name == "bc5" ? Bc5Tolerance(texture->GetImageView(imageState.asset),
                                    mip, x, y, component, expected) : srgb ? 0.50001f : 0.0000001f;
                                const auto comparisonError = srgb ? std::abs(ToSrgbCode(actual) - std::round(ToSrgbCode(expected))) : delta;
                                Check(std::isfinite(actual) && comparisonError <= tolerance,
                                    "GPU decoded texel mismatch: " + name + " mip=" + std::to_string(mip) +
                                    " x=" + std::to_string(x) + " y=" + std::to_string(y) +
                                    " component=" + std::to_string(component) + " actual=" + std::to_string(actual) +
                                    " expected=" + std::to_string(expected) + " delta=" + std::to_string(delta) +
                                    " comparisonError=" + std::to_string(comparisonError) + " tolerance=" + std::to_string(tolerance));
                                ++comparisons;
                            }
                        }
                    }
                }
                std::cout << "GPU_SAMPLE_OK format=" << name << " mips=4 components=340 maxError=" << maximum << '\n';
            }
            device.ReleaseReadback(readback);
        }
        device.WaitForGpu();
#ifdef CECT_PROBE_VULKAN
        Check(device.GetUnimplementedCount() == 0 && device.GetEncoderUnimplementedCount() == 0 &&
            pool.GetEncoderUnimplementedCount() == 0, "Unimplemented Vulkan encoder operation");
#endif
        pool.Shutdown();
        textures.Shutdown();
#ifdef CECT_PROBE_VULKAN
        device.SetPipelineCache(nullptr);
#endif
        pipelines.Shutdown();
#ifndef CECT_PROBE_VULKAN
        roots.Shutdown();
#endif
        device.Shutdown();
        std::string validation;
        Check(device.DrainDebugMessages(validation) == 0, validation);
        data.Finalize();
        ce::get_job_scheduler().shutdown();
        std::cout << "TEXTURE_COOKED_GPU_OK backend=" << kBackend << " formats=3 components=" << comparisons << " corruptRejected=3 validation=0\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        DataSystem::GetInstance()->Finalize();
        ce::get_job_scheduler().shutdown();
        return 1;
    }
}
