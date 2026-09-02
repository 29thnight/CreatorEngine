#pragma once

#include "AuthoringReadNode.h"
#include "AuthoringWriteNode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Authoring
{
	// D6 runtime document envelope. Authoring YAML is parsed once by AssetCooker;
	// Player consumes this deterministic tree encoding without entering ryml's
	// text parser. This is deliberately not a legacy reader: unsupported versions
	// fail and must be recooked.
	inline constexpr std::array<std::byte, 4> kCookedDocumentMagic{
		std::byte{ 'C' }, std::byte{ 'E' }, std::byte{ 'D' }, std::byte{ 'O' }
	};
	inline constexpr std::uint16_t kCookedDocumentVersion = 1u;
	inline constexpr std::string_view kCookedDocumentTextEnvelopePrefix =
		"CEDO1:";

	[[nodiscard]] bool IsCookedDocument(
		std::span<const std::byte> bytes) noexcept;

	[[nodiscard]] bool EncodeCookedDocument(const ReadNode& root,
		std::vector<std::byte>& outBytes, std::string& error);

	[[nodiscard]] std::optional<WriteDocument> DecodeCookedDocument(
		std::span<const std::byte> bytes, std::string& error);

	// Cooked scene 안에 문자열 필드로 저장해야 하는 임베디드 tree용 envelope.
	// authoring YAML fragment를 runtime에서 다시 파싱하지 않도록 CEDO bytes를
	// strict RFC4648 base64로 싣는다.
	[[nodiscard]] bool EncodeCookedDocumentTextEnvelope(const ReadNode& root,
		std::string& outText, std::string& error);
	[[nodiscard]] std::optional<WriteDocument> DecodeCookedDocumentTextEnvelope(
		std::string_view text, std::string& error);
}
