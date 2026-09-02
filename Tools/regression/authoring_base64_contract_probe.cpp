#include "AuthoringBase64.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
	bool CheckVector(std::string_view plain, std::string_view encoded)
	{
		const auto* bytes = reinterpret_cast<const std::uint8_t*>(plain.data());
		if (Authoring::Base64::Encode(bytes, plain.size()) != encoded) return false;

		std::vector<std::uint8_t> decoded;
		if (!Authoring::Base64::Decode(encoded, decoded)) return false;
		return decoded == std::vector<std::uint8_t>(bytes, bytes + plain.size());
	}
}

int main()
{
	if (!CheckVector("", "")
		|| !CheckVector("f", "Zg==")
		|| !CheckVector("fo", "Zm8=")
		|| !CheckVector("foo", "Zm9v")
		|| !CheckVector("foobar", "Zm9vYmFy"))
	{
		std::puts("base64 known vector failure");
		return 1;
	}

	const std::array<std::uint8_t, 6> binary{ 0u, 1u, 2u, 253u, 254u, 255u };
	const std::string binaryEncoded =
		Authoring::Base64::Encode(binary.data(), binary.size());
	if (binaryEncoded != "AAEC/f7/")
	{
		std::puts("base64 binary vector failure");
		return 2;
	}

	std::vector<std::uint8_t> allBytes(256u);
	for (std::size_t index = 0; index < allBytes.size(); ++index)
		allBytes[index] = static_cast<std::uint8_t>(index);
	std::vector<std::uint8_t> roundTrip;
	if (!Authoring::Base64::Decode(
			Authoring::Base64::Encode(allBytes.data(), allBytes.size()), roundTrip)
		|| roundTrip != allBytes)
	{
		std::puts("base64 0..255 round-trip failure");
		return 3;
	}

	for (const std::string_view invalid : {
		std::string_view{ "A" }, std::string_view{ "!!!!" },
		std::string_view{ "=m9v" }, std::string_view{ "Zm=v" },
		std::string_view{ "Zg=A" }, std::string_view{ "Zh==" } })
	{
		std::vector<std::uint8_t> rejected{ 1u, 2u, 3u };
		if (Authoring::Base64::Decode(invalid, rejected) || !rejected.empty())
		{
			std::puts("base64 malformed input acceptance failure");
			return 4;
		}
	}

	std::puts("authoring base64 contract: PASS");
	return 0;
}
