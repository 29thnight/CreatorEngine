#include "TextureAssetRuntime.h"
#include "../DataSystem.h"
#include "../Texture.h"
#include "../Experiment/Cooked/CookedTexture.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <exception>
#include <limits>
#include <utility>

namespace
{
    namespace texture_cooked = experiment::cooked;

    // A borrowed lock-context marker, never resource identity or queued state.
    // The enclosing RequestAsync local owner spans this entire lexical scope.
    thread_local const AssetDepot::TextureAssetWork* TextureDepotSubmittingWork{};

    class TextureDepotSubmissionScope final
    {
    public:
        explicit TextureDepotSubmissionScope(const AssetDepot::TextureAssetWork& work)
            : m_previous(std::exchange(TextureDepotSubmittingWork, &work))
        {
        }
        ~TextureDepotSubmissionScope()
        {
            TextureDepotSubmittingWork = m_previous;
        }
        TextureDepotSubmissionScope(const TextureDepotSubmissionScope&) = delete;
        TextureDepotSubmissionScope& operator=(const TextureDepotSubmissionScope&) = delete;
    private:
        const AssetDepot::TextureAssetWork* m_previous;
    };

    AssetDepot::TextureAssetKey TextureDepotKey(const texture_cooked::ResolvedAssetEntry& resolved,
        const AssetDepot::TextureAssetVariant& variant)
    {
        return { resolved.entry.asset, resolved.blob, resolved.resolverRevision, variant };
    }

    AssetDepot::AssetRequestError TextureDepotLookupError(texture_cooked::AssetLookupStatus status)
    {
        switch (status)
        {
        case texture_cooked::AssetLookupStatus::NotMounted:
            return AssetDepot::AssetRequestError::NotMounted;
        case texture_cooked::AssetLookupStatus::TypeMismatch:
            return AssetDepot::AssetRequestError::TypeMismatch;
        case texture_cooked::AssetLookupStatus::HardDependencyCycle:
            return AssetDepot::AssetRequestError::HardDependencyCycle;
        default:
            return AssetDepot::AssetRequestError::None;
        }
    }

    std::size_t TextureDepotAddCharge(std::size_t left, std::size_t right) noexcept
    {
        const auto maximum = (std::numeric_limits<std::size_t>::max)();
        return right > maximum - left ? maximum : left + right;
    }

    std::size_t TextureDepotCharge(const own::shared_owner<const Texture>& texture)
    {
        const auto origin = texture->GetAssetOrigin();
        return TextureDepotAddCharge(texture->DescriptorByteSize(),
            origin ? origin->hardDependencyChargeBytes : 0u);
    }

    std::size_t TextureImageCharge(const own::shared_owner<const Texture::CodecImage>& image,
        const own::shared_owner<const AssetDepot::TextureImageSource>& source) noexcept
    {
        auto charge = Texture::ImageRetainedCharge(image);
        if (source)
        {
            charge = TextureDepotAddCharge(charge, sizeof(AssetDepot::TextureImageSource));
            charge = TextureDepotAddCharge(charge, source->artifactPath.capacity());
            charge = TextureDepotAddCharge(charge, 1u);
        }
        return charge;
    }

    bool TextureDepotMatches(const own::shared_owner<const Texture>& texture,
        const AssetDepot::TextureAssetKey& key)
    {
        const auto origin = texture->GetAssetOrigin();
        return origin && origin->resolved.entry.asset == key.asset
            && origin->resolved.blob == key.blob
            && origin->resolved.resolverRevision == key.resolverRevision
            && origin->variant == key.variant;
    }

    std::string TextureDepotDiagnostic(const char* message) noexcept
    {
        try
        {
            return message;
        }
        catch (...)
        {
            return {};
        }
    }

    template<class T>
    void TextureDepotNotify(const own::shared_owner<AssetDepot::AssetRequestState<T>>& consumer,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        const std::string& message, const own::shared_owner<const T>& asset)
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != AssetDepot::AssetRequestStatus::Pending)
        {
            return;
        }
        consumer->SetTerminalLocked(status);
        consumer->error = error;
        consumer->asset = asset;
        try
        {
            consumer->message = message;
        }
        catch (...)
        {
            // A diagnostic allocation failure must not strand other consumers.
            consumer->message.clear();
        }
    }
}

own::shared_owner<const Texture> DataSystem::TryAcquireTexture(
    AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant)
{
    if (!link.IsValid())
    {
        return {};
    }
    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
    {
        return {};
    }
    texture_cooked::ResolvedAssetEntry resolved;
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (!m_cookedCatalog || m_cookedCatalog->Find(link.ToReference(), resolved)
            != texture_cooked::AssetLookupStatus::Found)
        {
            return {};
        }
    }
    const auto key = TextureDepotKey(resolved, variant);
    const auto found = m_textureAssets.entries.find(key);
    if (found == m_textureAssets.entries.end())
    {
        return {};
    }
    auto texture = found->second.retained ? found->second.retained : found->second.live.lock();
    if (!texture || !TextureDepotMatches(texture, key))
    {
        return {};
    }
    found->second.lastUse = ++m_textureAssets.clock;
    // No source Size/ReadAt, decoder call, upload, scheduling or wait here.
    return texture;
}

AssetDepot::AssetRequest<Texture> DataSystem::RequestTextureAsync(
    AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant)
{
    own::shared_owner<const texture_cooked::CookedAssetCatalog> catalog;
    std::uint64_t epoch{};
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        catalog = m_cookedCatalog;
        epoch = m_assetPreparationEpoch;
    }
    return RequestTextureAsyncFromSnapshot(link, variant, std::move(catalog), epoch);
}

