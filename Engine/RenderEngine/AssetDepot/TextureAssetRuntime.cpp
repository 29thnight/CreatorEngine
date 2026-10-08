#include "TextureAssetRuntime.h"
#include "../DataSystem.h"
#include "../Texture.h"

#include <algorithm>
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
        return TextureDepotAddCharge(texture->DecodedByteSize(),
            origin ? origin->hardDependencyChargeBytes : 0u);
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

    void TextureDepotNotify(const own::shared_owner<AssetDepot::AssetRequestState<Texture>>& consumer,
        AssetDepot::AssetRequestStatus status, AssetDepot::AssetRequestError error,
        const std::string& message, const own::shared_owner<const Texture>& asset)
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != AssetDepot::AssetRequestStatus::Pending)
        {
            return;
        }
        consumer->status = status;
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
    using namespace AssetDepot;
    auto consumer = own::make_shared<AssetRequestState<Texture>>();
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
    if (variant.colorSpace != TextureAssetColorSpace::Source
        && variant.colorSpace != TextureAssetColorSpace::Linear
        && variant.colorSpace != TextureAssetColorSpace::Srgb)
    {
        return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedRepresentation,
            "Unknown texture color-space variant.");
    }
    own::shared_owner<const texture_cooked::CookedAssetCatalog> catalog;
    std::uint64_t epoch{};
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
        catalog = m_cookedCatalog;
        epoch = m_assetPreparationEpoch;
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
            || resolved.blob.representation != 1u
            || resolved.blob.schemaVersion != texture_cooked::kTextureArtifactVersion)
        {
            return fail(AssetRequestStatus::Failed, AssetRequestError::UnsupportedRepresentation,
                "Texture acquisition requires TextureSourceImage representation 1, schema 1.");
        }
    }

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

    TrimTextureAssetsLocked();
    std::map<texture_cooked::AssetIdentity, own::shared_owner<TextureAssetWork>> selected;
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

        // All source access and image decoding occur only in this tracked worker.
        // Read/hash exact captured locator bytes; never re-resolve the latest path.
        auto& resolved = origin->resolved;
        std::string failure;
        std::uint64_t byteSize{};
        constexpr std::uint64_t maxSourceImageBytes = 512ull * 1024ull * 1024ull;
        AssetRequestError error = AssetRequestError::None;
        if (!texture_cooked::CaptureArtifactSource(resolved.byteSource, resolved.blob.artifactPath, failure)
            || !resolved.byteSource->Size(resolved.blob.artifactPath, byteSize, failure))
        {
            error = AssetRequestError::ReadFailed;
        }
        else if (byteSize != resolved.blob.byteSize || byteSize == 0u
            || byteSize > maxSourceImageBytes
            || byteSize > (std::numeric_limits<std::size_t>::max)())
        {
            error = AssetRequestError::IntegrityFailed;
            failure = "Texture source-image size does not match its bounded manifest record.";
        }
        std::vector<std::byte> bytes;
        if (error == AssetRequestError::None)
        {
            bytes.resize(static_cast<std::size_t>(byteSize));
            if (!resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0u, bytes, failure))
            {
                error = AssetRequestError::ReadFailed;
            }
        }
        if (error == AssetRequestError::None)
        {
            texture_cooked::Sha256Digest digest{};
            if (!texture_cooked::ComputeSha256(bytes, digest, failure) || digest != resolved.blob.contentSha256)
            {
                error = AssetRequestError::IntegrityFailed;
                if (failure.empty())
                {
                    failure = "Texture source-image SHA-256 does not match the captured manifest.";
                }
            }
        }
        own::shared_owner<const Texture> candidate;
        if (error == AssetRequestError::None)
        {
            candidate = Texture::LoadOwnedFromMemory(bytes, work->key.variant, std::move(origin), failure);
            if (!candidate)
            {
                error = AssetRequestError::DecodeFailed;
            }
        }
        {
            std::lock_guard preparationLock(m_assetPreparationMutex);
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
                && charge <= (std::numeric_limits<std::size_t>::max)() - m_textureAssets.retainedChargeBytes)
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
    TrimTextureAssetsLocked();
}

void DataSystem::TrimTextureAssetsLocked()
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
        oldest->second.retained.reset();
        ++m_textureAssets.logicalEvictions;
    }
    std::erase_if(m_textureAssets.entries, [](const auto& value)
    {
        const auto& entry = value.second;
        return !entry.retained && !entry.inFlight && entry.live.expired();
    });
}

AssetDepot::TextureAssetEntries DataSystem::InvalidateTextureAssetsLocked()
{
    using namespace AssetDepot;
    TextureAssetEntries detached;
    detached.swap(m_textureAssets.entries);
    m_textureAssets.retainedChargeBytes = 0u;
    const auto status = m_assetPreparationStopping
        ? AssetRequestStatus::Cancelled : AssetRequestStatus::Stale;
    const auto error = m_assetPreparationStopping
        ? AssetRequestError::ShuttingDown : AssetRequestError::RevisionChanged;
    for (auto& [key, entry] : detached)
    {
        if (entry.retained)
        {
            ++m_textureAssets.logicalEvictions;
        }
        if (!entry.inFlight || entry.inFlight->status != AssetRequestStatus::Pending)
        {
            continue;
        }
        // Do not call the normal commit path here: lifecycle callers can already
        // hold the catalog lock. Captured jobs still own and drain this work.
        const auto& work = entry.inFlight;
        work->status = status;
        work->error = error;
        work->message.clear();
        for (const auto& weakConsumer : work->consumers)
        {
            if (const auto consumer = weakConsumer.lock())
            {
                TextureDepotNotify(consumer, status, error, {}, {});
            }
        }
        work->consumers.clear();
    }
    // Lifecycle caller keeps this return value outside both locks, so final
    // descriptor/source/codec destruction is not part of the mount transaction.
    return detached;
}

void DataSystem::SetTextureAssetCacheBudget(std::size_t bytes)
{
    std::lock_guard lock(m_assetPreparationMutex);
    m_textureAssets.budgetBytes = bytes;
    TrimTextureAssetsLocked();
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
        snapshot.liveEntries += entry.live.lock() ? 1u : 0u;
        snapshot.inFlight += entry.inFlight ? 1u : 0u;
    }
    return snapshot;
}
