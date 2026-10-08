#include "MaterialAssetRuntime.h"
#include "../DataSystem.h"
#include "../Experiment/Cooked/CookedShaderMeta.h"

#include <algorithm>
#include <cassert>
#include <exception>
#include <limits>
#include <stdexcept>
#include <utility>

namespace
{
    namespace material_cooked = experiment::cooked;
    using Status = AssetDepot::AssetRequestStatus;
    using Error = AssetDepot::AssetRequestError;

    // Submission context only. The monotonic value is not resource identity;
    // it detects a scheduler terminal observer invoked inline under admission.
    thread_local std::uint64_t ShaderMetaSubmittingRequestId{};

    class ShaderMetaSubmissionScope final
    {
    public:
        explicit ShaderMetaSubmissionScope(std::uint64_t requestId)
            : m_previous(std::exchange(ShaderMetaSubmittingRequestId, requestId))
        {
        }
        ~ShaderMetaSubmissionScope()
        {
            ShaderMetaSubmittingRequestId = m_previous;
        }
        ShaderMetaSubmissionScope(const ShaderMetaSubmissionScope&) = delete;
        ShaderMetaSubmissionScope& operator=(const ShaderMetaSubmissionScope&) = delete;

    private:
        std::uint64_t m_previous{};
    };

    void NotifyShaderMetaConsumer(
        const own::shared_owner<AssetDepot::AssetRequestState<ShaderMeta>>& consumer,
        Status status, Error error, const std::string& message,
        const own::shared_owner<const ShaderMeta>& asset = {})
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != Status::Pending)
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
            consumer->message.clear();
        }
    }

    AssetDepot::ShaderMetaAssetKey ShaderMetaKey(const material_cooked::ResolvedAssetEntry& resolved)
    {
        return { resolved.entry.asset, resolved.blob, resolved.resolverRevision };
    }

    bool SupportedShaderMetaRepresentation(const material_cooked::ResolvedAssetEntry& resolved)
    {
        return resolved.entry.asset.kind == material_cooked::CookedAssetKind::ShaderMeta
            && resolved.blob.kind == material_cooked::CookedAssetKind::ShaderMeta
            && resolved.blob.representation == material_cooked::kShaderMetaDocumentRepresentation
            && resolved.blob.schemaVersion == material_cooked::kShaderMetaDocumentVersion
            && resolved.byteSource;
    }

    bool HasShaderMetaHardDependency(const material_cooked::ResolvedAssetEntry& resolved)
    {
        return std::any_of(resolved.entry.dependencies.begin(), resolved.entry.dependencies.end(),
            [](const auto& dependency) { return dependency.kind == material_cooked::AssetDependencyKind::Hard; });
    }

    bool MatchesShaderMeta(const ShaderMeta& asset, const AssetDepot::ShaderMetaAssetKey& key)
    {
        const auto& origin = asset.assetOrigin;
        return origin && origin->resolved.entry.asset == key.asset
            && origin->resolved.blob == key.blob
            && origin->resolved.resolverRevision == key.resolverRevision;
    }

    class ShaderMetaAssetCharge final
    {
    public:
        void Add(std::size_t bytes) noexcept
        {
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            m_bytes = bytes > maximum - m_bytes ? maximum : m_bytes + bytes;
        }
        void String(const std::string& value) noexcept
        {
            Add(value.capacity());
            Add(1u);
        }
        void Path(const std::filesystem::path& value) noexcept
        {
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            const auto count = value.native().capacity();
            Add(count > maximum / sizeof(std::filesystem::path::value_type)
                ? maximum : count * sizeof(std::filesystem::path::value_type));
        }
        template<class T>
        void Vector(const std::vector<T>& values) noexcept
        {
            const auto maximum = (std::numeric_limits<std::size_t>::max)();
            Add(values.capacity() > maximum / sizeof(T) ? maximum : values.capacity() * sizeof(T));
        }
        std::size_t Bytes() const noexcept { return m_bytes; }

    private:
        std::size_t m_bytes{};
    };

    std::size_t ShaderMetaChargeBytes(const ShaderMeta& meta) noexcept
    {
        ShaderMetaAssetCharge charge;
        charge.Add(sizeof(ShaderMeta));
        charge.String(meta.name);
        charge.Path(meta.source);
        charge.Path(meta.originPath);
        charge.Vector(meta.properties);
        for (const auto& property : meta.properties)
        {
            charge.String(property.name);
            charge.String(property.label);
            charge.String(property.semantic);
            charge.String(property.colorSpace);
        }
        charge.Vector(meta.keywords);
        for (const auto& keyword : meta.keywords)
        {
            charge.String(keyword.name);
            charge.Vector(keyword.values);
            for (const auto& value : keyword.values)
            {
                charge.String(value);
            }
        }
        charge.Vector(meta.passes);
        for (const auto& pass : meta.passes)
        {
            charge.String(pass.name);
            if (pass.vertex)
            {
                charge.String(pass.vertex->entry);
            }
            if (pass.pixel)
            {
                charge.String(pass.pixel->entry);
            }
            if (pass.compute)
            {
                charge.String(pass.compute->entry);
            }
        }
        // The cooked leaf rejects generated metadata before publication.
        if (meta.assetOrigin)
        {
            charge.Add(sizeof(AssetDepot::ShaderMetaAssetOrigin));
            charge.Vector(meta.assetOrigin->resolved.entry.dependencies);
            charge.String(meta.assetOrigin->resolved.blob.targetPlatform);
            charge.String(meta.assetOrigin->resolved.blob.targetAbi);
            charge.String(meta.assetOrigin->resolved.blob.artifactPath);
        }
        return charge.Bytes();
    }

    std::uint64_t NextShaderMetaUse(AssetDepot::MaterialAssetRuntimeState& state) noexcept
    {
        if (state.clock == (std::numeric_limits<std::uint64_t>::max)())
        {
            for (auto& [key, entry] : state.shaderMetadata)
            {
                entry.lastUse = 0u;
            }
            state.clock = 0u;
        }
        return ++state.clock;
    }

    Error ReadShaderMetaBytes(material_cooked::ResolvedAssetEntry& resolved,
        std::vector<std::byte>& bytes, std::string& failure)
    {
        // All backing capture, Size/ReadAt calls and hashing run in the tracked
        // job. Never read a source-registry path or retry with a newer resolver.
        if (resolved.blob.byteSize == 0u
            || resolved.blob.byteSize > material_cooked::kShaderMetaDocumentMaxBytes)
        {
            failure = "ShaderMeta artifact exceeds its bounded document size.";
            return Error::IntegrityFailed;
        }
        std::uint64_t size{};
        if (!material_cooked::CaptureArtifactSource(resolved.byteSource, resolved.blob.artifactPath, failure)
            || !resolved.byteSource->Size(resolved.blob.artifactPath, size, failure))
        {
            return Error::ReadFailed;
        }
        if (size != resolved.blob.byteSize)
        {
            failure = "ShaderMeta artifact size does not match its exact manifest record.";
            return Error::IntegrityFailed;
        }
        bytes.resize(static_cast<std::size_t>(size));
        if (!resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0u, bytes, failure))
        {
            return Error::ReadFailed;
        }
        material_cooked::Sha256Digest digest{};
        if (!material_cooked::ComputeSha256(bytes, digest, failure) || digest != resolved.blob.contentSha256)
        {
            failure = "ShaderMeta SHA-256 does not match the exact captured artifact.";
            return Error::IntegrityFailed;
        }
        return Error::None;
    }

    void PruneShaderMetaEntries(AssetDepot::MaterialAssetRuntimeState& state)
    {
        std::erase_if(state.shaderMetadata, [](const auto& item)
        {
            return !item.second.retained && !item.second.inFlight && item.second.live.expired();
        });
    }

    void TrimShaderMetaCache(AssetDepot::MaterialAssetRuntimeState& state,
        std::vector<own::shared_owner<const ShaderMeta>>& retired)
    {
        while (state.shaderMetaRetainedChargeBytes > state.shaderMetaBudgetBytes)
        {
            auto oldest = state.shaderMetadata.end();
            for (auto entry = state.shaderMetadata.begin(); entry != state.shaderMetadata.end(); ++entry)
            {
                if (entry->second.retained && (oldest == state.shaderMetadata.end()
                    || entry->second.lastUse < oldest->second.lastUse))
                {
                    oldest = entry;
                }
            }
            if (oldest == state.shaderMetadata.end())
            {
                break;
            }
            assert(retired.size() < retired.capacity());
            state.shaderMetaRetainedChargeBytes -= oldest->second.retainedCharge;
            oldest->second.retainedCharge = 0u;
            retired.push_back(std::move(oldest->second.retained));
            ++state.shaderMetaLogicalEvictions;
        }
        PruneShaderMetaEntries(state);
    }
}