AssetDepot::AssetRequest<Texture> DataSystem::RequestTextureAsyncFromSnapshot(
    AssetDepot::AssetLink<Texture> link, const AssetDepot::TextureAssetVariant& variant,
    own::shared_owner<const texture_cooked::CookedAssetCatalog> catalog, std::uint64_t epoch)
{
    using namespace AssetDepot;
    auto consumer = own::make_shared<AssetRequestState<Texture>>(m_assetRequestCounters);
    AssetRequest<Texture> request(consumer);
    const auto fail = [&](AssetRequestStatus status, AssetRequestError error, const std::string& message)
    {
        TextureDepotNotify(consumer, status, error, message, {});
        return request;
    };
    if (!link.IsValid())
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::InvalidLink, "Invalid texture asset link.");
    }
    if (variant.mipPolicy != TextureMipPolicy::PreserveAuthored
        && variant.mipPolicy != TextureMipPolicy::GenerateFull)
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedRepresentation,
            "Unknown texture mip policy.");
    }
    if (variant.colorSpace != TextureAssetColorSpace::Source
        && variant.colorSpace != TextureAssetColorSpace::Linear
        && variant.colorSpace != TextureAssetColorSpace::Srgb)
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedRepresentation,
            "Unknown texture color-space variant.");
    }
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping)
        {
            return fail(AssetRequestStatus::Cancelled, AssetRequestError::ShuttingDown,
                "Asset admission is stopped.");
        }
        if (m_assetInvalidationDepth != 0u)
        {
            return fail(AssetRequestStatus::Stale, AssetRequestError::RevisionChanged,
                "Asset invalidation is in progress.");
        }
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (epoch != m_assetPreparationEpoch || (catalog && (!m_cookedCatalog
            || catalog->ResolverRevision() != m_cookedCatalog->ResolverRevision())))
        {
            return fail(AssetRequestStatus::Stale, AssetRequestError::RevisionChanged,
                "Captured texture resolver changed; request was not rebound.");
        }
    }
    if (!catalog)
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::NotMounted, "No asset catalog is mounted.");
    }

    // Metadata-only traversal on an immutable snapshot, outside the shared lock.
    std::vector<texture_cooked::ResolvedAssetEntry> closure;
    texture_cooked::AssetCatalogLookupIssue issue;
    const auto lookup = catalog->CollectHardClosure(link.ToReference(), closure, issue);
    if (lookup != texture_cooked::AssetLookupStatus::Found)
    {
        return fail(AssetRequestStatus::Failed, TextureDepotLookupError(lookup), issue.message);
    }
    for (const auto& resolved : closure)
    {
        if (resolved.entry.asset.kind != texture_cooked::CookedAssetKind::Texture)
        {
            return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedType,
                "A texture hard dependency requires a runtime decoder that is not implemented.");
        }
        if (resolved.blob.kind != texture_cooked::CookedAssetKind::Texture
            || resolved.blob.representation != texture_cooked::kCookedTextureRepresentationVersion
            || resolved.blob.schemaVersion != texture_cooked::kTextureArtifactVersion)
        {
            return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedRepresentation,
                "Texture acquisition requires GPU-ready CECT representation 2, schema 2; recook this asset.");
        }
    }

    std::map<texture_cooked::AssetIdentity, own::shared_owner<TextureAssetWork>> selected;
    AssetDepot::TextureAssetRetiredEntries retired;
    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping)
    {
        return fail(AssetRequestStatus::Cancelled, AssetRequestError::ShuttingDown, "Asset admission is stopped.");
    }
    if (m_assetInvalidationDepth != 0u)
    {
        return fail(AssetRequestStatus::Stale, AssetRequestError::RevisionChanged,
            "Asset invalidation is in progress.");
    }
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetPreparationEpoch != epoch || !m_cookedCatalog
            || m_cookedCatalog->ResolverRevision() != catalog->ResolverRevision())
        {
            return fail(AssetRequestStatus::Stale, AssetRequestError::RevisionChanged,
                "Asset resolver changed during metadata lookup; request was not retried.");
        }
    }

    retired.descriptorPins.reserve(m_textureAssets.entries.size());
    retired.imagePins.reserve(m_textureAssets.images.size());
    TrimTextureAssetsLocked(retired);
    for (const auto& resolved : closure)
    {
        const auto nodeVariant = resolved.entry.asset.key == link.identity ? variant : TextureAssetVariant{};
        const auto key = TextureDepotKey(resolved, nodeVariant);
        const auto previous = m_textureAssets.entries.find(key);
        if (previous != m_textureAssets.entries.end() && previous->second.inFlight
            && previous->second.inFlight->epoch != epoch)
        {
            const auto stale = previous->second.inFlight;
            CompleteTextureAssetWorkLocked(stale, AssetRequestStatus::Stale,
                AssetRequestError::RevisionChanged, {});
        }
        auto& entry = m_textureAssets.entries[key];
        entry.lastUse = ++m_textureAssets.clock;
        if (entry.inFlight)
        {
            selected.emplace(resolved.entry.asset.key, entry.inFlight);
            continue;
        }
        auto resident = entry.retained ? entry.retained : entry.live.lock();
        auto work = own::make_shared<TextureAssetWork>();
        work->key = key;
        work->resolved = resolved;
        work->catalog = catalog;
        work->epoch = epoch;
        if (resident && TextureDepotMatches(resident, key))
        {
            work->status = AssetRequestStatus::Ready;
            work->asset = std::move(resident);
            selected.emplace(resolved.entry.asset.key, std::move(work));
            continue;
        }
        if (m_textureAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
        {
            return fail(AssetRequestStatus::Failed, AssetRequestError::SubmissionFailed,
                "Texture generation request identity space exhausted.");
        }
        work->requestId = m_textureAssets.nextRequestId++;
        std::vector<job_handle> dependencies;
        for (const auto& dependency : resolved.entry.dependencies)
        {
            if (dependency.kind != texture_cooked::AssetDependencyKind::Hard)
            {
                continue;
            }
            const auto child = selected.find(dependency.target.key);
            if (child == selected.end())
            {
                return fail(AssetRequestStatus::Failed, AssetRequestError::DependencyFailed,
                    "Hard dependency traversal did not produce a dependency-first closure.");
            }
            work->dependencies.push_back(child->second);
            if (child->second->completion.valid())
            {
                dependencies.push_back(child->second->completion);
            }
        }
        work->imageWork = StartTextureImageWorkLocked(MakeTextureImageKey(resolved.blob, nodeVariant), resolved);
        if (work->imageWork->completion.valid())
        {
            dependencies.push_back(work->imageWork->completion);
        }
        // Allocate every registration record before exposing a Pending job.
        // A failed local map insertion must not strand an unsubmitted in-flight entry.
        selected.emplace(resolved.entry.asset.key, work);
        entry.inFlight = work;
        TextureDepotSubmissionScope submission(*work);
        try
        {
            job_group jobs;
            jobs.add([this, work]() { RunTextureAssetWork(work); });
            jobs.on_complete([this, work](std::exception_ptr error)
            {
                const auto finish = [&]()
                {
                    // Even a scheduler-skipped body must terminalize its cache
                    // entry and notify every consumer before its handle is ready.
                    CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Failed,
                        error ? AssetRequestError::SubmissionFailed : AssetRequestError::DecodeFailed,
                        {});
                };
                if (TextureDepotSubmittingWork == &*work)
                {
                    // Inline submission failure: this exact thread already owns
                    // the admission lock. Asynchronous callbacks have other TLS.
                    finish();
                }
                else
                {
                    std::lock_guard completionLock(m_assetPreparationMutex);
                    finish();
                }
            });
            work->completion = SubmitAssetWorkLocked(std::move(jobs), dependencies);
        }
        catch (const std::exception& exception)
        {
            CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Failed,
                AssetRequestError::SubmissionFailed, TextureDepotDiagnostic(exception.what()));
        }
        catch (...)
        {
            CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Failed,
                AssetRequestError::SubmissionFailed, TextureDepotDiagnostic("Texture job submission failed."));
        }
    }
    const auto root = selected.find(link.identity);
    if (root == selected.end())
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::NotMounted, "Texture root was not resolved.");
    }
    auto work = root->second;
    consumer->completion = work->completion;
    if (work->status == AssetRequestStatus::Pending)
    {
        try
        {
            work->consumers.emplace_back(consumer);
        }
        catch (...)
        {
            // The generation is already tracked and may finish for the cache;
            // this consumer could not register and receives a terminal result.
            return fail(AssetRequestStatus::Failed, AssetRequestError::SubmissionFailed, {});
        }
    }
    else
    {
        TextureDepotNotify(consumer, work->status, work->error, work->message, work->asset);
    }
    return request;
}

