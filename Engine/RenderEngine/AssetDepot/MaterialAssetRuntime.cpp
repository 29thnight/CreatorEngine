#include "MaterialAssetRuntime.h"
#include "../DataSystem.h"
#include "../Experiment/Cooked/CookedShaderMeta.h"
#include "../Experiment/Cooked/MaterialAssetSetCodec.h"
#include "../Experiment/Cooked/CookedCodeMaterial.h"
#include "../Experiment/MaterialPropertyBlock.h"
#include "../Experiment/MaterialResolver.h"
#include "../ExperimentMaterialMigration.h"
#include "LegacyResourceCharges.h"

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
        consumer->SetTerminalLocked(status);
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
        charge.String(meta.codeProgramIdentity);
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
            charge.Vector(meta.assetOrigin->codeProgramTextures);
            if (meta.assetOrigin->codeProgramSource)
            {
                const auto& source = *meta.assetOrigin->codeProgramSource;
                charge.Vector(source.entry.dependencies);
                charge.String(source.blob.targetPlatform);
                charge.String(source.blob.targetAbi);
                charge.String(source.blob.artifactPath);
            }
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
        std::vector<std::byte>& bytes, AssetDepot::AssetByteObservation& inputObservation, std::string& failure)
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
        inputObservation.Set(bytes.capacity());
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
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<ShaderMeta>>(m_assetRequestCounters);
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
        AssetDepot::AssetByteObservation inputObservation(m_materialAssets.shaderMetaInputStagingBytes);
        std::vector<std::byte> bytes;
        std::string failure;
        auto error = ReadShaderMetaBytes(resolved, bytes, inputObservation, failure);
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

namespace
{
    thread_local std::uint64_t MaterialPipelineSubmittingRequestId{};

    class MaterialPipelineSubmissionScope final
    {
    public:
        explicit MaterialPipelineSubmissionScope(std::uint64_t requestId)
            : m_previous(std::exchange(MaterialPipelineSubmittingRequestId, requestId))
        {
        }
        ~MaterialPipelineSubmissionScope()
        {
            MaterialPipelineSubmittingRequestId = m_previous;
        }
        MaterialPipelineSubmissionScope(const MaterialPipelineSubmissionScope&) = delete;
        MaterialPipelineSubmissionScope& operator=(const MaterialPipelineSubmissionScope&) = delete;

    private:
        std::uint64_t m_previous{};
    };

