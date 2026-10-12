#include "DataSystem.h"

#include "ShaderMeta.h"
#include "Material.h"
#include "AssetDepot/LegacyResourceCharges.h"
#include "ShaderPermutationDomain.h"
#include "../Utility_Framework/LogSystem.h"
#include "../Utility_Framework/PathFinder.h"

#include <cstdio>
#include <limits>

namespace
{
    // Capacity accounting bounds the cache's retained CPU payload, not allocator
    // overhead or physically reclaimed bytes. Consumer owners are not cache bytes.
    class ShaderMetaCharge
    {
    public:
        void Add(std::size_t bytes) noexcept
        {
            bytes_ = bytes > SIZE_MAX - bytes_ ? SIZE_MAX : bytes_ + bytes;
        }
        void String(const std::string& value) noexcept
        {
            Add(value.capacity());
            Add(1);
        }
        void Path(const std::filesystem::path& value) noexcept
        {
            const auto count = value.native().capacity();
            Add(count > SIZE_MAX / sizeof(std::filesystem::path::value_type)
                ? SIZE_MAX : count * sizeof(std::filesystem::path::value_type));
        }
        template<class T>
        void Vector(const std::vector<T>& values) noexcept
        {
            Add(values.capacity() > SIZE_MAX / sizeof(T) ? SIZE_MAX : values.capacity() * sizeof(T));
        }
        std::size_t Bytes() const noexcept { return bytes_; }

    private:
        std::size_t bytes_{};
    };

    std::size_t RetainedShaderMetaBytes(const ShaderMeta& meta) noexcept
    {
        ShaderMetaCharge charge;
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
        if (meta.generatedMaterial)
        {
            charge.String(meta.generatedMaterial->generation);
            charge.String(meta.generatedMaterial->sourceSha256);
            charge.Vector(meta.generatedMaterial->samplers);
            for (const auto& sampler : meta.generatedMaterial->samplers)
            {
                charge.String(sampler.name);
                charge.String(sampler.description);
            }
        }
        return charge.Bytes();
    }
}

std::uint64_t DataSystem::NextShaderMetaUseLocked() const noexcept
{
    if (m_shaderMetaUseSerial == (std::numeric_limits<std::uint64_t>::max)())
    {
        for (const auto& slot : m_shaderMetaSlots)
        {
            slot.lastUse = 0;
        }
        m_shaderMetaUseSerial = 0;
    }
    return ++m_shaderMetaUseSerial;
}

void DataSystem::TrimShaderMetaRetainedLocked(std::size_t targetBytes,
    std::vector<own::shared_owner<const ShaderMeta>>& released)
{
    // The caller reserves one entry per slot before making any cache mutation.
    while (m_shaderMetaRetainedBytes > targetBytes)
    {
        ShaderMetaCacheSlot* oldest = nullptr;
        for (auto& slot : m_shaderMetaSlots)
        {
            if (slot.retained && (!oldest || slot.lastUse < oldest->lastUse))
            {
                oldest = &slot;
            }
        }
        if (!oldest)
        {
            break;
        }
        m_shaderMetaRetainedBytes -= oldest->retainedBytes;
        oldest->retainedBytes = 0;
        released.push_back(std::move(oldest->retained));
    }
}

void DataSystem::RetainShaderMetaLocked(ShaderMetaCacheSlot& slot,
    const own::shared_owner<const ShaderMeta>& owner, std::size_t charge,
    std::vector<own::shared_owner<const ShaderMeta>>& released)
{
    slot.lastUse = NextShaderMetaUseLocked();
    if (slot.retained || charge > m_shaderMetaRetainedBudgetBytes)
    {
        return;
    }
    // Subtract before adding to avoid overflow even at a SIZE_MAX budget.
    TrimShaderMetaRetainedLocked(m_shaderMetaRetainedBudgetBytes - charge, released);
    slot.retained = owner;
    slot.retainedBytes = charge;
    m_shaderMetaRetainedBytes += charge;
}

