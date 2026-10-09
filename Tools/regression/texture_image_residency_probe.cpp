// Unrun Windows runtime regression source. Link with the existing RenderEngine
// and ownership/job-scheduler harness; no shader compilation or GPU is required.
#include "../../Engine/RenderEngine/DataSystem.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/ArtifactByteSource.h"
#include "../../Engine/RenderEngine/TextureFramePins.h"
#include "../../Engine/RenderEngine/Experiment/Cooked/CookedTexture.h"

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace
{
    namespace Cooked = experiment::cooked;

    void Require(bool condition, const char* message)
    {
        if (!condition)
        {
            throw std::runtime_error(message);
        }
    }

    struct Reads final
    {
        std::atomic<unsigned> captures{};
        std::atomic<unsigned> reads{};
        std::atomic<unsigned> pathMismatches{};
        std::mutex mutex;
        std::condition_variable ready;
        bool allow{};
    };

    // The narrowed adapter refuses every path except the path it captured. This
    // catches sharing a source owner while accidentally retaining another path.
    class ImageSource final : public Cooked::ArtifactByteSource
    {
    public:
        ImageSource(own::shared_owner<const std::vector<std::byte>> bytes,
            own::shared_owner<Reads> reads, std::string captured = {})
            : bytes_(std::move(bytes)), reads_(std::move(reads)), captured_(std::move(captured))
        {
        }
        bool CaptureArtifact(std::string_view path,
            own::shared_owner<const ArtifactByteSource>& result, std::string& failure) const override
        {
            if (!Accept(path, failure))
            {
                return false;
            }
            ++reads_->captures;
            if (captured_.empty())
            {
                result = own::make_shared<const ImageSource>(bytes_, reads_, std::string(path));
            }
            else
            {
                result.reset();
            }
            return true;
        }
        bool Size(std::string_view path, std::uint64_t& size, std::string& failure) const override
        {
            if (!Accept(path, failure))
            {
                return false;
            }
            size = bytes_->size();
            return true;
        }
        bool ReadAt(std::string_view path, std::uint64_t offset,
            std::span<std::byte> destination, std::string& failure) const override
        {
            if (!Accept(path, failure) || offset > bytes_->size()
                || destination.size() > bytes_->size() - offset)
            {
                return false;
            }
            {
                std::unique_lock lock(reads_->mutex);
                reads_->ready.wait(lock, [&]() { return reads_->allow; });
            }
            ++reads_->reads;
            std::memcpy(destination.data(), bytes_->data() + offset, destination.size());
            return true;
        }
    private:
        bool Accept(std::string_view path, std::string& failure) const
        {
            if ((!captured_.empty() && captured_ != path) || !path.starts_with("Derived/"))
            {
                ++reads_->pathMismatches;
                failure = "Mismatched captured image source/path";
                return false;
            }
            return true;
        }
        own::shared_owner<const std::vector<std::byte>> bytes_;
        own::shared_owner<Reads> reads_;
        std::string captured_;
    };

    experiment::AssetId Id(std::uint8_t byte)
    {
        experiment::AssetId id;
        id.value.data[6] = 0x40u;
        id.value.data[8] = 0x80u;
        id.value.data[15] = byte;
        return id;
    }

    template<class T>
    own::shared_owner<const T> Finish(const AssetDepot::AssetRequest<T>& request)
    {
        // The probe is a test controller, not GT/RT or a job-system worker.
        const auto completion = request.Completion();
        if (completion.valid())
        {
            completion.wait();
        }
        const auto snapshot = request.Snapshot();
        Require(snapshot.status == AssetDepot::AssetRequestStatus::Ready && !!snapshot.asset,
            "Texture request did not finish Ready");
        return snapshot.asset;
    }

    void VerifyRecipeIdentity()
    {
        Cooked::AssetBlobRecord blob;
        blob.kind = Cooked::CookedAssetKind::Texture;
        blob.byteSize = 34u;
        blob.representation = Cooked::kCookedTextureRepresentationVersion;
        blob.schemaVersion = Cooked::kCookedTextureSchemaVersion;
        blob.targetPlatform = "probe";
        blob.targetAbi = "probe";
        AssetDepot::TextureAssetVariant linear;
        linear.colorSpace = AssetDepot::TextureAssetColorSpace::Linear;
        auto srgb = linear;
        srgb.colorSpace = AssetDepot::TextureAssetColorSpace::Srgb;
        srgb.role = 99u;
        Require(AssetDepot::MakeTextureImageKey(blob, linear) == AssetDepot::MakeTextureImageKey(blob, srgb),
            "Sampling-only labels or roles split compatible bytes");
        linear.compress = true;
        srgb.compress = true;
        Require(AssetDepot::MakeTextureImageKey(blob, linear) == AssetDepot::MakeTextureImageKey(blob, srgb),
            "Runtime compression hint split already-cooked bytes");
        linear.compress = false;
        srgb.compress = false;
        linear.mipPolicy = AssetDepot::TextureMipPolicy::GenerateFull;
        srgb.mipPolicy = AssetDepot::TextureMipPolicy::GenerateFull;
        Require(AssetDepot::MakeTextureImageKey(blob, linear) == AssetDepot::MakeTextureImageKey(blob, srgb),
            "Runtime mip hint split already-cooked bytes");
        const auto original = AssetDepot::MakeTextureImageKey(blob, linear);
        blob.artifactPath = "Derived/Another/path.tga";
        Require(original == AssetDepot::MakeTextureImageKey(blob, linear), "Path entered compatible image identity");
        blob.targetAbi += "-other";
        Require(original != AssetDepot::MakeTextureImageKey(blob, linear), "ABI omitted from image identity");
    }

    void VerifyExactResidency(DataSystem& data)
    {
        auto fixtureImage = TextureImage::Allocate(RHIFormat::RGBA8Unorm, 2u, 2u, 1u, 1u);
        Require(fixtureImage.IsValid(), "Fixture image allocation failed");
        const auto* subresource = fixtureImage.Find(0u, 0u);
        std::memset(fixtureImage.MutablePixelsAt(*subresource), 0x80, subresource->slicePitch);
        std::vector<std::byte> artifact;
        std::string encodeFailure;
        Require(Cooked::EncodeCookedTexture(fixtureImage.View(), artifact, encodeFailure, { true, false }),
            "Fixture cooked encoding failed");
        const auto bytes = own::make_shared<const std::vector<std::byte>>(std::move(artifact));
        const auto reads = own::make_shared<Reads>();
        struct ReleaseGate final
        {
            own::shared_owner<Reads> reads;
            ~ReleaseGate()
            {
                {
                    std::lock_guard lock(reads->mutex);
                    reads->allow = true;
                }
                reads->ready.notify_all();
            }
        } releaseGate{ reads };
        const own::shared_owner<const Cooked::ArtifactByteSource> source =
            own::make_shared<const ImageSource>(bytes, reads);
        Cooked::AssetSetManifest manifest;
        manifest.assetSetId = Id(1u);
        manifest.revision = 1u;
        manifest.targetPlatform = "probe";
        manifest.targetAbi = "probe";
        Cooked::AssetBlobRecord blob;
        blob.byteSize = bytes->size();
        blob.kind = Cooked::CookedAssetKind::Texture;
        blob.representation = Cooked::kCookedTextureRepresentationVersion;
        blob.schemaVersion = Cooked::kCookedTextureSchemaVersion;
        blob.targetPlatform = manifest.targetPlatform;
        blob.targetAbi = manifest.targetAbi;
        std::string error;
        Require(Cooked::ComputeSha256(*bytes, blob.contentSha256, error), "Fixture SHA failed");
        for (std::uint8_t index = 0u; index < 2u; ++index)
        {
            blob.artifactPath = "Derived/Image/" + std::to_string(index) + ".cetex";
            manifest.blobs.push_back(blob);
            const Cooked::TypedAssetReference asset{ { Id(2u + index), {} }, Cooked::CookedAssetKind::Texture };
            manifest.entries.push_back({ asset, index, {} });
            manifest.roots.push_back(asset);
        }
        const auto encoded = Cooked::WriteAssetSetManifest(manifest);
        Require(encoded.Succeeded(), "Fixture manifest failed");
        std::vector<Cooked::AssetManifestIssue> issues;
        const auto mount = data.MountAssetSet(encoded.bytes, source, { "probe", "probe", {} }, issues);
        Require(mount.IsValid(), "Fixture mount failed");
        data.SetTextureAssetCacheBudget(0u);
        data.SetTextureImageCacheBudget(1024u * 1024u);
        const AssetDepot::AssetLink<Texture> first{ manifest.roots[0].key };
        const AssetDepot::AssetLink<Texture> second{ manifest.roots[1].key };
        auto firstRequest = data.RequestAsync<Texture>(first);
        auto secondRequest = data.RequestAsync<Texture>(second);
        Require(firstRequest.Snapshot().status == AssetDepot::AssetRequestStatus::Pending,
            "Submission blocked instead of returning Pending");
        {
            std::lock_guard lock(reads->mutex);
            reads->allow = true;
        }
        reads->ready.notify_all();
        const auto a = Finish(firstRequest);
        const auto b = Finish(secondRequest);
        Require(a->m_assetId != b->m_assetId && !a->NonRehydratableImage() && !b->NonRehydratableImage(),
            "Logical descriptions collapsed or permanently pinned reproducible pixels");
        Require(reads->reads == 1u, "Compatible logical assets decoded/read twice");
        auto imageA = data.TryAcquire<Texture::CodecImage>(a);
        auto imageB = data.TryAcquire<Texture::CodecImage>(b);
        Require(imageA && imageB && &*imageA == &*imageB, "Compatible images were not shared");
        const auto relabelled = Texture::WithColorSpace(a, true);
        {
            const auto relabelledImage = data.TryAcquire<Texture::CodecImage>(relabelled);
            Require(relabelledImage && &*relabelledImage == &*imageA,
                "Sampling relabel did not reuse payload");
        }
        own::weak_owner<const Texture::CodecImage> weak = imageA;
        imageA.reset();
        imageB.reset();
        data.SetTextureImageCacheBudget(0u);
        Require(weak.expired(), "Pinned descriptors retained evicted pixels");
        Require(!a->GetImageDescription().IsEmpty(), "Eviction lost metadata");
        AssetDepot::TextureAssetVariant rgbaVariant;
        rgbaVariant.forceRgba8 = true;
        const auto rgbaRequest = data.RequestAsync<Texture>(first, rgbaVariant);
        const auto rgbaDescriptor = Finish(rgbaRequest);
        const auto rgbaImageRequest = data.RequestAsync<Texture::CodecImage>(rgbaDescriptor);
        const auto rgbaImage = Finish(rgbaImageRequest);
        Require(rgbaDescriptor->GetImageView(rgbaImage).Format() == RHIFormat::RGBA8Unorm,
            "Explicit RGBA8 recipe failed or lost its output layout");
        Require(data.UnmountAssetSet(mount, issues), "Unmount failed");
        Require(!data.TryAcquire<Texture>(first), "Current lookup resurrected unmounted generation");
        {
            std::lock_guard lock(reads->mutex);
            reads->allow = false;
        }
        auto cancelled = data.RequestAsync<Texture::CodecImage>(b);
        auto exact = data.RequestAsync<Texture::CodecImage>(b);
        cancelled.Cancel();
        {
            std::lock_guard lock(reads->mutex);
            reads->allow = true;
        }
        reads->ready.notify_all();
        auto image = Finish(exact);
        Require(cancelled.Snapshot().status == AssetDepot::AssetRequestStatus::Cancelled,
            "Consumer cancellation changed into shared-job readiness");
        Require(!b->GetImageView(image).IsEmpty(), "Old generation did not rehydrate its exact source");
        weak = image;
        image.reset();
        Require(!weak.expired(), "Zero-budget ready request lost its result before handoff");
        TextureFramePins frame;
        image = exact.Snapshot().asset;
        frame.RetainImage(b, image);
        exact = {};
        image.reset();
        Require(!weak.expired(), "Request-to-frame handoff lost image");
        frame.ReleaseImages();
        Require(weak.expired(), "Completed staging frame retained image");
        const auto mipped = Texture::WithMipChain(relabelled, error);
        Require(mipped && !mipped->NonRehydratableImage() && mipped->GetImageDescription().MipLevels() == 2u,
            "Generated mip descriptor is not independently rehydratable");
        const auto mipRequest = data.RequestAsync<Texture::CodecImage>(mipped);
        const auto mipImage = Finish(mipRequest);
        Require(mipped->GetImageView(mipImage).MipLevels() == 2u,
            "WithMipChain origin rehydrated the unmodified source");
        Require(reads->pathMismatches == 0u, "Compatible cache hit mismatched exact source/path");
    }
}

int main()
{
    try
    {
        VerifyRecipeIdentity();
        ce::get_job_scheduler().start(2u);
        auto* data = DataSystem::GetInstance();
        VerifyExactResidency(*data);
        data->Finalize();
        ce::get_job_scheduler().shutdown();
        std::cout << "texture image residency probe passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        DataSystem::GetInstance()->Finalize();
        ce::get_job_scheduler().shutdown();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
