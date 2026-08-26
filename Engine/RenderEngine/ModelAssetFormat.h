#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

namespace ModelAssetFormat
{
	inline constexpr std::array<char, 4> kMagic{ 'C', 'E', 'M', 'A' };
	// 무버전 legacy payload를 v1로 간주하고, math::aabb/sphere 정본 전환부터
	// v2로 시작한다.
	inline constexpr std::uint32_t kFormatVersion = 2u;

	struct FileHeader final
	{
		std::array<char, 4> magic{ kMagic };
		std::uint32_t formatVersion{ kFormatVersion };
	};

	[[nodiscard]] inline constexpr bool IsCurrent(const FileHeader& header) noexcept
	{
		return header.magic == kMagic && header.formatVersion == kFormatVersion;
	}

	static_assert(sizeof(FileHeader) == 8u);
	static_assert(std::is_standard_layout_v<FileHeader>);
	static_assert(std::is_trivially_copyable_v<FileHeader>);
}
