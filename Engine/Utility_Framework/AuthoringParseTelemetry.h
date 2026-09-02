#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Authoring
{
	struct TextParseTelemetrySnapshot final
	{
		std::uint64_t calls{};
		std::vector<std::string> contexts{};
	};

	// D6 runtime acceptance counter. Only actual text-to-tree parser entry points
	// increment this value; binary document decoding must not touch it.
	void RecordTextParserCall(std::string_view context) noexcept;
	[[nodiscard]] TextParseTelemetrySnapshot GetTextParseTelemetry() noexcept;
}