void DataSystem::RunTextureAssetWork(own::shared_owner<AssetDepot::TextureAssetWork> work)
{
    using namespace AssetDepot;
    TextureAssetRetiredEntries retired;
    try
    {
        auto origin = own::make_shared<TextureAssetOrigin>();
        origin->resolved = work->resolved;
        origin->variant = work->key.variant;
        {
            std::lock_guard preparationLock(m_assetPreparationMutex);
            if (work->status != AssetRequestStatus::Pending)
            {
                return;
            }
            if (m_assetPreparationStopping)
            {
                CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Cancelled,
                    AssetRequestError::ShuttingDown, "Texture job admission was stopped.");
                return;
            }
            if (m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != work->epoch)
            {
                CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Stale,
                    AssetRequestError::RevisionChanged, "Texture job epoch changed.");
                return;
            }
            for (const auto& dependency : work->dependencies)
            {
                if (dependency->status != AssetRequestStatus::Ready || !dependency->asset)
                {
                    const auto status = dependency->status == AssetRequestStatus::Cancelled
                        ? AssetRequestStatus::Cancelled
                        : (dependency->status == AssetRequestStatus::Stale
                            ? AssetRequestStatus::Stale : AssetRequestStatus::Failed);
                    CompleteTextureAssetWorkLocked(work, status, AssetRequestError::DependencyFailed,
                        "Texture hard dependency did not become CPU-ready.");
                    return;
                }
                origin->hardDependencies.push_back(dependency->asset);
                origin->hardDependencyChargeBytes = TextureDepotAddCharge(origin->hardDependencyChargeBytes,
                    TextureDepotCharge(dependency->asset));
            }
        }
        for (const auto& dependency : work->resolved.entry.dependencies)
        {
            if (dependency.kind == texture_cooked::AssetDependencyKind::Loadable)
            {
                origin->loadableDependencies.push_back(dependency.target);
            }
        }

        // The scheduler dependency already completed. No worker-to-worker wait.
        // Compatible descriptors share this verified source/path pair explicitly.
        std::string failure;
        AssetRequestError error = AssetRequestError::None;
        own::shared_owner<const Texture::CodecImage> image;
        {
            std::lock_guard preparationLock(m_assetPreparationMutex);
            if (!work->imageWork || work->imageWork->status != AssetRequestStatus::Ready
                || !work->imageWork->image || !work->imageWork->source)
            {
                error = work->imageWork ? work->imageWork->error : AssetRequestError::DependencyFailed;
                failure = work->imageWork ? work->imageWork->message : "Texture image dependency is missing.";
            }
            else
            {
                image = work->imageWork->image;
                origin->imageKey = work->imageWork->key;
                origin->imageSource = work->imageWork->source;
            }
        }
        origin->resolved.byteSource.reset();
        own::shared_owner<const Texture> candidate;
        if (error == AssetRequestError::None)
        {
            candidate = Texture::CreateOwnedDescriptor(image, std::move(origin), failure);
            if (!candidate)
            {
                error = AssetRequestError::DecodeFailed;
            }
        }
        {
            std::lock_guard preparationLock(m_assetPreparationMutex);
            if (candidate && error == AssetRequestError::None
                && work->status == AssetRequestStatus::Pending && !m_assetPreparationStopping
                && m_assetInvalidationDepth == 0u && m_assetPreparationEpoch == work->epoch)
            {
                // Allocation precedes every retention change. Evicted SDK/source
                // owners stay in this worker-local stage until after unlock.
                retired.descriptorPins.reserve(m_textureAssets.entries.size());
                const auto incoming = TextureDepotCharge(candidate);
                if (incoming <= m_textureAssets.budgetBytes)
                {
                    while (m_textureAssets.retainedChargeBytes > m_textureAssets.budgetBytes - incoming)
                    {
                        auto oldest = m_textureAssets.entries.end();
                        for (auto entry = m_textureAssets.entries.begin(); entry != m_textureAssets.entries.end(); ++entry)
                        {
                            if (entry->second.retained && (oldest == m_textureAssets.entries.end()
                                || entry->second.lastUse < oldest->second.lastUse))
                            {
                                oldest = entry;
                            }
                        }
                        if (oldest == m_textureAssets.entries.end())
                        {
                            break;
                        }
                        m_textureAssets.retainedChargeBytes -= oldest->second.retainedCharge;
                        oldest->second.retainedCharge = 0u;
                        retired.descriptorPins.push_back(std::move(oldest->second.retained));
                        ++m_textureAssets.logicalEvictions;
                    }
                }
            }
            CompleteTextureAssetWorkLocked(work,
                error == AssetRequestError::None ? AssetRequestStatus::Ready : AssetRequestStatus::Failed,
                error, std::move(failure), candidate);
        }
        // Candidate/source byte scratch and remaining local pins release outside
        // the publication lock. A stale/cancelled job never retries a new revision.
    }
    catch (const std::exception& exception)
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Failed,
            AssetRequestError::DecodeFailed, TextureDepotDiagnostic(exception.what()));
    }
    catch (...)
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        CompleteTextureAssetWorkLocked(work, AssetRequestStatus::Failed,
            AssetRequestError::DecodeFailed, TextureDepotDiagnostic("Texture worker failed."));
    }
}