own::shared_owner<const ShaderMeta> DataSystem::TryAcquireShaderMeta(AssetDepot::AssetLink<ShaderMeta> link)
{
    if (!link.IsValid() || link.identity.subassetId.IsValid()
        || !experiment::IsAssetIdV4(link.identity.assetId))
    {
        return {};
    }
    // Locals that can hold final source/descriptor pins outlive the lock.
    material_cooked::ResolvedAssetEntry resolved;
    own::shared_owner<const ShaderMeta> resident;
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
    {
        return {};
    }
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (!m_cookedCatalog || m_cookedCatalog->Find(link.ToReference(), resolved)
            != material_cooked::AssetLookupStatus::Found)
        {
            return {};
        }
    }
    const auto key = ShaderMetaKey(resolved);
    const auto found = m_materialAssets.shaderMetadata.find(key);
    if (found == m_materialAssets.shaderMetadata.end())
    {
        return {};
    }
    resident = found->second.retained ? found->second.retained : found->second.live.lock();
    if (!resident || !MatchesShaderMeta(*resident, key))
    {
        return {};
    }
    found->second.lastUse = NextShaderMetaUse(m_materialAssets);
    return resident;
}

AssetDepot::AssetRequest<ShaderMeta> DataSystem::RequestShaderMetaAsync(AssetDepot::AssetLink<ShaderMeta> link)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<ShaderMeta>>();
    AssetDepot::AssetRequest<ShaderMeta> request(consumer);
    const auto fail = [&](Status status, Error error, const std::string& message = {})
    {
        NotifyShaderMetaConsumer(consumer, status, error, message);
        return request;
    };
    if (!link.IsValid())
    {
        return fail(Status::Failed, Error::InvalidLink, "Invalid ShaderMeta link.");
    }
    if (link.identity.subassetId.IsValid() || !experiment::IsAssetIdV4(link.identity.assetId))
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation,
            "Authored ShaderMeta documents require a UUIDv4 root identity without a subasset ID.");
    }
    own::shared_owner<const material_cooked::CookedAssetCatalog> catalog;
    std::uint64_t epoch{};
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
        {
            return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
        }
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        catalog = m_cookedCatalog;
        epoch = m_assetPreparationEpoch;
    }
    if (!catalog)
    {
        return fail(Status::Failed, Error::NotMounted, "No asset catalog is mounted.");
    }
    material_cooked::ResolvedAssetEntry resolved;
    const auto lookup = catalog->Find(link.ToReference(), resolved);
    if (lookup != material_cooked::AssetLookupStatus::Found)
    {
        return fail(Status::Failed, lookup == material_cooked::AssetLookupStatus::TypeMismatch
            ? Error::TypeMismatch : Error::NotMounted, "ShaderMeta link is not present with the expected type.");
    }
    if (!SupportedShaderMetaRepresentation(resolved))
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation,
            "ShaderMeta requires the supported source-free cooked document representation.");
    }
    if (HasShaderMetaHardDependency(resolved))
    {
        return fail(Status::Failed, Error::DependencyFailed,
            "This ShaderMeta document loader does not support Hard dependencies.");
    }

    // Work/catalog/result owners are declared before admission so even inline
    // submission failure releases their final backing pins after outer unlock.
    own::shared_owner<AssetDepot::ShaderMetaAssetWork> work;
    own::shared_owner<const ShaderMeta> resident;
    const auto key = ShaderMetaKey(resolved);
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping)
    {
        return fail(Status::Cancelled, Error::ShuttingDown);
    }
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != epoch || !m_cookedCatalog
            || m_cookedCatalog->ResolverRevision() != resolved.resolverRevision)
        {
            return fail(Status::Stale, Error::RevisionChanged);
        }
    }
    try
    {
        PruneShaderMetaEntries(m_materialAssets);
        auto& entry = m_materialAssets.shaderMetadata[key];
        entry.lastUse = NextShaderMetaUse(m_materialAssets);
        if (entry.inFlight)
        {
            work = entry.inFlight;
            work->consumers.emplace_back(consumer);
            consumer->completion = work->completion;
            return request;
        }
        resident = entry.retained ? entry.retained : entry.live.lock();
        if (resident)
        {
            if (!MatchesShaderMeta(*resident, key))
            {
                return fail(Status::Failed, Error::IntegrityFailed,
                    "Resident ShaderMeta does not match its exact generation key.");
            }
            NotifyShaderMetaConsumer(consumer, Status::Ready, Error::None, {}, resident);
            return request;
        }
        if (m_materialAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
        {
            return fail(Status::Failed, Error::SubmissionFailed, "ShaderMeta request identity space exhausted.");
        }
        work = own::make_shared<AssetDepot::ShaderMetaAssetWork>();
        work->key = key;
        work->resolved = resolved;
        work->catalog = catalog;
        work->epoch = epoch;
        work->requestId = m_materialAssets.nextRequestId++;
        work->consumers.emplace_back(consumer);
        // Every potentially allocating registration above precedes publication.
        entry.inFlight = work;
        ShaderMetaSubmissionScope submitting(work->requestId);
        try
        {
            job_group jobs;
            jobs.add([this, work]() { RunShaderMetaAssetWork(work); });
            jobs.on_complete([this, work](std::exception_ptr failure)
            {
                const auto finish = [&]()
                {
                    // A skipped body must still terminalize joined consumers
                    // before readiness. A completed body makes this a no-op.
                    CompleteShaderMetaAssetWorkLocked(work, Status::Failed,
                        failure ? Error::SubmissionFailed : Error::DecodeFailed);
                };
                if (ShaderMetaSubmittingRequestId == work->requestId)
                {
                    finish();
                }
                else
                {
                    std::lock_guard completionLock(m_assetPreparationMutex);
                    finish();
                }
            });
            work->completion = SubmitAssetWorkLocked(std::move(jobs));
        }
        catch (...)
        {
            CompleteShaderMetaAssetWorkLocked(work, Status::Failed, Error::SubmissionFailed);
        }
        consumer->completion = work->completion;
    }
    catch (...)
    {
        return fail(Status::Failed, Error::SubmissionFailed);
    }
    return request;
}

