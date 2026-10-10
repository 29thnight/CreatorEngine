#include "InputGraphAssetRuntime.h"
#include "../DataSystem.h"
#include "../Experiment/Cooked/CookedInputGraph.h"
#include <algorithm>

#include <exception>
#include <utility>

namespace AssetDepot::InputGraphDetail
{
    using Status = AssetRequestStatus;
    using Error = AssetRequestError;
    thread_local const InputGraphAssetWork* submitting{};

    void Notify(const own::shared_owner<AssetRequestState<Input::InputGraphProgram>>& consumer,
        Status status, Error error, const std::string& failure,
        const own::shared_owner<const Input::InputGraphProgram>& program = {})
    {
        std::lock_guard lock(consumer->mutex);
        if (consumer->status != Status::Pending)
        {
            return;
        }
        consumer->SetTerminalLocked(status);
        consumer->error = error;
        consumer->asset = program;
        try
        {
            consumer->message = failure;
        }
        catch (...)
        {
            consumer->message.clear();
        }
    }
}

own::shared_owner<const Input::InputGraphProgram> DataSystem::TryAcquireInputGraph(
    AssetDepot::AssetLink<Input::InputGraphProgram> link)
{
    if (!link.IsValid() || link.identity.subassetId.IsValid())
    {
        return {};
    }
    experiment::cooked::ResolvedAssetEntry resolved;
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
    {
        return {};
    }
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (!m_cookedCatalog || m_cookedCatalog->Find(link.ToReference(), resolved) !=
            experiment::cooked::AssetLookupStatus::Found)
        {
            return {};
        }
    }
    const AssetDepot::InputGraphAssetKey key{ resolved.entry.asset, resolved.blob, resolved.resolverRevision };
    const auto found = m_inputGraphAssets.find(key);
    return found == m_inputGraphAssets.end() ? own::shared_owner<const Input::InputGraphProgram>{} :
        found->second.live.lock();
}

AssetDepot::AssetRequest<Input::InputGraphProgram> DataSystem::RequestInputGraphAsync(
    AssetDepot::AssetLink<Input::InputGraphProgram> link)
{
    namespace cooked = experiment::cooked;
    using namespace AssetDepot::InputGraphDetail;
    auto consumer = own::make_shared<AssetDepot::AssetRequestState<Input::InputGraphProgram>>(m_assetRequestCounters);
    AssetDepot::AssetRequest<Input::InputGraphProgram> request(consumer);
    const auto fail = [&](Status status, Error error, const std::string& failure = {})
    {
        Notify(consumer, status, error, failure);
        return request;
    };
    if (!link.IsValid() || link.identity.subassetId.IsValid())
    {
        return fail(Status::Failed, Error::InvalidLink, "InputGraph requires a valid root asset GUID.");
    }
    cooked::ResolvedAssetEntry resolved;
    own::shared_owner<AssetDepot::InputGraphAssetWork> work;
    std::lock_guard lock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0u)
    {
        return fail(m_assetPreparationStopping ? Status::Cancelled : Status::Stale,
            m_assetPreparationStopping ? Error::ShuttingDown : Error::RevisionChanged);
    }
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        if (!m_cookedCatalog)
        {
            return fail(Status::Failed, Error::NotMounted, "No InputGraph asset set is mounted.");
        }
        const auto status = m_cookedCatalog->Find(link.ToReference(), resolved);
        if (status != cooked::AssetLookupStatus::Found)
        {
            return fail(Status::Failed, status == cooked::AssetLookupStatus::TypeMismatch ?
                Error::TypeMismatch : Error::NotMounted, "InputGraph typed asset link cannot be resolved.");
        }
    }
    if (resolved.blob.representation != cooked::kInputGraphRepresentation ||
        resolved.blob.schemaVersion != cooked::kInputGraphArtifactVersion ||
        resolved.blob.byteSize > cooked::kInputGraphMaxBytes || !resolved.byteSource)
    {
        return fail(Status::Failed, Error::UnsupportedRepresentation, "InputGraph requires a compatible CEIG artifact.");
    }
    if (!resolved.entry.dependencies.empty())
    {
        return fail(Status::Failed, Error::DependencyFailed, "InputGraph v1 cannot declare asset dependencies.");
    }
    try
    {
        std::erase_if(m_inputGraphAssets, [](const auto& pair)
        {
            return !pair.second.inFlight && pair.second.live.expired();
        });
        const AssetDepot::InputGraphAssetKey key{ resolved.entry.asset, resolved.blob, resolved.resolverRevision };
        auto& entry = m_inputGraphAssets[key];
        if (auto live = entry.live.lock())
        {
            Notify(consumer, Status::Ready, Error::None, {}, live);
            return request;
        }
        if (entry.inFlight)
        {
            entry.inFlight->consumers.emplace_back(consumer);
            consumer->completion = entry.inFlight->completion;
            return request;
        }
        work = own::make_shared<AssetDepot::InputGraphAssetWork>();
        work->key = key;
        work->resolved = std::move(resolved);
        work->epoch = m_assetPreparationEpoch;
        work->consumers.emplace_back(consumer);
        entry.inFlight = work;
        const auto previous = std::exchange(submitting, std::addressof(*work));
        try
        {
            job_group jobs;
            jobs.add([this, work]() { RunInputGraphAssetWork(work); });
            jobs.on_complete([this, work](std::exception_ptr)
            {
                if (AssetDepot::InputGraphDetail::submitting == std::addressof(*work))
                {
                    CompleteInputGraphAssetWorkLocked(work, Error::SubmissionFailed, "InputGraph work did not complete.");
                }
                else
                {
                    std::lock_guard completionLock(m_assetPreparationMutex);
                    CompleteInputGraphAssetWorkLocked(work, Error::SubmissionFailed, "InputGraph work did not complete.");
                }
            });
            work->completion = SubmitAssetWorkLocked(std::move(jobs));
        }
        catch (...)
        {
            CompleteInputGraphAssetWorkLocked(work, Error::SubmissionFailed, "InputGraph work could not be submitted.");
        }
        submitting = previous;
        consumer->completion = work->completion;
        return request;
    }
    catch (...)
    {
        return fail(Status::Failed, Error::SubmissionFailed, "InputGraph request allocation failed.");
    }
}