void DataSystem::CompleteTextureAssetWorkLocked(
    const own::shared_owner<AssetDepot::TextureAssetWork>& work,
    AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
    std::string message, own::shared_owner<const Texture> texture)
{
    using namespace AssetDepot;
    if (work->status != AssetRequestStatus::Pending)
    {
        return;
    }
    auto found = m_textureAssets.entries.find(work->key);
    bool current = false;
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        // An immutable resolver revision identifies the exact metadata mapping;
        // the map key and requestId below identify this exact resolved generation.
        // Avoid allocating another metadata copy in this terminal/error path.
        current = m_cookedCatalog
            && m_cookedCatalog->ResolverRevision() == work->key.resolverRevision;
    }
    if (m_assetPreparationStopping)
    {
        status = AssetRequestStatus::Cancelled;
        error = AssetRequestError::ShuttingDown;
        message.clear();
    }
    else if (m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != work->epoch || !current
        || found == m_textureAssets.entries.end() || !found->second.inFlight
        || found->second.inFlight->requestId != work->requestId)
    {
        status = AssetRequestStatus::Stale;
        error = AssetRequestError::RevisionChanged;
        message.clear();
    }
    if (status != AssetRequestStatus::Ready)
    {
        texture.reset();
    }
    if (status == AssetRequestStatus::Ready && !texture)
    {
        status = AssetRequestStatus::Failed;
        error = AssetRequestError::DecodeFailed;
    }
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    work->asset = texture;
    if (found != m_textureAssets.entries.end() && found->second.inFlight
        && found->second.inFlight->requestId == work->requestId)
    {
        auto& entry = found->second;
        entry.inFlight.reset();
        if (texture)
        {
            entry.live = texture;
            entry.lastUse = ++m_textureAssets.clock;
            const auto charge = TextureDepotCharge(texture);
            // Failed candidates never replace an existing live/retained owner.
            // The arithmetic guard also admits explicit size_t-max budgets safely.
            if (charge <= m_textureAssets.budgetBytes
                && charge <= m_textureAssets.budgetBytes - m_textureAssets.retainedChargeBytes)
            {
                m_textureAssets.retainedChargeBytes -= entry.retainedCharge;
                entry.retained = texture;
                entry.retainedCharge = charge;
                m_textureAssets.retainedChargeBytes += charge;
            }
        }
    }
    for (const auto& weakConsumer : work->consumers)
    {
        if (const auto consumer = weakConsumer.lock())
        {
            TextureDepotNotify(consumer, status, error, work->message, texture);
        }
    }
    work->consumers.clear();
}

