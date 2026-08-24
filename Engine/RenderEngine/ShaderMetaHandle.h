#pragma once

#include <cstdint>

// DataSystem이 소유하는 ShaderMeta cache slot의 세대 식별자다. slot 0과
// generation 0은 항상 invalid이며, reload/remove 뒤 이전 handle은 resolve되지 않는다.
struct ShaderMetaHandle
{
	std::uint32_t slot{};
	std::uint32_t generation{};

	constexpr bool IsValid() const noexcept
	{
		return 0 != slot && 0 != generation;
	}

	bool operator==(const ShaderMetaHandle&) const = default;
};