void DataSystem::RunShaderMetaAssetWork(own::shared_owner<AssetDepot::ShaderMetaAssetWork> work)
{
    try
    {
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (work->status != Status::Pending)
            {
                return;
            }
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u
                || work->epoch != m_assetPreparationEpoch)
            {
                CompleteShaderMetaAssetWorkLocked(work,
                    m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                    m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
                return;
            }
        }
        auto resolved = work->resolved;
        std::vector<std::byte> bytes;
        std::string failure;
        auto error = ReadShaderMetaBytes(resolved, bytes, failure);
        own::shared_owner<const ShaderMeta> candidate;
        if (error == Error::None)
        {
            auto decoded = own::make_shared<ShaderMeta>();
            if (material_cooked::ReadShaderMetaArtifact(bytes, resolved.entry.asset.key.assetId,
                *decoded, failure))
            {
                auto origin = own::make_shared<AssetDepot::ShaderMetaAssetOrigin>();
                origin->resolved = std::move(resolved);
                decoded->assetOrigin = std::move(origin);
                candidate = std::move(decoded);
            }
            else
            {
                error = Error::DecodeFailed;
            }
        }
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteShaderMetaAssetWorkLocked(work, error == Error::None ? Status::Ready : Status::Failed,
            error, std::move(failure), candidate);
        // Rejected/stale candidates and exact source pins outlive this lock.
    }
    catch (...)
    {
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteShaderMetaAssetWorkLocked(work, Status::Failed, Error::DecodeFailed);
    }
}

