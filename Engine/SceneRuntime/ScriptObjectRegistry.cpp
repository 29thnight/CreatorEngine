#include "ScriptObjectRegistry.h"
#include "Entity.h"
#include "../RenderEngine/DataSystem.h"
#include "../RenderEngine/Texture.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

ScriptObjectRegistry& ScriptObjectRegistry::Get()
{
	static ScriptObjectRegistry instance;
	return instance;
}

ScriptObjectHandle ScriptObjectRegistry::Register(Entity* object)
{
	if (nullptr == object)
	{
		return {};
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	// 역방향 map 대신 선형 탐색(트랙 E4 — ScriptObjectRegistry.h 클래스 주석 참고).
	// object는 위에서 이미 nullptr을 걸렀으므로, 비어 있는(tombstone) 슬롯의
	// object==nullptr과 절대 겹치지 않는다.
	for (uint32_t i = 0; i < m_slots.size(); ++i)
	{
		if (m_slots[i].object == object)
		{
			return { i, m_slots[i].generation };
		}
	}

	uint32_t index;
	if (!m_freeSlots.empty())
	{
		index = m_freeSlots.back();
		m_freeSlots.pop_back();
	}
	else
	{
		index = static_cast<uint32_t>(m_slots.size());
		m_slots.push_back({});
	}

	Slot& slot = m_slots[index];
	slot.object = object;

	// 세대 0은 무효를 뜻하므로 건너뛴다. 재사용마다 1씩 올라간다.
	if (0 == slot.generation)
	{
		slot.generation = 1;
	}

	return { index, slot.generation };
}

void ScriptObjectRegistry::Unregister(Entity* object)
{
	if (nullptr == object)
	{
		return;
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	for (uint32_t i = 0; i < m_slots.size(); ++i)
	{
		if (m_slots[i].object != object)
		{
			continue;
		}

		Slot& slot = m_slots[i];
		slot.object = nullptr;

		// 세대를 올려 이 슬롯을 가리키던 기존 핸들을 전부 무효화한다.
		// 0으로 되돌아가면 "무효" 값과 겹치므로 건너뛴다.
		++slot.generation;
		if (0 == slot.generation)
		{
			slot.generation = 1;
		}

		m_freeSlots.push_back(i);
		return;
	}
	// 못 찾으면 조용히 넘어간다 — 이미 Unregister된 객체를 다시 부르는 경로가
	// 있다(Entity::Destroy의 재귀 파괴 + 과거 ClrHost.cpp의 명시적 호출이
	// 겹치던 자리). idempotent해야 두 번째 호출이 안전하다.
}

Entity* ScriptObjectRegistry::Resolve(ScriptObjectHandle handle) const
{
	if (!handle.IsValid())
	{
		return nullptr;
	}

	std::lock_guard<std::mutex> guard(m_mutex);

	if (handle.index >= m_slots.size())
	{
		return nullptr;
	}

	const Slot& slot = m_slots[handle.index];
	if (slot.generation != handle.generation)
	{
		return nullptr;   // 슬롯이 재사용됨 = 낡은 핸들
	}

	return slot.object;
}

void ScriptObjectRegistry::Clear()
{
	std::lock_guard<std::mutex> guard(m_mutex);

	// 슬롯 자체는 남기고 세대만 올린다. 밖에 나가 있는 핸들이 되살아나지 않게 하기 위함이다.
	for (size_t i = 0; i < m_slots.size(); ++i)
	{
		// ★ DontDestroyOnLoad 오브젝트는 건너뛴다 (트랙 L · L3 잔여, 2026-08-20 실측).
		//
		// 이 함수는 씬 언로드(ClrHost::NotifySceneUnload)에서 불린다. 그런데 DDOL
		// 오브젝트는 **그 언로드를 살아서 건넌다** — 여기서 세대를 올리면 살아 있는
		// 오브젝트를 가리키던 관리 측 핸들이 죽고, 스크립트는 자기 GameObject를
		// 잃은 채 계속 돈다. 조용하다: SweepOrphans는 이 함수보다 **먼저** 돌아
		// (위 m_fnSceneUnload) 그 스크립트를 고아로 잡지 않고, 그 뒤로는 GameObject
		// 경유 API가 전부 무응답이 된다.
		//
		// 실측: DDOL 스크립트가 씬 전환 뒤 GameObject.Name을 빈 문자열로 받았고,
		// 이송 재부착 통지도 IsAlive=false로 거부됐다(goAlive=False).
		//
		// 진짜 파괴의 등록 해제 정본은 Entity::Destroy()다(이 파일 상단 주석) —
		// 그 경로는 DDOL 이송을 지나지 않으므로, 여기서 살아남는 것을 건드리지
		// 않아도 죽은 슬롯이 새는 일은 없다.
		if (nullptr != m_slots[i].object && m_slots[i].object->IsDontDestroyOnLoad())
		{
			continue;
		}

		if (nullptr != m_slots[i].object)
		{
			m_slots[i].object = nullptr;
			++m_slots[i].generation;
			if (0 == m_slots[i].generation)
			{
				m_slots[i].generation = 1;
			}
			m_freeSlots.push_back(static_cast<uint32_t>(i));
		}
	}
}

size_t ScriptObjectRegistry::LiveCount() const
{
	std::lock_guard<std::mutex> guard(m_mutex);

	// 역방향 map이 없으므로(트랙 E4) 슬롯을 훑어 센다 — 진단용 호출이라
	// 빈도가 낮고, O(n)이어도 문제 되지 않는다.
	size_t count = 0;
	for (const auto& slot : m_slots)
	{
		if (nullptr != slot.object)
		{
			++count;
		}
	}
	return count;
}

namespace
{
    constexpr std::uint32_t kTextureAssetType =
        static_cast<std::uint32_t>(experiment::cooked::CookedAssetKind::Texture);
    constexpr std::uint32_t kTextureRequestType = 0x80000000u | kTextureAssetType;

    experiment::AssetId DecodeScriptAssetId(ScriptAssetId value)
    {
        experiment::AssetId result{};
        // Explicit byte order, matching AssetId in ScriptCore. Do not memcpy a
        // platform Guid layout or depend on the CPU's integer endianness.
        for (std::size_t index = 0; index < 8u; ++index)
        {
            result.value.data[index] = static_cast<std::uint8_t>(value.first >> (index * 8u));
            result.value.data[index + 8u] = static_cast<std::uint8_t>(value.second >> (index * 8u));
        }
        return result;
    }

    ScriptAssetId EncodeScriptAssetId(const experiment::AssetId& value)
    {
        ScriptAssetId result{};
        for (std::size_t index = 0; index < 8u; ++index)
        {
            result.first |= static_cast<std::uint64_t>(value.value.data[index]) << (index * 8u);
            result.second |= static_cast<std::uint64_t>(value.value.data[index + 8u]) << (index * 8u);
        }
        return result;
    }
}

void ScriptObjectRegistry::BeginAssetSession(DataSystem* dataSystem)
{
    EndAssetSession();
    std::lock_guard lock(m_assetMutex);
    m_assetDataSystem = dataSystem;
    m_assetThread = std::this_thread::get_id();
    m_assetsActive = true;
}

void ScriptObjectRegistry::EndAssetSession()
{
    std::lock_guard lock(m_assetMutex);
    m_assetsActive = false;
    m_assetDataSystem = nullptr;
    for (auto& slot : m_assetSlots)
    {
        if (slot.type != 0u)
        {
            ReleaseAssetLocked(slot);
        }
    }
    // Keep tombstones and their generations across CLR reinitialization.
}

ScriptAssetResult ScriptObjectRegistry::CheckAssetSessionLocked() const
{
    if (!m_assetsActive || m_assetDataSystem == nullptr
        || DataSystem::GetIfAlive() != m_assetDataSystem)
    {
        return ScriptAssetResult::Unavailable;
    }
    if (m_assetThread != std::this_thread::get_id())
    {
        return ScriptAssetResult::WrongThread;
    }
    return ScriptAssetResult::Success;
}

ScriptObjectRegistry::AssetSlot* ScriptObjectRegistry::FindAssetLocked(ScriptAssetToken token)
{
    if (token.generation == 0u || token.index >= m_assetSlots.size()
        || (token.type != kTextureAssetType && token.type != kTextureRequestType))
    {
        return nullptr;
    }
    auto& slot = m_assetSlots[token.index];
    return slot.generation == token.generation && slot.type == token.type ? &slot : nullptr;
}

ScriptAssetToken ScriptObjectRegistry::InsertAssetLocked(own::shared_owner<const Texture> texture,
    AssetDepot::AssetRequest<Texture> request, bool isRequest)
{
    std::size_t index = 0u;
    for (; index < m_assetSlots.size(); ++index)
    {
        if (m_assetSlots[index].type == 0u && m_assetSlots[index].generation != 0u)
        {
            break;
        }
    }
    if (index == m_assetSlots.size())
    {
        if (index >= std::numeric_limits<std::uint32_t>::max())
        {
            throw std::length_error("Managed asset owner slots exhausted.");
        }
        m_assetSlots.emplace_back();
    }
    auto& slot = m_assetSlots[index];
    slot.texture = std::move(texture);
    slot.request = std::move(request);
    slot.type = isRequest ? kTextureRequestType : kTextureAssetType;
    return { static_cast<std::uint32_t>(index), slot.generation, slot.type };
}

void ScriptObjectRegistry::ReleaseAssetLocked(AssetSlot& slot)
{
    slot.request.Cancel();
    slot.request = {};
    slot.texture.reset();
    slot.type = 0u;
    // On wrap retire this slot permanently instead of reviving an ancient token.
    ++slot.generation;
}

ScriptAssetResult ScriptObjectRegistry::RequestAsset(const ScriptAssetLink& link,
    const ScriptTextureAssetVariant& variant, bool residentOnly, ScriptAssetToken& token)
{
    token = {};
    std::lock_guard lock(m_assetMutex);
    const auto available = CheckAssetSessionLocked();
    if (available != ScriptAssetResult::Success)
    {
        return available;
    }
    if (link.kind != kTextureAssetType)
    {
        return ScriptAssetResult::UnsupportedType;
    }
    AssetDepot::AssetLink<Texture> typed{
        { DecodeScriptAssetId(link.asset), DecodeScriptAssetId(link.subasset) } };
    if (link.reserved != 0u || !typed.IsValid())
    {
        return ScriptAssetResult::InvalidLink;
    }
    if (variant.colorSpace > static_cast<std::uint32_t>(AssetDepot::TextureAssetColorSpace::Srgb)
        || variant.compress > 1u)
    {
        return ScriptAssetResult::InvalidArgument;
    }
    const AssetDepot::TextureAssetVariant options{
        static_cast<AssetDepot::TextureAssetColorSpace>(variant.colorSpace), variant.compress != 0u, variant.role };
    if (residentOnly)
    {
        auto texture = m_assetDataSystem->TryAcquire<Texture>(typed, options);
        if (!texture)
        {
            return ScriptAssetResult::NotResident;
        }
        token = InsertAssetLocked(std::move(texture), {}, false);
    }
    else
    {
        token = InsertAssetLocked({}, m_assetDataSystem->RequestAsync<Texture>(typed, options), true);
    }
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::SnapshotAssetRequest(ScriptAssetToken token,
    ScriptAssetRequestSnapshot& snapshot, char* message, int capacity)
{
    snapshot = {};
    if (capacity < 0 || (capacity > 0 && message == nullptr))
    {
        return ScriptAssetResult::InvalidArgument;
    }
    std::lock_guard lock(m_assetMutex);
    auto* slot = FindAssetLocked(token);
    if (slot == nullptr || token.type != kTextureRequestType)
    {
        return ScriptAssetResult::InvalidToken;
    }
    const auto result = slot->request.Snapshot();
    const auto completion = slot->request.Completion();
    snapshot.status = static_cast<std::int32_t>(result.status);
    snapshot.error = static_cast<std::int32_t>(result.error);
    snapshot.workComplete = !completion.valid() || completion.is_complete() ? 1 : 0;
    snapshot.messageBytes = static_cast<std::int32_t>(std::min<std::size_t>(
        result.message.size(), static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())));
    if (capacity > 0)
    {
        const auto count = std::min<std::size_t>(result.message.size(), static_cast<std::size_t>(capacity - 1));
        std::memcpy(message, result.message.data(), count);
        message[count] = '\0';
    }
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::AcquireAssetResult(ScriptAssetToken request, ScriptAssetToken& owner)
{
    owner = {};
    std::lock_guard lock(m_assetMutex);
    auto* slot = FindAssetLocked(request);
    if (slot == nullptr || request.type != kTextureRequestType)
    {
        return ScriptAssetResult::InvalidToken;
    }
    const auto result = slot->request.Snapshot();
    if (result.status != AssetDepot::AssetRequestStatus::Ready || !result.asset)
    {
        return ScriptAssetResult::NotResident;
    }
    owner = InsertAssetLocked(result.asset, {}, false);
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::CancelAssetRequest(ScriptAssetToken request)
{
    std::lock_guard lock(m_assetMutex);
    auto* slot = FindAssetLocked(request);
    if (slot == nullptr || request.type != kTextureRequestType)
    {
        return ScriptAssetResult::InvalidToken;
    }
    slot->request.Cancel();
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::ReleaseAsset(ScriptAssetToken token)
{
    std::lock_guard lock(m_assetMutex);
    auto* slot = FindAssetLocked(token);
    if (slot == nullptr)
    {
        return ScriptAssetResult::InvalidToken;
    }
    // Never fetch/recreate DataSystem here, including after engine shutdown.
    // This drops only CPU owners. GPU retirement remains the renderer's job.
    ReleaseAssetLocked(*slot);
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::ReadTexture(ScriptAssetToken token, ScriptTextureDescriptor& descriptor)
{
    descriptor = {};
    std::lock_guard lock(m_assetMutex);
    auto* slot = FindAssetLocked(token);
    if (slot == nullptr || token.type != kTextureAssetType || !slot->texture)
    {
        return ScriptAssetResult::InvalidToken;
    }
    const auto image = slot->texture->GetImageDescription();
    descriptor = { image.Width(), image.Height(), image.MipLevels(), image.ArraySize(), image.IsCube() ? 1u : 0u };
    return ScriptAssetResult::Success;
}

ScriptAssetResult ScriptObjectRegistry::ListAssetRoots(std::uint64_t mount, std::uint32_t kind,
    ScriptAssetLink* links, int capacity, int& count)
{
    count = 0;
    if (capacity < 0 || (capacity > 0 && links == nullptr))
    {
        return ScriptAssetResult::InvalidArgument;
    }
    std::lock_guard lock(m_assetMutex);
    const auto available = CheckAssetSessionLocked();
    if (available != ScriptAssetResult::Success)
    {
        return available;
    }
    if (kind != kTextureAssetType)
    {
        return ScriptAssetResult::UnsupportedType;
    }
    const auto roots = m_assetDataSystem->ListRootLinks<Texture>({ mount });
    if (roots.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    {
        return ScriptAssetResult::InternalError;
    }
    count = static_cast<int>(roots.size());
    for (int index = 0; index < std::min(count, capacity); ++index)
    {
        links[index] = { EncodeScriptAssetId(roots[index].identity.assetId),
            EncodeScriptAssetId(roots[index].identity.subassetId), kTextureAssetType, 0u };
    }
    return ScriptAssetResult::Success;
}