    template<class T>
    void NotifyMaterialPipelineConsumer(const own::shared_owner<AssetDepot::AssetRequestState<T>>& consumer,
        Status status, Error error, const std::string& message, const own::shared_owner<const T>& asset = {})
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != Status::Pending)
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
            consumer->message.clear();
        }
    }

    AssetDepot::MaterialPipelineAssetKey MaterialPipelineKey(const material_cooked::ResolvedAssetEntry& resolved)
    {
        return { resolved.entry.asset, resolved.blob, resolved.resolverRevision };
    }

    template<class T>
    bool SupportedMaterialPipelineRepresentation(const material_cooked::ResolvedAssetEntry& resolved)
    {
        const auto kind = AssetDepot::AssetLink<T>::kKind;
        if (resolved.entry.asset.kind != kind || resolved.blob.kind != kind || !resolved.byteSource
            || resolved.entry.asset.key.subassetId.IsValid())
        {
            return false;
        }
        if constexpr (std::is_same_v<T, material_graph::Generation>)
        {
            return resolved.blob.representation == material_cooked::kMaterialProgramRepresentation
                && resolved.blob.schemaVersion == material_cooked::kMaterialProgramArtifactVersion;
        }
        else if constexpr (std::is_same_v<T, LX::Runtime::ShaderGeneration>)
        {
            return resolved.blob.representation == material_cooked::kCodeProgramRepresentation
                && resolved.blob.schemaVersion == material_cooked::kCodeProgramVersion;
        }
        else
        {
            const bool authored = resolved.blob.representation == material_cooked::kAuthoredMaterialRepresentation
                && resolved.blob.schemaVersion == material_cooked::kAuthoredMaterialVersion;
            if constexpr (std::is_same_v<T, experiment::Material>)
            {
                return authored;
            }
            else
            {
                static_assert(std::is_same_v<T, Material>);
                return authored || (resolved.blob.representation == material_cooked::kMaterialDocumentRepresentation
                    && resolved.blob.schemaVersion == material_cooked::kMaterialArtifactVersion);
            }
        }
    }

    template<class T>
    bool MatchesMaterialPipelineAsset(const T& asset, const AssetDepot::MaterialPipelineAssetKey& key)
    {
        const auto& origin = [&]() -> const auto&
        {
            if constexpr (!std::is_same_v<T, Material>)
            {
                return asset.assetOrigin;
            }
            else
            {
                return asset.GetAssetOrigin();
            }
        }();
        return origin && origin->resolved.entry.asset == key.asset
            && origin->resolved.blob == key.blob && origin->resolved.resolverRevision == key.resolverRevision;
    }

    void ChargeMaterialOrigin(asset_cache_detail::Charge& charge, const material_cooked::ResolvedAssetEntry& resolved)
    {
        charge.Vector(resolved.entry.dependencies);
        charge.String(resolved.blob.targetPlatform);
        charge.String(resolved.blob.targetAbi);
        charge.String(resolved.blob.artifactPath);
    }

    template<class T>
    std::size_t MaterialPipelineChargeBytes(const T& asset)
    {
        asset_cache_detail::Charge charge;
        if constexpr (std::is_same_v<T, material_graph::Generation>)
        {
            charge.Add(asset.RetainedPayloadBytes());
        }
        else if constexpr (std::is_same_v<T, LX::Runtime::ShaderGeneration>)
        {
            charge.Shader(asset);
            if (asset.codeProgram)
            {
                charge.Add(material_cooked::CodeProgramRetainedBytes(*asset.codeProgram));
            }
            if (asset.assetOrigin)
            {
                charge.Add(sizeof(AssetDepot::MaterialProgramAssetOrigin));
                ChargeMaterialOrigin(charge, asset.assetOrigin->resolved);
                charge.Vector(asset.assetOrigin->defaultTextures);
                charge.Add(asset.assetOrigin->defaultTextureChargeBytes);
                if (asset.assetOrigin->shaderMetadata)
                {
                    charge.Add(ShaderMetaChargeBytes(*asset.assetOrigin->shaderMetadata));
                }
            }
        }
        else if constexpr (std::is_same_v<T, experiment::Material>)
        {
            charge.Add(asset_cache_detail::LegacyAuthoredMaterialRetainedBytes(asset));
            if (asset.assetOrigin)
            {
                charge.Add(sizeof(AssetDepot::MaterialDocumentAssetOrigin));
                ChargeMaterialOrigin(charge, asset.assetOrigin->resolved);
                charge.Vector(asset.assetOrigin->textures);
                if (asset.assetOrigin->codeProgram)
                {
                    charge.Add(MaterialPipelineChargeBytes(*asset.assetOrigin->codeProgram));
                }
                for (const auto& texture : asset.assetOrigin->textures)
                {
                    charge.Add(asset_cache_detail::LegacyTextureRetainedBytes(*texture.owner));
                }
            }
        }
        else
        {
            charge.Add(asset_cache_detail::LegacyMaterialRetainedBytes(asset));
            if (asset.GetAssetOrigin())
            {
                charge.Add(sizeof(AssetDepot::MaterialDocumentAssetOrigin));
                ChargeMaterialOrigin(charge, asset.GetAssetOrigin()->resolved);
                if (asset.GetAssetOrigin()->codeProgram)
                {
                    charge.Add(MaterialPipelineChargeBytes(*asset.GetAssetOrigin()->codeProgram));
                    charge.Vector(asset.GetAssetOrigin()->textures);
                    for (const auto& texture : asset.GetAssetOrigin()->textures)
                    {
                        charge.Add(asset_cache_detail::LegacyTextureRetainedBytes(*texture.owner));
                    }
                }
            }
            // The legacy helper includes the canonical Generation charge and
            // active instance Texture owners, so do not count defaults again.
        }
        return charge.Bytes();
    }

    template<class T>
    std::uint64_t NextMaterialPipelineUse(AssetDepot::MaterialPipelineAssetCache<T>& cache)
    {
        if (cache.clock == (std::numeric_limits<std::uint64_t>::max)())
        {
            for (auto& [key, entry] : cache.entries)
            {
                entry.lastUse = 0u;
            }
            cache.clock = 0u;
        }
        return ++cache.clock;
    }

    template<class T>
    void PruneMaterialPipelineCache(AssetDepot::MaterialPipelineAssetCache<T>& cache)
    {
        std::erase_if(cache.entries, [](const auto& item)
        {
            return !item.second.retained && !item.second.inFlight && item.second.live.expired();
        });
    }

    template<class T>
    void TrimMaterialPipelineCache(AssetDepot::MaterialPipelineAssetCache<T>& cache,
        std::vector<own::shared_owner<const T>>& retired)
    {
        while (cache.retainedChargeBytes > cache.budgetBytes)
        {
            auto oldest = cache.entries.end();
            for (auto item = cache.entries.begin(); item != cache.entries.end(); ++item)
            {
                if (item->second.retained && (oldest == cache.entries.end()
                    || item->second.lastUse < oldest->second.lastUse))
                {
                    oldest = item;
                }
            }
            if (oldest == cache.entries.end())
            {
                break;
            }
            assert(retired.size() < retired.capacity());
            cache.retainedChargeBytes -= oldest->second.retainedCharge;
            oldest->second.retainedCharge = 0u;
            retired.push_back(std::move(oldest->second.retained));
            ++cache.logicalEvictions;
        }
        PruneMaterialPipelineCache(cache);
    }

    template<class T>
    void StageMaterialPipelineConsumers(const AssetDepot::MaterialPipelineAssetCache<T>& cache,
        std::vector<own::shared_owner<AssetDepot::AssetRequestState<T>>>& consumers)
    {
        assert(consumers.empty());
        std::size_t count{};
        for (const auto& [key, entry] : cache.entries)
        {
            if (entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                if (entry.inFlight->consumers.size() > consumers.max_size() - count)
                {
                    throw std::length_error("Material retirement consumer capacity exceeded.");
                }
                count += entry.inFlight->consumers.size();
            }
        }
        consumers.reserve(count);
        for (const auto& [key, entry] : cache.entries)
        {
            if (entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                for (const auto& weak : entry.inFlight->consumers)
                {
                    if (auto consumer = weak.lock())
                    {
                        consumers.push_back(std::move(consumer));
                    }
                }
            }
        }
    }

    template<class T>
    void InvalidateMaterialPipelineCache(AssetDepot::MaterialPipelineAssetCache<T>& cache,
        AssetDepot::MaterialPipelineAssetEntries<T>& retired,
        const std::vector<own::shared_owner<AssetDepot::AssetRequestState<T>>>& consumers,
        Status status, Error error) noexcept
    {
        assert(retired.empty());
        static_assert(noexcept(retired.swap(cache.entries)));
        retired.swap(cache.entries);
        cache.retainedChargeBytes = 0u;
        for (auto& [key, entry] : retired)
        {
            if (entry.retained)
            {
                ++cache.logicalEvictions;
            }
            if (entry.inFlight && entry.inFlight->status == Status::Pending)
            {
                entry.inFlight->status = status;
                entry.inFlight->error = error;
                entry.inFlight->message.clear();
            }
        }
        for (const auto& consumer : consumers)
        {
            NotifyMaterialPipelineConsumer(consumer, status, error, {});
        }
    }

    template<class T>
    Error ReadMaterialPipelineBytes(material_cooked::ResolvedAssetEntry& resolved,
        std::vector<std::byte>& bytes, AssetDepot::AssetByteObservation& inputObservation, std::string& failure)
    {
        constexpr auto maximum = (std::is_same_v<T, material_graph::Generation>
            || std::is_same_v<T, LX::Runtime::ShaderGeneration>)
            ? material_cooked::kMaterialProgramAssetSetMaxBytes : material_cooked::kMaterialDocumentMaxBytes;
        if (resolved.blob.byteSize == 0u || resolved.blob.byteSize > maximum)
        {
            failure = "Material artifact size exceeds its bounded representation.";
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
            failure = "Material artifact size differs from its exact manifest record.";
            return Error::IntegrityFailed;
        }
        bytes.resize(static_cast<std::size_t>(size));
        inputObservation.Set(bytes.capacity());
        if (!resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0u, bytes, failure))
        {
            return Error::ReadFailed;
        }
        material_cooked::Sha256Digest digest{};
        if (!material_cooked::ComputeSha256(bytes, digest, failure) || digest != resolved.blob.contentSha256)
        {
            failure = "Material artifact SHA-256 differs from its exact captured record.";
            return Error::IntegrityFailed;
        }
        return Error::None;
    }

    own::shared_owner<const Texture> SelectMaterialTexture(
        const experiment::AssetId& id, LX::LXColorSpace colorSpace,
        std::span<const AssetDepot::MaterialAssetTexturePin> sources,
        std::vector<AssetDepot::MaterialAssetTexturePin>& selected, std::string& failure)
    {
        if (colorSpace != LX::LXColorSpace::Data && colorSpace != LX::LXColorSpace::Linear
            && colorSpace != LX::LXColorSpace::SRGB)
        {
            failure = "Material resource has an unknown texture color space.";
            return {};
        }
        const auto previous = std::ranges::find_if(selected, [&](const auto& pin)
        {
            return pin.assetId == id && pin.colorSpace == colorSpace;
        });
        if (previous != selected.end())
        {
            return previous->owner;
        }
        const auto source = std::ranges::find(sources, id, &AssetDepot::MaterialAssetTexturePin::assetId);
        if (source == sources.end() || !source->owner)
        {
            failure = "Material texture was not resolved in the captured hard dependency closure.";
            return {};
        }
        // Fixed-DAG Source acquisition precedes the parent's token. This clones
        // only a sampling description; it performs no I/O, decode or mip work.
        auto owner = Texture::WithColorSpace(source->owner, colorSpace == LX::LXColorSpace::SRGB);
        if (!owner)
        {
            failure = "Material texture cannot represent the requested color space.";
            return {};
        }
        // Preserve graph sampling policy: Data and Linear filter as linear,
        // SRGB filters in its transfer function. Reproducible AssetDepot images
        // only transform their exact descriptor recipe here; rehydration does
        // the mip work later in the image worker, never inline source loading.
        owner = Texture::WithMipChain(owner, failure);
        if (!owner)
        {
            return {};
        }
        selected.push_back({ id, colorSpace, owner });
        return owner;
    }
}