void DataSystem::TrimTextureAssetsLocked(AssetDepot::TextureAssetRetiredEntries& retired)
{
    while (m_textureAssets.retainedChargeBytes > m_textureAssets.budgetBytes)
    {
        auto oldest = m_textureAssets.entries.end();
        for (auto entry = m_textureAssets.entries.begin(); entry != m_textureAssets.entries.end(); ++entry)
        {
            if (entry->second.retained && (oldest == m_textureAssets.entries.end()
                || entry->second.lastUse < oldest->second.lastUse))
            {
                oldest = entry;
            }
        }
        if (oldest == m_textureAssets.entries.end())
        {
            break;
        }
        m_textureAssets.retainedChargeBytes -= oldest->second.retainedCharge;
        oldest->second.retainedCharge = 0u;
        // Logical eviction drops only the cache pin; consumer/job/child pins
        // remain valid. Native CPU deallocation is not a GPU retirement signal.
        retired.descriptorPins.push_back(std::move(oldest->second.retained));
        ++m_textureAssets.logicalEvictions;
    }
    while (m_textureAssets.retainedImageBytes > m_textureAssets.imageBudgetBytes)
    {
        auto oldest = m_textureAssets.images.end();
        for (auto entry = m_textureAssets.images.begin(); entry != m_textureAssets.images.end(); ++entry)
        {
            if (entry->second.retained && (oldest == m_textureAssets.images.end()
                || entry->second.lastUse < oldest->second.lastUse))
            {
                oldest = entry;
            }
        }
        if (oldest == m_textureAssets.images.end())
        {
            break;
        }
        m_textureAssets.retainedImageBytes -= oldest->second.retainedCharge;
        oldest->second.retainedCharge = 0u;
        retired.imagePins.push_back(std::move(oldest->second.retained));
        ++m_textureAssets.imageEvictions;
    }
    for (auto entry = m_textureAssets.entries.begin(); entry != m_textureAssets.entries.end();)
    {
        if (!entry->second.retained && !entry->second.inFlight && entry->second.live.expired())
        {
            retired.entries.insert(m_textureAssets.entries.extract(entry++));
        }
        else
        {
            ++entry;
        }
    }
    for (auto entry = m_textureAssets.images.begin(); entry != m_textureAssets.images.end();)
    {
        if (!entry->second.retained && !entry->second.inFlight && entry->second.live.expired())
        {
            retired.images.insert(m_textureAssets.images.extract(entry++));
        }
        else
        {
            ++entry;
        }
    }
}

void DataSystem::StageTextureAssetRetirementLocked(AssetDepot::TextureAssetRetiredEntries& retired)
{
    assert(retired.entries.empty() && retired.consumerPins.empty() && retired.images.empty());
    retired.descriptorPins.reserve(m_textureAssets.entries.size());
    retired.imagePins.reserve(m_textureAssets.images.size());
    if (m_assetPreparationStopping)
    {
        std::size_t imageCount{};
        for (const auto& [key, entry] : m_textureAssets.images)
        {
            if (entry.inFlight)
            {
                if (entry.inFlight->consumers.size() > retired.imageConsumers.max_size() - imageCount)
                {
                    throw std::length_error("Image retirement consumer capacity exceeded.");
                }
                imageCount += entry.inFlight->consumers.size();
            }
        }
        retired.imageConsumers.reserve(imageCount);
        for (const auto& [key, entry] : m_textureAssets.images)
        {
            if (entry.inFlight)
            {
                for (const auto& weak : entry.inFlight->consumers)
                {
                    if (auto consumer = weak.lock())
                    {
                        retired.imageConsumers.push_back(std::move(consumer));
                    }
                }
            }
        }
    }
    std::size_t count{};
    for (const auto& [key, entry] : m_textureAssets.entries)
    {
        if (entry.inFlight && entry.inFlight->status == AssetDepot::AssetRequestStatus::Pending)
        {
            if (entry.inFlight->consumers.size() > retired.consumerPins.max_size() - count)
            {
                throw std::length_error("Texture retirement consumer capacity exceeded.");
            }
            count += entry.inFlight->consumers.size();
        }
    }
    retired.consumerPins.reserve(count);
    for (const auto& [key, entry] : m_textureAssets.entries)
    {
        if (entry.inFlight && entry.inFlight->status == AssetDepot::AssetRequestStatus::Pending)
        {
            for (const auto& weak : entry.inFlight->consumers)
            {
                if (auto consumer = weak.lock())
                {
                    retired.consumerPins.push_back(std::move(consumer));
                }
            }
        }
    }
}

void DataSystem::InvalidateTextureAssetsLocked(AssetDepot::TextureAssetRetiredEntries& retired) noexcept
{
    using namespace AssetDepot;
    assert(retired.entries.empty());
    static_assert(noexcept(retired.entries.swap(m_textureAssets.entries)));
    retired.entries.swap(m_textureAssets.entries);
    m_textureAssets.retainedChargeBytes = 0u;
    const auto status = m_assetPreparationStopping
        ? AssetRequestStatus::Cancelled : AssetRequestStatus::Stale;
    const auto error = m_assetPreparationStopping
        ? AssetRequestError::ShuttingDown : AssetRequestError::RevisionChanged;
    for (auto& [key, entry] : retired.entries)
    {
        if (entry.retained)
        {
            ++m_textureAssets.logicalEvictions;
        }
        if (entry.inFlight && entry.inFlight->status == AssetRequestStatus::Pending)
        {
            // Captured jobs retain work and sources. Keep their weak subscriber
            // lists intact until the detached work is destroyed after outer unlock.
            entry.inFlight->status = status;
            entry.inFlight->error = error;
            entry.inFlight->message.clear();
        }
    }
    // Image keys identify exact bytes and recipes, so resolver replacement does
    // not cancel admitted rehydration. Shutdown detaches these jobs and cache pins.
    if (m_assetPreparationStopping)
    {
        retired.images.swap(m_textureAssets.images);
        m_textureAssets.retainedImageBytes = 0u;
        for (auto& [key, entry] : retired.images)
        {
            if (entry.retained)
            {
                ++m_textureAssets.imageEvictions;
            }
            if (entry.inFlight && entry.inFlight->status == AssetRequestStatus::Pending)
            {
                entry.inFlight->status = AssetRequestStatus::Cancelled;
                entry.inFlight->error = AssetRequestError::ShuttingDown;
            }
        }
        for (const auto& consumer : retired.imageConsumers)
        {
            TextureDepotNotify(consumer, AssetRequestStatus::Cancelled,
                AssetRequestError::ShuttingDown, {}, {});
        }
    }
    for (const auto& consumer : retired.consumerPins)
    {
        TextureDepotNotify(consumer, status, error, {}, {});
    }
}

