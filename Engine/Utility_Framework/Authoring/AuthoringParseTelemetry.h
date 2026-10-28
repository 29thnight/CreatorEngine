#include "../../Containers/ContainerAPI.h"
#pragma once

#include "../Foundation/StandardLibrary.h"

namespace Authoring
{
	struct TextParseTelemetrySnapshot final
	{
		std::uint64_t calls{};
		ce::dynamic_array<std::string> contexts{};
	};

	// D6 runtime acceptance counter. Only actual text-to-tree parser entry points
	// increment this value; binary document decoding must not touch it.
	void RecordTextParserCall(std::string_view context) noexcept;
	[[nodiscard]] TextParseTelemetrySnapshot GetTextParseTelemetry() noexcept;
}