own::shared_owner<const ShaderMeta> DataSystem::LoadShaderMetaOwner(FileGuid guid,
    ShaderMetaHandle& outHandle, std::string& outError)
{
    outHandle = {};
    const auto catalog = GetCookedCatalog();
    const AssetDepot::AssetLink<ShaderMeta> link{ { experiment::AssetId{ guid.m_guid }, {} } };
    experiment::cooked::ResolvedAssetEntry resolved;
    if (catalog && catalog->Find(link.ToReference(), resolved) != experiment::cooked::AssetLookupStatus::NotMounted)
    {
        outError = "Mounted metadata requires an explicit prepared code-program owner; a descriptor is not shader readiness.";
        return {};
    }

    std::uint64_t loadEpoch{};
    std::uint64_t resolverRevision{};
    ShaderMetaHandle requested;
    own::shared_owner<AssetMetaRegistry> registry;
    std::vector<own::shared_owner<const ShaderMeta>> released;
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
        {
            outError = "Asset preparation is shutting down.";
            return {};
        }
        registry = SnapshotAssetMetaRegistry();
        if (!registry || FileGuid{} == guid)
        {
            outError = "Shader metadata requires a catalog GUID and an initialized registry.";
            return {};
        }
        loadEpoch = m_assetPreparationEpoch;
        resolverRevision = m_assetDepotRevision;
        std::lock_guard lock(m_shaderMetaMutex);
        released.reserve(m_shaderMetaSlots.size());
        auto cached = m_shaderMetaSlotByGuid.find(guid);
        if (cached != m_shaderMetaSlotByGuid.end())
        {
            auto& slot = m_shaderMetaSlots[cached->second];
            if (slot.resolverRevision != resolverRevision)
            {
                // Remount invalidates lookup identity, including an expired weak
                // entry. Existing consumer owners retain the prior description.
                if (slot.retained)
                {
                    released.push_back(std::move(slot.retained));
                }
                m_shaderMetaRetainedBytes -= slot.retainedBytes;
                slot.retainedBytes = 0;
                slot.current.reset();
                slot.generation = 0;
                slot.documentDigest = {};
                slot.hasDocumentDigest = false;
            }
            if (slot.occupied && slot.guid == guid && slot.generation != 0)
            {
                if (auto owner = slot.current.lock())
                {
                    RetainShaderMetaLocked(slot, owner, RetainedShaderMetaBytes(*owner), released);
                    outHandle = { cached->second + 1, slot.generation };
                    outError.clear();
                    return owner;
                }
            }
        }
        if (cached == m_shaderMetaSlotByGuid.end())
        {
            if (m_shaderMetaSlots.size() >= (std::numeric_limits<std::uint32_t>::max)())
            {
                outError = "Shader metadata slot identity space is exhausted.";
                return {};
            }
            // Stage both allocating containers before publishing an occupied slot.
            // A failed map insertion has not consumed a slot or a generation.
            m_shaderMetaSlots.reserve(m_shaderMetaSlots.size() + 1);
            const auto slotIndex = m_shaderMetaFreeSlots.empty()
                ? static_cast<std::uint32_t>(m_shaderMetaSlots.size()) : m_shaderMetaFreeSlots.back();
            cached = m_shaderMetaSlotByGuid.emplace(guid, slotIndex).first;
            if (m_shaderMetaFreeSlots.empty())
            {
                m_shaderMetaSlots.emplace_back();
            }
            else
            {
                m_shaderMetaFreeSlots.pop_back();
            }
            auto& slot = m_shaderMetaSlots[slotIndex];
            slot.guid = guid;
            slot.occupied = true;
        }
        auto& slot = m_shaderMetaSlots[cached->second];
        if (slot.generation == 0)
        {
            if (m_shaderMetaGenerationSerial >= (std::numeric_limits<std::uint32_t>::max)())
            {
                outError = "Shader metadata generation identity space is exhausted.";
                return {};
            }
            // This serial survives Finalize. Reused slots and reinitialization can
            // never make a historical numeric handle resolve a different object.
            slot.generation = static_cast<std::uint32_t>(++m_shaderMetaGenerationSerial);
        }
        slot.resolverRevision = resolverRevision;
        requested = { cached->second + 1, slot.generation };
    }

    const file::path sourcePath = registry->GetPath(guid);
    if (sourcePath.empty())
    {
        outError = "Shader metadata source identity was not found: " + guid.ToString();
        return {};
    }
    file::path path = ResolveCookedArtifact(experiment::AssetId{ guid.m_guid });
    if (path.empty() && PathFinder::IsAssetAuthoringEnabled())
    {
        path = sourcePath;
    }
    if (path.empty())
    {
        outError = "Shader metadata runtime path was not found: " + guid.ToString();
        return {};
    }
    ShaderMeta loaded;
    std::array<std::uint8_t, 32> documentDigest{};
    if (!ShaderMetaLoader::LoadFile(path, sourcePath, guid, loaded, outError, &documentDigest))
    {
        return {};
    }
    ShaderMetaPermutationStats stats;
    if (!ShaderPermutationDomain::Measure(loaded, stats, outError))
    {
        return {};
    }
    // No mutable alias exists after this publication factory; the stack value is moved.
    auto owner = own::make_shared<const ShaderMeta>(std::move(loaded));
    {
        std::lock_guard preparationLock(m_assetPreparationMutex);
        if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || loadEpoch != m_assetPreparationEpoch ||
            resolverRevision != m_assetDepotRevision)
        {
            outError = "Shader metadata changed while being prepared.";
            return {};
        }
        std::lock_guard lock(m_shaderMetaMutex);
        const auto cached = m_shaderMetaSlotByGuid.find(guid);
        if (cached == m_shaderMetaSlotByGuid.end() || cached->second != requested.slot - 1)
        {
            outError = "Shader metadata was removed while being prepared.";
            return {};
        }
        auto& slot = m_shaderMetaSlots[cached->second];
        if (!slot.occupied || slot.guid != guid || slot.generation != requested.generation ||
            slot.resolverRevision != resolverRevision)
        {
            outError = "Shader metadata generation changed while being prepared.";
            return {};
        }
        released.reserve(released.size() + m_shaderMetaSlots.size());
        if (slot.hasDocumentDigest && slot.documentDigest != documentDigest)
        {
            outError = "Shader metadata input changed without generation invalidation.";
            return {};
        }
        if (auto accepted = slot.current.lock())
        {
            owner = std::move(accepted);
        }
        else
        {
            slot.current = owner;
            slot.documentDigest = documentDigest;
            slot.hasDocumentDigest = true;
        }
        RetainShaderMetaLocked(slot, owner, RetainedShaderMetaBytes(*owner), released);
        // Copy/move this real pin to the caller even when the cache budget is zero.
        outHandle = requested;
        outError.clear();
    }
    std::printf("[shadermeta.document] source=%s guid=%s\n",
        path.lexically_normal() != sourcePath.lexically_normal() ? "cooked" : "authoring", guid.ToString().c_str());
    Debug::PrintLog(spdlog::level::info, "ShaderMeta loaded: " + owner->name + " [" + guid.ToString()
        + "] variants/pass=" + std::to_string(stats.variantsPerPass)
        + ", compile requests=" + std::to_string(stats.compileRequests));
    if (stats.compileRequests > ShaderPermutationDomain::kDefaultBuildCompileLimit)
    {
        Debug::PrintLog(spdlog::level::warn, "ShaderMeta build permutation limit exceeded: "
            + owner->name + " requests=" + std::to_string(stats.compileRequests));
    }
    return owner;
}