void DataSystem::StageMaterialAssetRetirementLocked(AssetDepot::MaterialAssetRetiredEntries& retired)
{
    assert(retired.shaderMetadata.empty() && retired.shaderMetaConsumers.empty());
    assert(retired.programs.empty() && retired.materials.empty());
    StageMaterialPipelineConsumers(m_materialAssets.programs, retired.programConsumers);
    StageMaterialPipelineConsumers(m_materialAssets.materials, retired.materialConsumers);
    StageMaterialPipelineConsumers(m_materialAssets.codePrograms, retired.codeProgramConsumers);
    StageMaterialPipelineConsumers(m_materialAssets.authoredMaterials, retired.authoredMaterialConsumers);
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
    InvalidateMaterialPipelineCache(m_materialAssets.programs, retired.programs,
        retired.programConsumers, status, error);
    InvalidateMaterialPipelineCache(m_materialAssets.materials, retired.materials,
        retired.materialConsumers, status, error);
    InvalidateMaterialPipelineCache(m_materialAssets.codePrograms, retired.codePrograms,
        retired.codeProgramConsumers, status, error);
    InvalidateMaterialPipelineCache(m_materialAssets.authoredMaterials, retired.authoredMaterials,
        retired.authoredMaterialConsumers, status, error);
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
    snapshot.inputStagingBytes = m_materialAssets.shaderMetaInputStagingBytes.load(std::memory_order_relaxed);
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

template<class T>
AssetDepot::MaterialPipelineAssetCache<T>& DataSystem::MaterialPipelineAssetCacheLocked()
{
    if constexpr (std::is_same_v<T, material_graph::Generation>)
    {
        return m_materialAssets.programs;
    }
    else if constexpr (std::is_same_v<T, LX::Runtime::ShaderGeneration>)
    {
        return m_materialAssets.codePrograms;
    }
    else if constexpr (std::is_same_v<T, experiment::Material>)
    {
        return m_materialAssets.authoredMaterials;
    }
    else
    {
        static_assert(std::is_same_v<T, Material>);
        return m_materialAssets.materials;
    }
}

template<class T>
own::shared_owner<const T> DataSystem::TryAcquireCurrentMaterialPipelineAsset(AssetDepot::AssetLink<T> link)
{
    if (!link.IsValid() || link.identity.subassetId.IsValid())
    {
        return {};
    }
    material_cooked::ResolvedAssetEntry resolved;
    own::shared_owner<const T> resident;
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
    auto& cache = MaterialPipelineAssetCacheLocked<T>();
    const auto key = MaterialPipelineKey(resolved);
    const auto found = cache.entries.find(key);
    if (found == cache.entries.end())
    {
        return {};
    }
    resident = found->second.retained ? found->second.retained : found->second.live.lock();
    if (!resident || !MatchesMaterialPipelineAsset(*resident, key))
    {
        return {};
    }
    found->second.lastUse = NextMaterialPipelineUse(cache);
    return resident;
}

template<class T>
AssetDepot::AssetRequest<T> DataSystem::RequestCurrentMaterialPipelineAssetAsync(AssetDepot::AssetLink<T> link)
{
    own::shared_owner<const material_cooked::CookedAssetCatalog> catalog;
    std::uint64_t epoch{};
    {
        std::lock_guard lock(m_assetPreparationMutex);
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        catalog = m_cookedCatalog;
        epoch = m_assetPreparationEpoch;
    }
    return RequestMaterialPipelineAssetFromSnapshot(link, std::move(catalog), epoch);
}

template<class T>
AssetDepot::AssetRequest<T> DataSystem::RequestMaterialPipelineAssetFromSnapshot(AssetDepot::AssetLink<T> link,
    own::shared_owner<const material_cooked::CookedAssetCatalog> catalog, std::uint64_t epoch)
{
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<T>>(m_assetRequestCounters);
    AssetDepot::AssetRequest<T> request(consumer);
    const auto fail = [&](Status status, Error error, const std::string& message = {})
    {
        NotifyMaterialPipelineConsumer(consumer, status, error, message);
        return request;
    };
    if (!link.IsValid())
    {
        return fail(Status::Failed, Error::InvalidLink, "Invalid material pipeline link.");
    }
    if (link.identity.subassetId.IsValid())
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation,
            "Material pipeline artifacts require a global identity without a subasset ID.");
    }
    {
        std::lock_guard lock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || epoch != m_assetPreparationEpoch)
        {
            return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
        }
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (catalog && (!m_cookedCatalog || m_cookedCatalog->ResolverRevision() != catalog->ResolverRevision()))
        {
            return fail(Status::Stale, Error::RevisionChanged);
        }
    }
    if (!catalog)
    {
        return fail(Status::Failed, Error::NotMounted, "No material pipeline catalog is mounted.");
    }
    std::vector<material_cooked::ResolvedAssetEntry> closure;
    material_cooked::AssetCatalogLookupIssue issue;
    const auto lookup = catalog->CollectHardClosure(link.ToReference(), closure, issue);
    if (lookup != material_cooked::AssetLookupStatus::Found)
    {
        const auto error = lookup == material_cooked::AssetLookupStatus::HardDependencyCycle
            ? Error::HardDependencyCycle : lookup == material_cooked::AssetLookupStatus::TypeMismatch
            ? Error::TypeMismatch : Error::NotMounted;
        return fail(Status::Failed, error, issue.message);
    }
    if (closure.empty() || !SupportedMaterialPipelineRepresentation<T>(closure.back()))
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation,
            "Unsupported MaterialProgram or Lattice Material document representation.");
    }
    auto resolved = std::move(closure.back());
    const auto key = MaterialPipelineKey(resolved);
    // All resource-owning locals outlive either admission lock, including an
    // inline scheduler terminal observer and a lost duplicate-admission race.
    own::shared_owner<AssetDepot::MaterialPipelineAssetWork<T>> work;
    own::shared_owner<const T> resident;
    std::vector<AssetDepot::MaterialAssetTextureRequest> textures;
    AssetDepot::AssetRequest<material_graph::Generation> program;
    AssetDepot::AssetRequest<LX::Runtime::ShaderGeneration> codeProgram;
    AssetDepot::AssetRequest<experiment::Material> authoredMaterial;
    std::optional<material_cooked::ResolvedAssetEntry> shaderMetadata;
    std::vector<job_handle> dependencies;
    const auto joinOrAcquire = [&]()
    {
        auto& cache = MaterialPipelineAssetCacheLocked<T>();
        const auto found = cache.entries.find(key);
        if (found == cache.entries.end())
        {
            return false;
        }
        found->second.lastUse = NextMaterialPipelineUse(cache);
        if (found->second.inFlight)
        {
            work = found->second.inFlight;
            work->consumers.emplace_back(consumer);
            consumer->completion = work->completion;
            return true;
        }
        resident = found->second.retained ? found->second.retained : found->second.live.lock();
        if (resident)
        {
            if (!MatchesMaterialPipelineAsset(*resident, key))
            {
                NotifyMaterialPipelineConsumer(consumer, Status::Failed, Error::IntegrityFailed,
                    "Resident material does not match its exact generation key.");
            }
            else
            {
                NotifyMaterialPipelineConsumer(consumer, Status::Ready, Error::None, {}, resident);
            }
            return true;
        }
        return false;
    };
    try
    {
        {
            std::lock_guard lock(m_assetPreparationMutex);
            std::lock_guard catalogLock(m_cookedCatalogMutex);
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || epoch != m_assetPreparationEpoch
                || !m_cookedCatalog || m_cookedCatalog->ResolverRevision() != resolved.resolverRevision)
            {
                return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                    m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
            }
            if (joinOrAcquire())
            {
                return request;
            }
        }
        // Build a fixed dependency DAG from typed metadata. Texture color-space
        // projections occur after decode and require no additional jobs or I/O.
        // Therefore the token returned below covers the entire hard closure.
        const bool authoredRepresentation = resolved.blob.representation == material_cooked::kAuthoredMaterialRepresentation
            && resolved.entry.asset.kind == material_cooked::CookedAssetKind::Material;
        if (resolved.entry.dependencies.size() > 130u)
        {
            return fail(Status::Failed, Error::DependencyFailed, "Material dependency count exceeds its budget.");
        }
        material_cooked::TypedAssetReference programReference;
        std::size_t programCount{};
        for (const auto& dependency : resolved.entry.dependencies)
        {
            if (dependency.kind != material_cooked::AssetDependencyKind::Hard
                || dependency.target.key.subassetId.IsValid())
            {
                return fail(Status::Failed, Error::DependencyFailed,
                    "Material pipeline entries require explicitly typed root Hard dependencies.");
            }
            if (dependency.target.kind == material_cooked::CookedAssetKind::Texture)
            {
                continue;
            }
            if constexpr (std::is_same_v<T, Material> || std::is_same_v<T, experiment::Material>)
            {
                if (dependency.target.kind == material_cooked::CookedAssetKind::MaterialProgram)
                {
                    programReference = dependency.target;
                    ++programCount;
                    continue;
                }
            }
            if (dependency.target.kind == material_cooked::CookedAssetKind::ShaderMeta
                && (authoredRepresentation || std::is_same_v<T, LX::Runtime::ShaderGeneration>))
            {
                material_cooked::ResolvedAssetEntry metadata;
                if (shaderMetadata || catalog->Find(dependency.target, metadata)
                    != material_cooked::AssetLookupStatus::Found
                    || !SupportedShaderMetaRepresentation(metadata) || HasShaderMetaHardDependency(metadata))
                {
                    return fail(Status::Failed, Error::DependencyFailed, "Code material requires one supported metadata leaf.");
                }
                shaderMetadata = std::move(metadata);
                continue;
            }
            return fail(Status::Failed, Error::DependencyFailed, "Unsupported material hard dependency type.");
        }
        if ((authoredRepresentation || std::is_same_v<T, LX::Runtime::ShaderGeneration>) && !shaderMetadata)
        {
            return fail(Status::Failed, Error::DependencyFailed, "Code material has no exact metadata dependency.");
        }
        if constexpr (std::is_same_v<T, Material> || std::is_same_v<T, experiment::Material>)
        {
            if (programCount != 1u)
            {
                return fail(Status::Failed, Error::DependencyFailed, "Material requires one MaterialProgram.");
            }
            if constexpr (std::is_same_v<T, Material>)
            {
                if (authoredRepresentation)
                {
                    // One authored decode/validation owns the closure; this view
                    // is only the existing rendering facade, never a new manager.
                    authoredMaterial = RequestMaterialPipelineAssetFromSnapshot(
                        AssetDepot::AssetLink<experiment::Material>{ link.identity }, catalog, epoch);
                    const auto completion = authoredMaterial.Completion();
                    if (completion.valid())
                    {
                        dependencies.push_back(completion);
                    }
                }
                else
                {
                    program = RequestMaterialPipelineAssetFromSnapshot(
                        AssetDepot::AssetLink<material_graph::Generation>{ programReference.key }, catalog, epoch);
                    const auto completion = program.Completion();
                    if (completion.valid())
                    {
                        dependencies.push_back(completion);
                    }
                }
            }
            else
            {
                codeProgram = RequestMaterialPipelineAssetFromSnapshot(
                    AssetDepot::AssetLink<LX::Runtime::ShaderGeneration>{ programReference.key }, catalog, epoch);
                const auto completion = codeProgram.Completion();
                if (completion.valid())
                {
                    dependencies.push_back(completion);
                }
            }
        }
        textures.reserve(resolved.entry.dependencies.size());
        for (const auto& dependency : resolved.entry.dependencies)
        {
            if (dependency.target.kind == material_cooked::CookedAssetKind::Texture
                && !(std::is_same_v<T, Material> && authoredRepresentation))
            {
                auto texture = RequestTextureAsyncFromSnapshot(
                    AssetDepot::AssetLink<Texture>{ dependency.target.key }, {}, catalog, epoch);
                const auto completion = texture.Completion();
                if (completion.valid())
                {
                    dependencies.push_back(completion);
                }
                textures.push_back({ dependency.target.key.assetId, std::move(texture) });
            }
        }
        std::lock_guard lock(m_assetPreparationMutex);
        {
            std::lock_guard catalogLock(m_cookedCatalogMutex);
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || epoch != m_assetPreparationEpoch
                || !m_cookedCatalog || m_cookedCatalog->ResolverRevision() != resolved.resolverRevision)
            {
                return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                    m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
            }
        }
        if (joinOrAcquire())
        {
            return request;
        }
        if (m_materialAssets.nextRequestId == (std::numeric_limits<std::uint64_t>::max)())
        {
            return fail(Status::Failed, Error::SubmissionFailed, "Material request identity space exhausted.");
        }
        work = own::make_shared<AssetDepot::MaterialPipelineAssetWork<T>>();
        work->key = key;
        work->resolved = std::move(resolved);
        work->catalog = catalog;
        work->textures = std::move(textures);
        work->program = std::move(program);
        work->codeProgram = std::move(codeProgram);
        work->authoredMaterial = std::move(authoredMaterial);
        work->shaderMetadata = std::move(shaderMetadata);
        work->epoch = epoch;
        work->requestId = m_materialAssets.nextRequestId++;
        if constexpr (std::is_same_v<T, material_graph::Generation>)
        {
            work->programGeneration = m_materialGraphGenerations.ReserveGeneration();
            if (work->programGeneration == 0u)
            {
                return fail(Status::Failed, Error::SubmissionFailed, "Material generation identity space exhausted.");
            }
        }
        work->consumers.emplace_back(consumer);
        auto& cache = MaterialPipelineAssetCacheLocked<T>();
        PruneMaterialPipelineCache(cache);
        auto& entry = cache.entries[key];
        entry.lastUse = NextMaterialPipelineUse(cache);
        entry.inFlight = work;
        MaterialPipelineSubmissionScope submitting(work->requestId);
        try
        {
            job_group jobs;
            jobs.add([this, work]() { RunMaterialPipelineAssetWork(work); });
            jobs.on_complete([this, work](std::exception_ptr failure)
            {
                const auto finish = [&]()
                {
                    CompleteMaterialPipelineAssetWorkLocked(work, Status::Failed,
                        failure ? Error::SubmissionFailed : Error::DecodeFailed);
                };
                if (MaterialPipelineSubmittingRequestId == work->requestId)
                {
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
        catch (...)
        {
            CompleteMaterialPipelineAssetWorkLocked(work, Status::Failed, Error::SubmissionFailed);
        }
        consumer->completion = work->completion;
    }
    catch (...)
    {
        return fail(Status::Failed, Error::SubmissionFailed);
    }
    return request;
}

template<class T>
void DataSystem::RunMaterialPipelineAssetWork(own::shared_owner<AssetDepot::MaterialPipelineAssetWork<T>> work)
{
    try
    {
        {
            std::lock_guard lock(m_assetPreparationMutex);
            if (work->status != Status::Pending)
            {
                return;
            }
            if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u || work->epoch != m_assetPreparationEpoch)
            {
                CompleteMaterialPipelineAssetWorkLocked(work,
                    m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
                    m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
                return;
            }
        }
        std::string failure;
        Error error = Error::None;
        std::vector<AssetDepot::MaterialAssetTexturePin> sources;
        sources.reserve(work->textures.size());
        for (const auto& texture : work->textures)
        {
            const auto result = texture.request.Snapshot();
            const auto origin = result.asset ? result.asset->GetAssetOrigin()
                : own::shared_owner<const AssetDepot::TextureAssetOrigin>{};
            if (result.status != Status::Ready || !origin
                || origin->resolved.resolverRevision != work->key.resolverRevision
                || origin->resolved.entry.asset.key.assetId != texture.assetId)
            {
                error = Error::DependencyFailed;
                failure = "Material Texture hard dependency did not produce its captured generation.";
                break;
            }
            sources.push_back({ texture.assetId, LX::LXColorSpace::Data, result.asset });
        }
        own::shared_owner<const material_graph::Generation> program;
        own::shared_owner<const LX::Runtime::ShaderGeneration> codeProgram;
        own::shared_owner<const experiment::Material> authored;
        if constexpr (std::is_same_v<T, Material>)
        {
            if (work->key.blob.representation == material_cooked::kAuthoredMaterialRepresentation)
            {
                const auto result = work->authoredMaterial.Snapshot();
                authored = result.asset;
                if (result.status != Status::Ready || !authored
                    || !MatchesMaterialPipelineAsset(*authored, work->key))
                {
                    error = Error::DependencyFailed;
                    failure = "Authored Material did not produce its captured generation.";
                }
            }
            else
            {
                const auto result = work->program.Snapshot();
                program = result.asset;
                if (result.status != Status::Ready || !program || !program->assetOrigin
                    || program->assetOrigin->resolved.resolverRevision != work->key.resolverRevision)
                {
                    error = Error::DependencyFailed;
                    failure = "MaterialProgram hard dependency did not produce its captured generation.";
                }
            }
        }
        else if constexpr (std::is_same_v<T, experiment::Material>)
        {
            const auto result = work->codeProgram.Snapshot();
            codeProgram = result.asset;
            const auto origin = codeProgram ? codeProgram->assetOrigin : nullptr;
            const auto metadata = origin ? origin->shaderMetadata : nullptr;
            if (result.status != Status::Ready || !origin || !metadata || !metadata->assetOrigin
                || !work->shaderMetadata || !codeProgram->codeProgram
                || origin->resolved.resolverRevision != work->key.resolverRevision
                || metadata->assetOrigin->resolved.entry.asset != work->shaderMetadata->entry.asset
                || metadata->assetOrigin->resolved.blob != work->shaderMetadata->blob)
            {
                error = Error::DependencyFailed;
                failure = "Code Program and authored Material do not own the same exact metadata generation.";
            }
        }
        auto resolved = work->resolved;
        AssetDepot::AssetByteObservation inputObservation(m_materialAssets.materialInputStagingBytes);
        std::vector<std::byte> bytes;
        own::shared_owner<const T> candidate;
        own::shared_owner<LX::Runtime::ShaderGeneration> pendingCodeProgram;
        own::shared_owner<AssetDepot::MaterialProgramAssetOrigin> pendingCodeOrigin;
        if (error == Error::None && !authored)
        {
            error = ReadMaterialPipelineBytes<T>(resolved, bytes, inputObservation, failure);
        }
        if (error == Error::None)
        {
            if constexpr (std::is_same_v<T, material_graph::Generation>)
            {
                auto decoded = own::make_shared<material_graph::Generation>();
                if (!material_cooked::ReadMaterialProgramAssetSetArtifact(bytes, resolved.entry.asset,
                    resolved.entry.dependencies, {}, decoded->cooked, failure))
                {
                    error = Error::DecodeFailed;
                }
                else
                {
                    auto origin = own::make_shared<AssetDepot::MaterialProgramAssetOrigin>();
                    origin->resolved = std::move(resolved);
                    for (const auto& resource : decoded->cooked.product.program.resources)
                    {
                        if (resource.kind != LX::LXMaterialResourceKind::Texture)
                        {
                            continue;
                        }
                        experiment::AssetId id;
                        if ((!experiment::TryParseCanonicalAssetId(resource.reference, id)
                            && !assets::TryParseCanonicalUuidV8(resource.reference, id.value))
                            || !SelectMaterialTexture(id, resource.colorSpace, sources,
                                origin->defaultTextures, failure))
                        {
                            error = Error::DependencyFailed;
                            break;
                        }
                    }
                    if (error == Error::None)
                    {
                        asset_cache_detail::Charge textureCharge;
                        for (const auto& texture : origin->defaultTextures)
                        {
                            textureCharge.Add(asset_cache_detail::LegacyTextureRetainedBytes(*texture.owner));
                        }
                        origin->defaultTextureChargeBytes = textureCharge.Bytes();
                        decoded->assetId = work->key.asset.key.assetId;
                        decoded->generation = work->programGeneration;
                        decoded->assetOrigin = std::move(origin);
                        if (!material_graph::PrepareGenerationMetadata(*decoded, failure))
                        {
                            error = Error::DecodeFailed;
                        }
                        else
                        {
                            candidate = std::move(decoded);
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, LX::Runtime::ShaderGeneration>)
            {
                auto product = own::make_shared<material_cooked::CodeProgram>();
                if (!material_cooked::ReadCodeProgramArtifact(bytes, resolved.entry.asset,
                    resolved.entry.dependencies, *product, failure) || !work->shaderMetadata)
                {
                    error = Error::DecodeFailed;
                }
                else
                {
                    auto metadataEntry = *work->shaderMetadata;
                    AssetDepot::AssetByteObservation metadataObservation(m_materialAssets.shaderMetaInputStagingBytes);
                    std::vector<std::byte> metadataBytes;
                    error = ReadShaderMetaBytes(metadataEntry, metadataBytes, metadataObservation, failure);
                    auto metadata = own::make_shared<ShaderMeta>();
                    if (error == Error::None && (metadataBytes != product->metadataBytes
                        || !material_cooked::ReadShaderMetaArtifact(metadataBytes,
                            product->shaderMetaAssetId, *metadata, failure)))
                    {
                        error = Error::IntegrityFailed;
                        failure = "Code Program metadata differs from its exact hard descriptor artifact.";
                    }
                    if (error == Error::None)
                    {
                        auto metadataOrigin = own::make_shared<AssetDepot::ShaderMetaAssetOrigin>();
                        metadataOrigin->resolved = std::move(metadataEntry);
                        metadataOrigin->codeProgramSource = resolved;
                        metadata->assetOrigin = metadataOrigin;
                        metadata->codeProgram = product;
                        constexpr char hex[] = "0123456789abcdef";
                        metadata->codeProgramIdentity.reserve(64u);
                        for (const auto byte : resolved.blob.contentSha256)
                        {
                            metadata->codeProgramIdentity.push_back(hex[byte >> 4u]);
                            metadata->codeProgramIdentity.push_back(hex[byte & 15u]);
                        }
                        pendingCodeOrigin = own::make_shared<AssetDepot::MaterialProgramAssetOrigin>();
                        pendingCodeOrigin->resolved = std::move(resolved);
                        pendingCodeOrigin->shaderMetadata = metadata;
                        for (const auto& property : metadata->properties)
                        {
                            if (property.type != ShaderPropertyType::Texture2D)
                            {
                                continue;
                            }
                            const auto* guid = std::get_if<FileGuid>(&property.defaultValue);
                            if (guid && *guid != FileGuid{})
                            {
                                const auto color = property.colorSpace == "srgb" ? LX::LXColorSpace::SRGB : LX::LXColorSpace::Data;
                                if (!SelectMaterialTexture({ guid->m_guid }, color, sources,
                                    pendingCodeOrigin->defaultTextures, failure))
                                {
                                    error = Error::DependencyFailed;
                                    break;
                                }
                            }
                        }
                        if (error == Error::None)
                        {
                            asset_cache_detail::Charge charge;
                            for (const auto& texture : pendingCodeOrigin->defaultTextures)
                            {
                                charge.Add(asset_cache_detail::LegacyTextureRetainedBytes(*texture.owner));
                                metadataOrigin->codeProgramTextures.push_back(texture.owner);
                            }
                            pendingCodeOrigin->defaultTextureChargeBytes = charge.Bytes();
                            pendingCodeProgram = own::make_shared<LX::Runtime::ShaderGeneration>();
                            pendingCodeProgram->meta = *metadata;
                            pendingCodeProgram->layout = product->layout;
                            pendingCodeProgram->codeProgram = std::move(product);
                            pendingCodeProgram->meta.codeProgram = pendingCodeProgram->codeProgram;
                            pendingCodeProgram->assetOrigin = pendingCodeOrigin;
                            if (!LX::Runtime::ValidateShaderGeneration(*pendingCodeProgram, failure))
                            {
                                error = Error::DecodeFailed;
                            }
                        }
                    }
                }
            }
            else if constexpr (std::is_same_v<T, experiment::Material>)
            {
                material_cooked::AuthoredMaterialDocument document;
                if (!material_cooked::ReadAuthoredMaterialArtifact(bytes, resolved.entry.asset,
                    resolved.entry.dependencies, document, failure)
                    || !material_cooked::ValidateAuthoredMaterialBinding(document, *codeProgram->codeProgram, failure))
                {
                    error = Error::DecodeFailed;
                }
                else
                {
                    auto origin = own::make_shared<AssetDepot::MaterialDocumentAssetOrigin>();
                    origin->resolved = std::move(resolved);
                    origin->codeProgram = codeProgram;
                    origin->textures = sources;
                    auto material = own::make_shared<experiment::Material>(std::move(document.material));
                    material->assetOrigin = std::move(origin);
                    experiment::ResolvedMaterial resolvedMaterial;
                    own::shared_owner<const LX::Runtime::Instance> instance;
                    if (!experiment::ResolveMaterial(*material, {}, resolvedMaterial, failure))
                    {
                        error = Error::DependencyFailed;
                    }
                    else
                    {
                        std::vector<MaterialTextureOwner> textureOwners;
                        for (const auto& texture : resolvedMaterial.textures)
                        {
                            textureOwners.push_back({ texture.propertyName, texture.owner });
                        }
                        if (!experiment::BuildMaterialRuntimeInstance(*material, codeProgram->meta, codeProgram->layout,
                            codeProgram->codeHandle, textureOwners, instance, failure))
                        {
                            error = Error::DecodeFailed;
                        }
                        else
                        {
                            candidate = std::move(material);
                        }
                    }
                }
            }
            else
            {
                if (authored)
                {
                    auto material = own::make_shared<Material>();
                    const auto& code = authored->assetOrigin->codeProgram;
                    experiment::ResolvedMaterial resolvedMaterial;
                    if (!ExperimentMaterialMigration::ConvertToLegacyMaterial(*authored, &code->meta, *material, failure)
                        || !experiment::ResolveMaterial(*authored, {}, resolvedMaterial, failure))
                    {
                        error = Error::DecodeFailed;
                    }
                    else
                    {
                        std::vector<MaterialTextureOwner> owners;
                        for (const auto& texture : resolvedMaterial.textures)
                        {
                            owners.push_back({ texture.propertyName, texture.owner });
                        }
                        own::shared_owner<const LX::Runtime::Instance> instance;
                        if (!experiment::BuildMaterialRuntimeInstance(*authored, code->meta, code->layout,
                            code->codeHandle, owners, instance, failure))
                        {
                            error = Error::DecodeFailed;
                        }
                        else
                        {
                            material->m_runtimeInstance = std::move(instance);
                            material->m_shaderMetaHandle = code->codeHandle;
                            material->m_assetOrigin = authored->assetOrigin;
                            material->SynchronizeCodeRuntime();
                            candidate = std::move(material);
                        }
                    }
                }
                else
                {
                    material_graph::InstanceDocument document;
                    if (!material_cooked::ReadMaterialAssetSetDocument(bytes, resolved.entry.asset,
                        resolved.entry.dependencies, document, failure)
                        || !material_cooked::ValidateMaterialAssetSetBinding(document, program->cooked.product, failure))
                    {
                        error = Error::DecodeFailed;
                    }
                    else
                    {
                        std::vector<AssetDepot::MaterialAssetTexturePin> selected;
                        const auto loadTexture = [&](const experiment::AssetId& id, LX::LXColorSpace colorSpace,
                            std::string& textureFailure) -> own::shared_owner<const Texture>
                        {
                            if (std::ranges::any_of(sources, [&](const auto& pin) { return pin.assetId == id; }))
                            {
                                return SelectMaterialTexture(id, colorSpace, sources, selected, textureFailure);
                            }
                            const auto& defaults = program->assetOrigin->defaultTextures;
                            const auto found = std::ranges::find_if(defaults, [&](const auto& pin)
                            {
                                return pin.assetId == id && pin.colorSpace == colorSpace;
                            });
                            if (found == defaults.end())
                            {
                                textureFailure = "Material default texture is absent from its exact Program generation.";
                                return {};
                            }
                            return found->owner;
                        };
                        own::shared_owner<const material_graph::Instance> instance;
                        if (!material_graph::BuildInstance(program, document.description, loadTexture, instance, failure))
                        {
                            error = Error::DependencyFailed;
                        }
                        else
                        {
                            auto origin = own::make_shared<AssetDepot::MaterialDocumentAssetOrigin>();
                            origin->resolved = std::move(resolved);
                            auto material = own::make_shared<Material>();
                            material->m_name = std::move(document.name);
                            material->m_fileGuid = FileGuid{ document.materialId.value };
                            // The graph identity belongs to its owning instance;
                            // the legacy authored ShaderMeta slot stays unused.
                            material->m_shaderMetaGuid = {};
                            material->m_doubleSided = document.doubleSided;
                            const auto& blendMode = program->surfaceBlendMode
                                ? *program->surfaceBlendMode : document.blendMode;
                            if (blendMode == "transparent")
                            {
                                material->m_renderingMode = MaterialRenderingMode::Transparent;
                            }
                            else if (blendMode == "masked")
                            {
                                material->m_renderingMode = MaterialRenderingMode::Masked;
                            }
                            material->m_materialGraphInstance = std::move(instance);
                            material->m_assetOrigin = std::move(origin);
                            candidate = std::move(material);
                        }
                    }
                }
            }
        }
        std::lock_guard lock(m_assetPreparationMutex);
        if constexpr (std::is_same_v<T, LX::Runtime::ShaderGeneration>)
        {
            bool current{};
            {
                std::lock_guard catalogLock(m_cookedCatalogMutex);
                current = m_cookedCatalog && m_cookedCatalog->ResolverRevision() == work->key.resolverRevision;
            }
            if (error == Error::None && pendingCodeProgram && current
                && !m_assetPreparationStopping && m_assetInvalidationDepth == 0u
                && work->epoch == m_assetPreparationEpoch && work->status == Status::Pending)
            {
                if (!RegisterCodeShaderMetadataLocked(*pendingCodeProgram, *pendingCodeOrigin, failure))
                {
                    error = Error::DecodeFailed;
                }
                else
                {
                    candidate = std::move(pendingCodeProgram);
                }
            }
        }
        CompleteMaterialPipelineAssetWorkLocked(work, error == Error::None ? Status::Ready : Status::Failed,
            error, std::move(failure), candidate);
    }
    catch (...)
    {
        std::lock_guard lock(m_assetPreparationMutex);
        CompleteMaterialPipelineAssetWorkLocked(work, Status::Failed, Error::DecodeFailed);
    }
}

template<class T>
void DataSystem::CompleteMaterialPipelineAssetWorkLocked(
    const own::shared_owner<AssetDepot::MaterialPipelineAssetWork<T>>& work,
    Status status, Error error, std::string message, const own::shared_owner<const T>& candidate)
{
    if (work->status != Status::Pending)
    {
        return;
    }
    auto& cache = MaterialPipelineAssetCacheLocked<T>();
    const auto found = cache.entries.find(work->key);
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
        || found == cache.entries.end() || !found->second.inFlight
        || found->second.inFlight->requestId != work->requestId)
    {
        status = Status::Stale;
        error = Error::RevisionChanged;
        message.clear();
    }
    if (status == Status::Ready && (!candidate || !MatchesMaterialPipelineAsset(*candidate, work->key)))
    {
        status = Status::Failed;
        error = Error::DecodeFailed;
    }
    const auto asset = status == Status::Ready ? candidate : own::shared_owner<const T>{};
    const auto charge = asset ? MaterialPipelineChargeBytes(*asset) : 0u;
    bool retainCandidate = asset && charge <= cache.budgetBytes
        && charge <= (std::numeric_limits<std::size_t>::max)() - cache.retainedChargeBytes;
    if (retainCandidate && cache.retainedChargeBytes > cache.budgetBytes - charge)
    {
        try
        {
            work->retiredAssets.reserve(cache.entries.size());
        }
        catch (...)
        {
            retainCandidate = false;
        }
    }
    work->status = status;
    work->error = error;
    work->message = std::move(message);
    work->asset = asset;
    if (found != cache.entries.end() && found->second.inFlight
        && found->second.inFlight->requestId == work->requestId)
    {
        auto& entry = found->second;
        entry.inFlight.reset();
        if (asset)
        {
            entry.live = asset;
            entry.lastUse = NextMaterialPipelineUse(cache);
            if (retainCandidate)
            {
                assert(!entry.retained && entry.retainedCharge == 0u);
                entry.retained = asset;
                entry.retainedCharge = charge;
                cache.retainedChargeBytes += charge;
            }
        }
    }
    for (const auto& weak : work->consumers)
    {
        if (const auto consumer = weak.lock())
        {
            NotifyMaterialPipelineConsumer(consumer, status, error, work->message, asset);
        }
    }
    work->consumers.clear();
    TrimMaterialPipelineCache(cache, work->retiredAssets);
}

void DataSystem::SetMaterialAssetCacheBudgets(std::size_t programs, std::size_t materials)
{
    std::vector<own::shared_owner<const material_graph::Generation>> retiredPrograms;
    std::vector<own::shared_owner<const Material>> retiredMaterials;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        retiredPrograms.reserve(m_materialAssets.programs.entries.size());
        retiredMaterials.reserve(m_materialAssets.materials.entries.size());
        m_materialAssets.programs.budgetBytes = programs;
        m_materialAssets.materials.budgetBytes = materials;
        TrimMaterialPipelineCache(m_materialAssets.programs, retiredPrograms);
        TrimMaterialPipelineCache(m_materialAssets.materials, retiredMaterials);
    }
}

AssetDepot::MaterialAssetCacheSnapshot DataSystem::SnapshotMaterialAssetCache() const
{
    std::lock_guard lock(m_assetPreparationMutex);
    AssetDepot::MaterialAssetCacheSnapshot snapshot;
    snapshot.inputStagingBytes = m_materialAssets.materialInputStagingBytes.load(std::memory_order_relaxed);
    const auto append = [](const auto& cache, auto& statistics)
    {
        statistics.entries = cache.entries.size();
        statistics.retainedChargeBytes = cache.retainedChargeBytes;
        statistics.budgetBytes = cache.budgetBytes;
        statistics.logicalEvictions = cache.logicalEvictions;
        for (const auto& [key, entry] : cache.entries)
        {
            statistics.retainedEntries += entry.retained ? 1u : 0u;
            statistics.liveEntries += entry.live.expired() ? 0u : 1u;
            statistics.inFlight += entry.inFlight ? 1u : 0u;
        }
    };
    append(m_materialAssets.programs, snapshot.programs);
    append(m_materialAssets.materials, snapshot.materials);
    append(m_materialAssets.codePrograms, snapshot.codePrograms);
    append(m_materialAssets.authoredMaterials, snapshot.authoredMaterials);
    return snapshot;
}

template own::shared_owner<const material_graph::Generation> DataSystem::TryAcquireCurrentMaterialPipelineAsset(
    AssetDepot::AssetLink<material_graph::Generation>);
template own::shared_owner<const Material> DataSystem::TryAcquireCurrentMaterialPipelineAsset(
    AssetDepot::AssetLink<Material>);
template AssetDepot::AssetRequest<material_graph::Generation> DataSystem::RequestCurrentMaterialPipelineAssetAsync(
    AssetDepot::AssetLink<material_graph::Generation>);
template AssetDepot::AssetRequest<Material> DataSystem::RequestCurrentMaterialPipelineAssetAsync(
    AssetDepot::AssetLink<Material>);

template own::shared_owner<const LX::Runtime::ShaderGeneration> DataSystem::TryAcquireCurrentMaterialPipelineAsset(
    AssetDepot::AssetLink<LX::Runtime::ShaderGeneration>);
template own::shared_owner<const experiment::Material> DataSystem::TryAcquireCurrentMaterialPipelineAsset(
    AssetDepot::AssetLink<experiment::Material>);
template AssetDepot::AssetRequest<LX::Runtime::ShaderGeneration> DataSystem::RequestCurrentMaterialPipelineAssetAsync(
    AssetDepot::AssetLink<LX::Runtime::ShaderGeneration>);
template AssetDepot::AssetRequest<experiment::Material> DataSystem::RequestCurrentMaterialPipelineAssetAsync(
    AssetDepot::AssetLink<experiment::Material>);
template AssetDepot::AssetRequest<Material> DataSystem::RequestMaterialPipelineAssetFromSnapshot(
    AssetDepot::AssetLink<Material>, own::shared_owner<const experiment::cooked::CookedAssetCatalog>, std::uint64_t);

void DataSystem::SetCodeMaterialAssetCacheBudgets(std::size_t programs, std::size_t materials)
{
    std::vector<own::shared_owner<const LX::Runtime::ShaderGeneration>> retiredPrograms;
    std::vector<own::shared_owner<const experiment::Material>> retiredMaterials;
    {
        std::lock_guard lock(m_assetPreparationMutex);
        retiredPrograms.reserve(m_materialAssets.codePrograms.entries.size());
        retiredMaterials.reserve(m_materialAssets.authoredMaterials.entries.size());
        m_materialAssets.codePrograms.budgetBytes = programs;
        m_materialAssets.authoredMaterials.budgetBytes = materials;
        TrimMaterialPipelineCache(m_materialAssets.codePrograms, retiredPrograms);
        TrimMaterialPipelineCache(m_materialAssets.authoredMaterials, retiredMaterials);
    }
}

bool DataSystem::RegisterCodeShaderMetadataLocked(LX::Runtime::ShaderGeneration& shader,
    AssetDepot::MaterialProgramAssetOrigin& origin, std::string& failure)
{
    // Caller owns admission. Code slots are weak numeric identities, not an
    // additional resident root or a GUID-to-latest replacement. Several exact
    // code programs may legitimately share one descriptor identity.
    if (!shader.codeProgram || !origin.shaderMetadata || !origin.shaderMetadata->assetOrigin)
    {
        failure = "Code shader metadata was not fully decoded.";
        return false;
    }
    const auto& metadata = origin.shaderMetadata;
    std::lock_guard lock(m_shaderMetaMutex);
    if (m_shaderMetaGenerationSerial >= (std::numeric_limits<std::uint32_t>::max)()
        || m_shaderMetaSlots.size() >= (std::numeric_limits<std::uint32_t>::max)())
    {
        failure = "Code shader metadata identity space exhausted.";
        return false;
    }
    auto available = std::ranges::find_if(m_shaderMetaSlots, [](const auto& slot)
    {
        return slot.codeProgram && slot.current.expired();
    });
    if (available == m_shaderMetaSlots.end())
    {
        m_shaderMetaSlots.emplace_back();
        available = m_shaderMetaSlots.end() - 1;
    }
    const auto slotIndex = static_cast<std::uint32_t>(available - m_shaderMetaSlots.begin());
    auto& slot = *available;
    slot.guid = metadata->guid;
    slot.generation = static_cast<std::uint32_t>(++m_shaderMetaGenerationSerial);
    slot.resolverRevision = origin.resolved.resolverRevision;
    slot.occupied = true;
    slot.codeProgram = true;
    slot.current = metadata;
    slot.lastUse = NextShaderMetaUseLocked();
    shader.codeHandle = { slotIndex + 1u, slot.generation };

    failure.clear();
    return true;
}

template AssetDepot::AssetRequest<experiment::Material> DataSystem::RequestMaterialPipelineAssetFromSnapshot(
    AssetDepot::AssetLink<experiment::Material>, own::shared_owner<const experiment::cooked::CookedAssetCatalog>, std::uint64_t);
template AssetDepot::AssetRequest<material_graph::Generation> DataSystem::RequestMaterialPipelineAssetFromSnapshot(
    AssetDepot::AssetLink<material_graph::Generation>, own::shared_owner<const experiment::cooked::CookedAssetCatalog>, std::uint64_t);