void DataSystem::CompleteShaderMetaAssetWorkLocked(
    const own::shared_owner<AssetDepot::ShaderMetaAssetWork>& work,
    Status status, Error error, std::string message,
    const own::shared_owner<const ShaderMeta>& candidate)
{
    if (work->status != Status::Pending)
    {
        return;
    }
    const auto found = m_materialAssets.shaderMetadata.find(work->key);
    bool current{};
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        current = m_cookedCatalog && m_cookedCatalog->ResolverRevision() == work->key.resolverRevision;
    }
    if (m_assetPreparationStopping)
    {
        status = Status::Cancelled;
        error = Error::ShuttingDown;
        message.clear();
    }
    else if (!current || m_assetInvalidationDepth != 0u || m_assetPreparationEpoch != work->epoch
        || found == m_materialAssets.shaderMetadata.end() || !found->second.inFlight
        || found->second.inFlight->requestId != work->requestId)
    {
        status = Status::Stale;
        error = Error::RevisionChanged;
        message.clear();
    }
    if (status == Status::Ready && (!candidate || !MatchesShaderMeta(*candidate, work->key)))
    {
        status = Status::Failed;
        error = Error::DecodeFailed;
    }
    const auto asset = status == Status::Ready ? candidate : own::shared_owner<const ShaderMeta>{};
    const auto charge = asset ? ShaderMetaChargeBytes(*asset) : 0u;
    bool retainCandidate = asset && charge <= m_materialAssets.shaderMetaBudgetBytes
        && charge <= (std::numeric_limits<std::size_t>::max)() - m_materialAssets.shaderMetaRetainedChargeBytes;
    if (retainCandidate && m_materialAssets.shaderMetaRetainedChargeBytes
        > m_materialAssets.shaderMetaBudgetBytes - charge)
    {
        try
        {
            work->retiredAssets.reserve(m_materialAssets.shaderMetadata.size());
        }
        catch (...)
        {
            // Ready delivery does not require cache retention. Never evict
            // owners under the lock merely because retirement staging failed.
            retainCandidate = false;
        }
    }
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    work->asset = asset;
    if (found != m_materialAssets.shaderMetadata.end() && found->second.inFlight
        && found->second.inFlight->requestId == work->requestId)
    {
        auto& entry = found->second;
        entry.inFlight.reset();
        if (asset)
        {
            entry.live = asset;
            entry.lastUse = NextShaderMetaUse(m_materialAssets);
            if (retainCandidate)
            {
                // A new decode was admitted only after a resident miss.
                assert(!entry.retained && entry.retainedCharge == 0u);
                entry.retained = asset;
                entry.retainedCharge = charge;
                m_materialAssets.shaderMetaRetainedChargeBytes += charge;
            }
        }
    }
    for (const auto& weak : work->consumers)
    {
        if (const auto consumer = weak.lock())
        {
            NotifyShaderMetaConsumer(consumer, status, error, work->message, asset);
        }
    }
    work->consumers.clear();
    TrimShaderMetaCache(m_materialAssets, work->retiredAssets);
}