own::shared_owner<const ShaderMeta> DataSystem::ResolveShaderMeta(ShaderMetaHandle handle) const
{
    if (!handle.IsValid())
    {
        return {};
    }
    std::lock_guard preparationLock(m_assetPreparationMutex);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0)
    {
        return {};
    }
    std::lock_guard lock(m_shaderMetaMutex);
    const auto slotIndex = handle.slot - 1;
    if (slotIndex >= m_shaderMetaSlots.size())
    {
        return {};
    }
    const auto& slot = m_shaderMetaSlots[slotIndex];
    const auto current = m_shaderMetaSlotByGuid.find(slot.guid);
    if (!slot.occupied || slot.generation != handle.generation || slot.resolverRevision != m_assetDepotRevision ||
        (!slot.codeProgram && (current == m_shaderMetaSlotByGuid.end() || current->second != slotIndex)))
    {
        return {};
    }
    auto owner = slot.current.lock();
    if (!owner || owner->guid != slot.guid)
    {
        return {};
    }
    slot.lastUse = NextShaderMetaUseLocked();
    return owner;
}

bool DataSystem::LoadShaderMetaGUID(FileGuid guid, ShaderMeta& outMeta, std::string& outError)
{
    ShaderMetaHandle handle;
    const auto owner = LoadShaderMetaOwner(guid, handle, outError);
    if (!owner)
    {
        return false;
    }
    outMeta = *owner;
    return true;
}

