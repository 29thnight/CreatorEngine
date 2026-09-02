#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// D3-b-4 — 저작 payload의 base64는 YAML parser의 책임이 아니다.
//
// Material constant buffer가 디스크에서 쓰는 표준 RFC 4648 alphabet과 `=` padding만
// 구현한다. Decode는 손상된 입력을 부분 성공으로 받아들이지 않고 false를 돌려준다.
// 이 경계를 별도로 두면 저작 문서 backend를 제거해도 binary payload 계약은 그대로다.
namespace Authoring::Base64
{
	[[nodiscard]] inline std::string Encode(
		const std::uint8_t* data, std::size_t size)
	{
		static constexpr char kAlphabet[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		if (0 == size) return {};
		if (nullptr == data) return {};

		std::string encoded;
		encoded.reserve(((size + 2u) / 3u) * 4u);
		std::size_t offset = 0;
		while (offset + 3u <= size)
		{
			const std::uint32_t block =
				(static_cast<std::uint32_t>(data[offset]) << 16u)
				| (static_cast<std::uint32_t>(data[offset + 1u]) << 8u)
				| static_cast<std::uint32_t>(data[offset + 2u]);
			encoded.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
			encoded.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
			encoded.push_back(kAlphabet[(block >> 6u) & 0x3fu]);
			encoded.push_back(kAlphabet[block & 0x3fu]);
			offset += 3u;
		}

		const std::size_t remaining = size - offset;
		if (1u == remaining)
		{
			const std::uint32_t block =
				static_cast<std::uint32_t>(data[offset]) << 16u;
			encoded.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
			encoded.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
			encoded.append("==");
		}
		else if (2u == remaining)
		{
			const std::uint32_t block =
				(static_cast<std::uint32_t>(data[offset]) << 16u)
				| (static_cast<std::uint32_t>(data[offset + 1u]) << 8u);
			encoded.push_back(kAlphabet[(block >> 18u) & 0x3fu]);
			encoded.push_back(kAlphabet[(block >> 12u) & 0x3fu]);
			encoded.push_back(kAlphabet[(block >> 6u) & 0x3fu]);
			encoded.push_back('=');
		}
		return encoded;
	}

	[[nodiscard]] inline bool Decode(std::string_view encoded,
		std::vector<std::uint8_t>& decoded)
	{
		decoded.clear();
		if (encoded.empty()) return true;
		if (0u != (encoded.size() % 4u)) return false;

		const auto valueOf = [](char value) noexcept -> int
		{
			if (value >= 'A' && value <= 'Z') return value - 'A';
			if (value >= 'a' && value <= 'z') return value - 'a' + 26;
			if (value >= '0' && value <= '9') return value - '0' + 52;
			if ('+' == value) return 62;
			if ('/' == value) return 63;
			return -1;
		};

		decoded.reserve((encoded.size() / 4u) * 3u);
		for (std::size_t offset = 0; offset < encoded.size(); offset += 4u)
		{
			const bool finalBlock = (offset + 4u == encoded.size());
			const int a = valueOf(encoded[offset]);
			const int b = valueOf(encoded[offset + 1u]);
			if (a < 0 || b < 0)
			{
				decoded.clear();
				return false;
			}

			const char third = encoded[offset + 2u];
			const char fourth = encoded[offset + 3u];
			if ('=' == third)
			{
				// One source byte: the low four bits of the second sextet are padding.
				if (!finalBlock || '=' != fourth || 0 != (b & 0x0f))
				{
					decoded.clear();
					return false;
				}
				decoded.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
				continue;
			}

			const int c = valueOf(third);
			if (c < 0)
			{
				decoded.clear();
				return false;
			}
			decoded.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
			decoded.push_back(static_cast<std::uint8_t>((b << 4) | (c >> 2)));

			if ('=' == fourth)
			{
				// Two source bytes: the low two bits of the third sextet are padding.
				if (!finalBlock || 0 != (c & 0x03))
				{
					decoded.clear();
					return false;
				}
				continue;
			}

			const int d = valueOf(fourth);
			if (d < 0)
			{
				decoded.clear();
				return false;
			}
			decoded.push_back(static_cast<std::uint8_t>((c << 6) | d));
		}
		return true;
	}
}