void DataSystem::SetTextureAssetCacheBudget(std::size_t bytes)
{
    AssetDepot::TextureAssetRetiredEntries retired;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        // Stage all allocating retirement storage before changing the budget.
        retired.descriptorPins.reserve(m_textureAssets.entries.size());
        retired.imagePins.reserve(m_textureAssets.images.size());
        m_textureAssets.budgetBytes = bytes;
        TrimTextureAssetsLocked(retired);
    }
}

AssetDepot::TextureAssetCacheSnapshot DataSystem::SnapshotTextureAssetCache() const
{
    std::lock_guard lock(m_assetPreparationMutex);
    AssetDepot::TextureAssetCacheSnapshot snapshot;
    snapshot.entries = m_textureAssets.entries.size();
    snapshot.retainedChargeBytes = m_textureAssets.retainedChargeBytes;
    snapshot.budgetBytes = m_textureAssets.budgetBytes;
    snapshot.logicalEvictions = m_textureAssets.logicalEvictions;
    for (const auto& [key, entry] : m_textureAssets.entries)
    {
        snapshot.retainedEntries += entry.retained ? 1u : 0u;
        snapshot.liveEntries += !entry.live.expired() ? 1u : 0u;
        snapshot.inFlight += entry.inFlight ? 1u : 0u;
    }
    return snapshot;
}

namespace AssetDepot
{
    TextureImageKey MakeTextureImageKey(const experiment::cooked::AssetBlobRecord& blob,
        const TextureAssetVariant& variant)
    {
        // The artifact already embodies all byte-affecting import settings.
        // Role/sampler labels and obsolete runtime processing hints do not split
        // identical payloads; distinct recipes are represented by distinct blobs.
        (void)variant;
        TextureImageRecipe recipe;
        recipe.version = 2u;
        return { blob.contentSha256, blob.byteSize, blob.kind, blob.representation,
            blob.schemaVersion, blob.targetPlatform, blob.targetAbi, recipe };
    }
}

namespace
{
    thread_local const AssetDepot::TextureImageWork* TextureImageSubmittingWork{};

    class TextureImageSubmissionScope final
    {
    public:
        explicit TextureImageSubmissionScope(const AssetDepot::TextureImageWork& work)
            : previous_(std::exchange(TextureImageSubmittingWork, &work))
        {
        }
        ~TextureImageSubmissionScope()
        {
            TextureImageSubmittingWork = previous_;
        }
    private:
        const AssetDepot::TextureImageWork* previous_;
    };
}

own::shared_owner<const Texture::CodecImage> DataSystem::TryAcquireTextureImage(
    const own::shared_owner<const Texture>& descriptor)
{
    if (!descriptor)
    {
        return {};
    }
    if (auto image = descriptor->NonRehydratableImage())
    {
        return image;
    }
    const auto origin = descriptor->GetAssetOrigin();
    if (!origin || !origin->imageSource)
    {
        return {};
    }
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping)
    {
        return {};
    }
    const auto found = m_textureAssets.images.find(origin->imageKey);
    if (found == m_textureAssets.images.end())
    {
        return {};
    }
    auto image = found->second.retained ? found->second.retained : found->second.live.lock();
    if (image)
    {
        found->second.lastUse = ++m_textureAssets.clock;
    }
    return image;
}

AssetDepot::AssetRequest<Texture::CodecImage> DataSystem::RequestTextureImageAsync(
    const own::shared_owner<const Texture>& descriptor)
{
    using namespace AssetDepot;
    auto consumer = own::make_shared<AssetRequestState<Texture::CodecImage>>(m_assetRequestCounters);
    AssetRequest<Texture::CodecImage> request(consumer);
    if (!descriptor)
    {
        TextureDepotNotify(consumer, AssetRequestStatus::Failed, AssetRequestError::InvalidLink, {}, {});
        return request;
    }
    // Legacy and generated textures have an explicit lifetime-bound payload.
    // Do not invent an exact origin from a mutable file path.
    if (auto image = descriptor->NonRehydratableImage())
    {
        TextureDepotNotify(consumer, AssetRequestStatus::Ready, AssetRequestError::None, {}, image);
        return request;
    }
    const auto origin = descriptor->GetAssetOrigin();
    if (!origin || !origin->imageSource)
    {
        TextureDepotNotify(consumer, AssetRequestStatus::Failed, AssetRequestError::ReadFailed,
            "Texture has no durable exact image source.", {});
        return request;
    }
    own::shared_owner<TextureImageWork> work;
    TextureAssetRetiredEntries retired;
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping)
    {
        TextureDepotNotify(consumer, AssetRequestStatus::Cancelled, AssetRequestError::ShuttingDown, {}, {});
        return request;
    }
    try
    {
        retired.descriptorPins.reserve(m_textureAssets.entries.size());
        retired.imagePins.reserve(m_textureAssets.images.size());
        m_textureAssets.imageRequests.reserve(m_textureAssets.imageRequests.size() + 1u);
        TrimTextureAssetsLocked(retired);
        std::erase_if(m_textureAssets.imageRequests, [](const auto& weak) { return weak.expired(); });
        m_textureAssets.imageRequests.emplace_back(consumer);
        work = StartTextureImageWorkLocked(origin->imageKey,
            origin->resolved, origin->imageSource);
        consumer->completion = work->completion;
        if (work->status == AssetRequestStatus::Pending)
        {
            work->consumers.emplace_back(consumer);
        }
        else
        {
            TextureDepotNotify(consumer, work->status, work->error, work->message, work->image);
        }
    }
    catch (...)
    {
        TextureDepotNotify(consumer, AssetRequestStatus::Failed, AssetRequestError::SubmissionFailed, {}, {});
    }
    return request;
}