void DataSystem::StageMaterialAssetRetirementLocked(AssetDepot::MaterialAssetRetiredEntries& retired)
{
    assert(retired.shaderMetadata.empty() && retired.shaderMetaConsumers.empty());
    std::size_t count{};
    for (const auto& [key, entry] : m_materialAssets.shaderMetadata)
    {
        if (entry.inFlight && entry.inFlight->status == Status::Pending)
        {
            if (entry.inFlight->consumers.size() > retired.shaderMetaConsumers.max_size() - count)
            {
                throw std::length_error("ShaderMeta retirement consumer capacity exceeded.");
            }
            count += entry.inFlight->consumers.size();
        }
    }
    retired.shaderMetaConsumers.reserve(count);
    for (const auto& [key, entry] : m_materialAssets.shaderMetadata)
    {
        if (entry.inFlight && entry.inFlight->status == Status::Pending)
        {
            for (const auto& weak : entry.inFlight->consumers)
            {
                if (auto consumer = weak.lock())
                {
                    retired.shaderMetaConsumers.push_back(std::move(consumer));
                }
            }
        }
    }
}

void DataSystem::InvalidateMaterialAssetsLocked(AssetDepot::MaterialAssetRetiredEntries& retired) noexcept
{
    assert(retired.shaderMetadata.empty());
    static_assert(noexcept(retired.shaderMetadata.swap(m_materialAssets.shaderMetadata)));
    retired.shaderMetadata.swap(m_materialAssets.shaderMetadata);
    m_materialAssets.shaderMetaRetainedChargeBytes = 0u;
    const auto status = m_assetPreparationStopping ? Status::Cancelled : Status::Stale;
    const auto error = m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged;
    for (auto& [key, entry] : retired.shaderMetadata)
    {
        if (entry.retained)
        {
            ++m_materialAssets.shaderMetaLogicalEvictions;
        }
        if (entry.inFlight && entry.inFlight->status == Status::Pending)
        {
            entry.inFlight->status = status;
            entry.inFlight->error = error;
            entry.inFlight->message.clear();
        }
    }
    for (const auto& consumer : retired.shaderMetaConsumers)
    {
        NotifyShaderMetaConsumer(consumer, status, error, {});
    }
}

