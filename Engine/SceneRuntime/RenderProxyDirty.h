#pragma once

#include <cstdint>

// TransformUpdatePlan X8: gameplay writes publish semantic dirtiness; the
// render bridge consumes the OR-ed mask once, after the final transform/layout
// resolve. The mask deliberately describes why a proxy is stale, not its type.
enum class ProxyDirty : std::uint8_t
{
	None       = 0,
	Transform  = 1u << 0,
	Material   = 1u << 1,
	Visibility = 1u << 2,
	LOD        = 1u << 3,
	Payload    = 1u << 4,
	All        = 0x1fu,
};

constexpr ProxyDirty operator|(ProxyDirty lhs, ProxyDirty rhs) noexcept
{
	return static_cast<ProxyDirty>(
		static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

constexpr ProxyDirty& operator|=(ProxyDirty& lhs, ProxyDirty rhs) noexcept
{
	lhs = lhs | rhs;
	return lhs;
}

constexpr bool AnyProxyDirty(ProxyDirty value) noexcept
{
	return ProxyDirty::None != value;
}

struct RenderProxyCommitMetrics
{
	std::uint64_t registered = 0;
	std::uint64_t pending = 0;
	std::uint64_t publishCalls = 0;
	std::uint64_t deduplicated = 0;
	std::uint64_t commitPasses = 0;
	std::uint64_t committed = 0;
	std::uint64_t staleTickets = 0;
	std::uint64_t lastDrained = 0;
	std::uint64_t lastCommitted = 0;
	std::uint64_t lastStale = 0;
	ProxyDirty lastMask = ProxyDirty::None;
};