bool DataSystem::PublishCodeShaderReload(const own::shared_owner<const LX::Runtime::ShaderGeneration>& expected,
    own::shared_owner<const LX::Runtime::ShaderGeneration>& shader, std::string& error)
{
    if (!expected || !shader || expected->meta.guid != shader->meta.guid)
    {
        error = "Shader reload identity changed";
        return false;
    }
    std::lock_guard admission(m_assetPreparationMutex);
    std::lock_guard materials(m_materialMutex);
    std::lock_guard metadata(m_shaderMetaMutex);
    const auto found = m_shaderMetaSlotByGuid.find(shader->meta.guid);
    if (m_assetPreparationStopping || m_assetInvalidationDepth != 0 || m_assetRootHandoffs != 0u
        || found == m_shaderMetaSlotByGuid.end()
        || m_shaderMetaGenerationSerial >= (std::numeric_limits<std::uint32_t>::max)())
    {
        error = "Shader reload cannot publish while the owner is unavailable";
        return false;
    }
    auto& slot = m_shaderMetaSlots[found->second];
    if (expected->codeHandle.generation != slot.generation)
    {
        error = "Shader reload was superseded by a newer accepted generation";
        return false;
    }
    std::vector<own::shared_owner<const ShaderMeta>> released;
    released.reserve(m_shaderMetaSlots.size() + 1u);
    auto replacement = own::make_shared<LX::Runtime::ShaderGeneration>(*shader);
    replacement->codeHandle = { found->second + 1u, static_cast<std::uint32_t>(m_shaderMetaGenerationSerial + 1u) };
    auto updated = Materials;
    for (auto& [key, entry] : updated)
    {
        const auto current = asset_cache_detail::Acquire(entry, m_assetDepotRevision);
        const auto instance = current ? current->GetLXMaterialInstance() : nullptr;
        if (!instance || current->HasMaterialGraph() || &*instance->shader != &*expected)
        {
            continue;
        }
        auto candidate = own::make_shared<Material>(*current);
        if (!candidate->ReloadCodeShader(replacement, error))
        {
            return false;
        }
        entry.current = own::weak_owner<const Material>(candidate);
        entry.retained = std::move(candidate);
        entry.retainedBytes = asset_cache_detail::LegacyMaterialRetainedBytes(*entry.retained);
        entry.publication = asset_cache_detail::NextUse();
        entry.lastUse = entry.publication;
    }
    auto meta = own::make_shared<const ShaderMeta>(replacement->meta);
    const auto charge = RetainedShaderMetaBytes(*meta);
    m_shaderMetaRetainedBytes -= slot.retainedBytes;
    released.push_back(std::move(slot.retained));
    slot.retainedBytes = 0u;
    slot.current = own::weak_owner<const ShaderMeta>(meta);
    RetainShaderMetaLocked(slot, meta, charge, released);
    asset_cache_detail::Trim(updated, kLegacyMaterialBudgetBytes, kLegacyCacheBudgetEntries);
    slot.generation = replacement->codeHandle.generation;
    slot.codeProgram = !!replacement->codeProgram;
    slot.resolverRevision = m_assetDepotRevision;
    slot.hasDocumentDigest = false;
    ++m_shaderMetaGenerationSerial;
    Materials.swap(updated);
    m_materialAssetRevision.fetch_add(1, std::memory_order_relaxed);
    shader = std::move(replacement);
    error.clear();
    return true;
}

void DataSystem::InvalidateShaderMeta(FileGuid guid, bool remove)
{
    if (FileGuid{} == guid)
    {
        return;
    }
    own::shared_owner<const ShaderMeta> released;
    {
        std::lock_guard lock(m_shaderMetaMutex);
        const auto found = m_shaderMetaSlotByGuid.find(guid);
        if (found == m_shaderMetaSlotByGuid.end())
        {
            return;
        }
        if (remove)
        {
            m_shaderMetaFreeSlots.reserve(m_shaderMetaFreeSlots.size() + 1);
        }
        auto& slot = m_shaderMetaSlots[found->second];
        released = std::move(slot.retained);
        m_shaderMetaRetainedBytes -= slot.retainedBytes;
        slot.retainedBytes = 0;
        slot.current.reset();
        slot.generation = 0;
        slot.hasDocumentDigest = false;
        slot.documentDigest = {};
        if (remove)
        {
            slot.guid = {};
            slot.occupied = false;
            m_shaderMetaFreeSlots.push_back(found->second);
            m_shaderMetaSlotByGuid.erase(found);
        }
    }
}

void DataSystem::SetShaderMetaRetainedBudgetBytes(std::size_t bytes)
{
    std::vector<own::shared_owner<const ShaderMeta>> released;
    {
        std::lock_guard lock(m_shaderMetaMutex);
        if (m_shaderMetaRetainedBytes > bytes)
        {
            released.reserve(m_shaderMetaSlots.size());
        }
        m_shaderMetaRetainedBudgetBytes = bytes;
        TrimShaderMetaRetainedLocked(bytes, released);
    }
}

std::size_t DataSystem::ShaderMetaRetainedBytes() const
{
    std::lock_guard lock(m_shaderMetaMutex);
    return m_shaderMetaRetainedBytes;
}

std::size_t DataSystem::ShaderMetaRetainedBudgetBytes() const
{
    std::lock_guard lock(m_shaderMetaMutex);
    return m_shaderMetaRetainedBudgetBytes;
}

void DataSystem::ClearShaderMetaCache()
{
    std::vector<ShaderMetaCacheSlot> released;
    {
        std::lock_guard lock(m_shaderMetaMutex);
        released.swap(m_shaderMetaSlots);
        m_shaderMetaSlotByGuid.clear();
        m_shaderMetaFreeSlots.clear();
        m_shaderMetaRetainedBytes = 0;
        m_shaderMetaUseSerial = 0;
        // m_shaderMetaGenerationSerial deliberately survives teardown.
    }
}