void DataSystem::SetShaderMetaAssetCacheBudget(std::size_t bytes)
{
    std::vector<own::shared_owner<const ShaderMeta>> retired;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        retired.reserve(m_materialAssets.shaderMetadata.size());
        m_materialAssets.shaderMetaBudgetBytes = bytes;
        TrimShaderMetaCache(m_materialAssets, retired);
    }
}

AssetDepot::ShaderMetaAssetCacheSnapshot DataSystem::SnapshotShaderMetaAssetCache() const
{
    std::lock_guard lock(m_assetPreparationMutex);
    AssetDepot::ShaderMetaAssetCacheSnapshot snapshot;
    snapshot.entries = m_materialAssets.shaderMetadata.size();
    snapshot.retainedChargeBytes = m_materialAssets.shaderMetaRetainedChargeBytes;
    snapshot.budgetBytes = m_materialAssets.shaderMetaBudgetBytes;
    snapshot.logicalEvictions = m_materialAssets.shaderMetaLogicalEvictions;
    for (const auto& [key, entry] : m_materialAssets.shaderMetadata)
    {
        snapshot.retainedEntries += entry.retained ? 1u : 0u;
        snapshot.liveEntries += entry.live.expired() ? 0u : 1u;
        snapshot.inFlight += entry.inFlight ? 1u : 0u;
    }
    return snapshot;
}