own::shared_owner<AssetDepot::TextureImageWork> DataSystem::StartTextureImageWorkLocked(
    const AssetDepot::TextureImageKey& key, const texture_cooked::ResolvedAssetEntry& resolved,
    own::shared_owner<const AssetDepot::TextureImageSource> source)
{
    using namespace AssetDepot;
    const auto found = m_textureAssets.images.find(key);
    if (found != m_textureAssets.images.end() && found->second.inFlight)
    {
        return found->second.inFlight;
    }
    auto work = own::make_shared<TextureImageWork>();
    work->key = key;
    work->resolved = resolved;
    work->source = std::move(source);
    if (found != m_textureAssets.images.end())
    {
        if (found->second.source)
        {
            work->source = found->second.source;
        }
        auto resident = found->second.retained ? found->second.retained : found->second.live.lock();
        if (resident && found->second.source)
        {
            work->image = std::move(resident);
            work->source = found->second.source;
            work->status = AssetRequestStatus::Ready;
            found->second.lastUse = ++m_textureAssets.clock;
            return work;
        }
    }
    if (m_textureAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
    {
        throw std::length_error("Image request identity space exhausted.");
    }
    // The complete work object and map node exist before the Pending job is
    // visible. Accepted captures are then registered in the service drain list.
    TextureImageEntries staged;
    if (found == m_textureAssets.images.end())
    {
        staged.try_emplace(key);
    }
    if (!staged.empty())
    {
        m_textureAssets.images.insert(staged.extract(staged.begin()));
    }
    auto& entry = m_textureAssets.images.find(key)->second;
    work->requestId = m_textureAssets.nextRequestId++;
    entry.inFlight = work;
    entry.lastUse = ++m_textureAssets.clock;
    TextureImageSubmissionScope submission(*work);
    try
    {
        job_group jobs;
        jobs.add([this, work]() { RunTextureImageWork(work); });
        jobs.on_complete([this, work](std::exception_ptr error)
        {
            const auto finish = [&]()
            {
                CompleteTextureImageWorkLocked(work, AssetRequestStatus::Failed,
                    error ? AssetRequestError::SubmissionFailed : AssetRequestError::DecodeFailed);
            };
            if (TextureImageSubmittingWork == &*work)
            {
                finish();
            }
            else
            {
                std::lock_guard lock(m_assetPreparationMutex);
                finish();
            }
        });
        work->completion = SubmitAssetWorkLocked(std::move(jobs), {}, true);
    }
    catch (...)
    {
        CompleteTextureImageWorkLocked(work, AssetRequestStatus::Failed, AssetRequestError::SubmissionFailed);
    }
    return work;
}

void DataSystem::RunTextureImageWork(own::shared_owner<AssetDepot::TextureImageWork> work)
{
    using namespace AssetDepot;
    TextureAssetRetiredEntries retired;
    try
    {
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (work->status != AssetRequestStatus::Pending)
            {
                return;
            }
            if (m_assetPreparationStopping)
            {
                CompleteTextureImageWorkLocked(work, AssetRequestStatus::Cancelled, AssetRequestError::ShuttingDown);
                return;
            }
        }
        auto captured = own::make_shared<TextureImageSource>();
        captured->byteSource = work->source ? work->source->byteSource : work->resolved.byteSource;
        captured->artifactPath = work->source ? work->source->artifactPath : work->resolved.blob.artifactPath;
        std::string failure;
        std::uint64_t byteSize{};
        constexpr std::uint64_t maxSourceBytes = texture_cooked::kCookedTextureMaxBytes;
        AssetRequestError error = AssetRequestError::None;
        if (!texture_cooked::CaptureArtifactSource(captured->byteSource, captured->artifactPath, failure)
            || !captured->byteSource->Size(captured->artifactPath, byteSize, failure))
        {
            error = AssetRequestError::ReadFailed;
        }
        else if (byteSize != work->key.byteSize || byteSize == 0u || byteSize > maxSourceBytes
            || byteSize > (std::numeric_limits<std::size_t>::max)())
        {
            error = AssetRequestError::IntegrityFailed;
            failure = "Texture image size does not match the bounded exact source record.";
        }
        AssetByteObservation inputObservation(m_textureAssets.imageInputStagingBytes);
        AssetByteObservation resultObservation(m_textureAssets.imageDecodedResultStagingBytes);
        std::vector<std::byte> bytes;
        if (error == AssetRequestError::None)
        {
            bytes.resize(static_cast<std::size_t>(byteSize));
            inputObservation.Set(bytes.capacity());
            if (!captured->byteSource->ReadAt(captured->artifactPath, 0u, bytes, failure))
            {
                error = AssetRequestError::ReadFailed;
            }
        }
        if (error == AssetRequestError::None)
        {
            texture_cooked::Sha256Digest digest{};
            if (!texture_cooked::ComputeSha256(bytes, digest, failure) || digest != work->key.contentSha256)
            {
                error = AssetRequestError::IntegrityFailed;
                failure = "Texture image SHA-256 does not match the exact source record.";
            }
        }
        own::shared_owner<const Texture::CodecImage> image;
        if (error == AssetRequestError::None)
        {
            image = Texture::DecodeOwnedImage(bytes, work->key, failure);
            resultObservation.Set(Texture::ImageByteSize(image));
            if (!image)
            {
                error = AssetRequestError::DecodeFailed;
            }
        }
        own::shared_owner<const TextureImageSource> source = work->source
            ? work->source : own::shared_owner<const TextureImageSource>(std::move(captured));
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (image && error == AssetRequestError::None
                && work->status == AssetRequestStatus::Pending && !m_assetPreparationStopping)
            {
                // Allocation precedes every retention change. Evicted SDK/source
                // owners stay in this worker-local stage until after unlock.
                retired.imagePins.reserve(m_textureAssets.images.size());
                const auto incoming = TextureImageCharge(image, source);
                if (incoming <= m_textureAssets.imageBudgetBytes)
                {
                    while (m_textureAssets.retainedImageBytes > m_textureAssets.imageBudgetBytes - incoming)
                    {
                        auto oldest = m_textureAssets.images.end();
                        for (auto entry = m_textureAssets.images.begin(); entry != m_textureAssets.images.end(); ++entry)
                        {
                            if (entry->second.retained && (oldest == m_textureAssets.images.end()
                                || entry->second.lastUse < oldest->second.lastUse))
                            {
                                oldest = entry;
                            }
                        }
                        if (oldest == m_textureAssets.images.end())
                        {
                            break;
                        }
                        m_textureAssets.retainedImageBytes -= oldest->second.retainedCharge;
                        oldest->second.retainedCharge = 0u;
                        retired.imagePins.push_back(std::move(oldest->second.retained));
                        ++m_textureAssets.imageEvictions;
                    }
                }
            }
            CompleteTextureImageWorkLocked(work,
                error == AssetRequestError::None ? AssetRequestStatus::Ready : AssetRequestStatus::Failed,
                error, std::move(failure), image, source);
        }
        // Exact source and SDK arrays are released outside the outer lock.
    }
    catch (const std::exception& exception)
    {
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteTextureImageWorkLocked(work, AssetRequestStatus::Failed, AssetRequestError::DecodeFailed,
            TextureDepotDiagnostic(exception.what()));
    }
    catch (...)
    {
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteTextureImageWorkLocked(work, AssetRequestStatus::Failed, AssetRequestError::DecodeFailed);
    }
}