void DataSystem::RunInputGraphAssetWork(own::shared_owner<AssetDepot::InputGraphAssetWork> work)
{
    using Error = AssetDepot::AssetRequestError;
    std::string failure;
    own::shared_owner<const Input::InputGraphProgram> candidate;
    auto error = Error::None;
    try
    {
        std::vector<std::byte> bytes;
        auto resolved = work->resolved;
        std::uint64_t size{};
        experiment::cooked::Sha256Digest digest{};
        if (!experiment::cooked::CaptureArtifactSource(resolved.byteSource, resolved.blob.artifactPath, failure) ||
            !resolved.byteSource->Size(resolved.blob.artifactPath, size, failure))
        {
            error = Error::ReadFailed;
        }
        else if (size != resolved.blob.byteSize || size > experiment::cooked::kInputGraphMaxBytes)
        {
            error = Error::IntegrityFailed;
            failure = "InputGraph artifact size differs from its manifest.";
        }
        else
        {
            bytes.resize(static_cast<std::size_t>(size));
            if (!resolved.byteSource->ReadAt(resolved.blob.artifactPath, 0u, bytes, failure))
            {
                error = Error::ReadFailed;
            }
            else if (!experiment::cooked::ComputeSha256(bytes, digest, failure) || digest != resolved.blob.contentSha256)
            {
                error = Error::IntegrityFailed;
                failure = "InputGraph artifact content hash mismatch.";
            }
            else if (!experiment::cooked::ReadInputGraphArtifact(bytes,
                resolved.entry.asset.key.assetId, candidate, failure, work->key.resolverRevision))
            {
                error = Error::DecodeFailed;
            }
        }
    }
    catch (...)
    {
        error = Error::DecodeFailed;
        failure = "InputGraph decoding failed.";
    }
    std::lock_guard lock(m_assetPreparationMutex);
    CompleteInputGraphAssetWorkLocked(work, error, failure, candidate);
}

void DataSystem::CompleteInputGraphAssetWorkLocked(
    const own::shared_owner<AssetDepot::InputGraphAssetWork>& work,
    AssetDepot::AssetRequestError error, const std::string& failure,
    const own::shared_owner<const Input::InputGraphProgram>& candidate)
{
    using namespace AssetDepot::InputGraphDetail;
    if (work->complete)
    {
        return;
    }
    auto status = error == Error::None && candidate ? Status::Ready : Status::Failed;
    bool current{};
    {
        std::lock_guard catalogLock(m_cookedCatalogMutex);
        current = m_cookedCatalog && m_cookedCatalog->ResolverRevision() == work->key.resolverRevision;
    }
    if (m_assetPreparationStopping)
    {
        status = Status::Cancelled;
        error = Error::ShuttingDown;
    }
    else if (!current || work->epoch != m_assetPreparationEpoch || m_assetInvalidationDepth != 0u)
    {
        status = Status::Stale;
        error = Error::RevisionChanged;
    }
    if (status == Status::Ready)
    {
        for (const auto& [key, entry] : m_inputGraphAssets)
        {
            if (key.asset.key != work->key.asset.key)
            {
                continue;
            }
            if (const auto previous = entry.live.lock())
            {
                for (const auto& signal : previous->GetDefinition().signals)
                {
                    const auto& next = candidate->GetDefinition().signals;
                    const auto found = std::find_if(next.begin(), next.end(), [&](const auto& item)
                    {
                        return item.id == signal.id;
                    });
                    if (found != next.end() && found->type != signal.type)
                    {
                        status = Status::Failed;
                        error = Error::TypeMismatch;
                    }
                }
            }
        }
    }
    const auto asset = status == Status::Ready ? candidate : own::shared_owner<const Input::InputGraphProgram>{};
    const auto found = m_inputGraphAssets.find(work->key);
    if (found != m_inputGraphAssets.end() && found->second.inFlight && std::addressof(*found->second.inFlight) == std::addressof(*work))
    {
        found->second.inFlight.reset();
        if (asset)
        {
            found->second.live = asset;
        }
    }
    work->complete = true;
    for (const auto& weak : work->consumers)
    {
        if (const auto consumer = weak.lock())
        {
            Notify(consumer, status, error, error == Error::TypeMismatch ?
                "A stable InputSignal ID cannot change value type; the previous lease is retained." : failure, asset);
        }
    }
    work->consumers.clear();
}