void DataSystem::CompleteTextureImageWorkLocked(
    const own::shared_owner<AssetDepot::TextureImageWork>& work,
    AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
    std::string message, own::shared_owner<const Texture::CodecImage> image,
    own::shared_owner<const AssetDepot::TextureImageSource> source)
{
    using namespace AssetDepot;
    if (work->status != AssetRequestStatus::Pending)
    {
        return;
    }
    const auto found = m_textureAssets.images.find(work->key);
    if (m_assetPreparationStopping)
    {
        status = AssetRequestStatus::Cancelled;
        error = AssetRequestError::ShuttingDown;
    }
    if (found == m_textureAssets.images.end() || !found->second.inFlight
        || found->second.inFlight->requestId != work->requestId)
    {
        status = AssetRequestStatus::Cancelled;
        error = AssetRequestError::ShuttingDown;
    }
    if (status == AssetRequestStatus::Ready && (!image || !source))
    {
        status = AssetRequestStatus::Failed;
        error = AssetRequestError::DecodeFailed;
    }
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    if (status == AssetRequestStatus::Ready)
    {
        work->image = image;
        work->source = source;
        auto& entry = found->second;
        entry.live = image;
        entry.source = source;
        entry.lastUse = ++m_textureAssets.clock;
        const auto bytes = TextureImageCharge(image, source);
        if (bytes <= m_textureAssets.imageBudgetBytes
            && bytes <= m_textureAssets.imageBudgetBytes - m_textureAssets.retainedImageBytes)
        {
            entry.retained = image;
            entry.retainedCharge = bytes;
            m_textureAssets.retainedImageBytes += bytes;
        }
    }
    if (found != m_textureAssets.images.end() && found->second.inFlight
        && found->second.inFlight->requestId == work->requestId)
    {
        found->second.inFlight.reset();
    }
    for (const auto& weak : work->consumers)
    {
        if (const auto consumer = weak.lock())
        {
            TextureDepotNotify(consumer, status, error, work->message, work->image);
        }
    }
    work->consumers.clear();
}

void DataSystem::SetTextureImageCacheBudget(std::size_t bytes)
{
    AssetDepot::TextureAssetRetiredEntries retired;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        retired.descriptorPins.reserve(m_textureAssets.entries.size());
        retired.imagePins.reserve(m_textureAssets.images.size());
        m_textureAssets.imageBudgetBytes = bytes;
        TrimTextureAssetsLocked(retired);
    }
}

AssetDepot::TextureImageCacheSnapshot DataSystem::SnapshotTextureImageCache() const
{
    std::vector<own::shared_owner<AssetDepot::AssetRequestState<Texture::CodecImage>>> requestPins;
    std::lock_guard lock(m_assetPreparationMutex);
    requestPins.reserve(m_textureAssets.imageRequests.size());
    for (const auto& weak : m_textureAssets.imageRequests)
    {
        if (auto request = weak.lock())
        {
            requestPins.push_back(std::move(request));
        }
    }
    AssetDepot::TextureImageCacheSnapshot result;
    result.entries = m_textureAssets.images.size();
    result.retainedChargeBytes = m_textureAssets.retainedImageBytes;
    result.budgetBytes = m_textureAssets.imageBudgetBytes;
    result.logicalEvictions = m_textureAssets.imageEvictions;
    const auto live = Texture::SnapshotImageMemory();
    result.liveBytes = live.liveBytes;
    result.nonRehydratableBytes = live.nonRehydratableBytes;
    result.livePayloads = live.livePayloads;
    result.inputStagingBytes = m_textureAssets.imageInputStagingBytes.load(std::memory_order_relaxed);
    result.decodedResultStagingBytes = m_textureAssets.imageDecodedResultStagingBytes.load(std::memory_order_relaxed);
    for (const auto& [key, entry] : m_textureAssets.images)
    {
        result.retainedEntries += entry.retained ? 1u : 0u;
        result.inFlight += entry.inFlight ? 1u : 0u;
        if (entry.inFlight)
        {
            result.inFlightResultBytes += Texture::ImageByteSize(entry.inFlight->image);
        }
    }
    for (const auto& request : requestPins)
    {
        std::lock_guard requestLock(request->mutex);
        result.inFlightResultBytes = TextureDepotAddCharge(result.inFlightResultBytes,
            Texture::ImageByteSize(request->asset));
    }
    return result;
}
