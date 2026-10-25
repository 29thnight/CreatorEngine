#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Base64.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

// Standard alphabet, canonical padding and unused-bit validation.
namespace ce::base64
{
    [[nodiscard]] inline std::string encode(
        const std::uint8_t* data, std::size_t size)
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        if (0 == size)
        {
            return {};
        }
        if (nullptr == data)
        {
            return {};
        }

        std::string encoded;
        encoded.reserve(((size + 2u) / 3u) * 4u);
        std::size_t offset = 0;
        while (offset + 3u <= size)
        {
            const std::uint32_t block =
                (static_cast<std::uint32_t>(data[offset]) << 16u)
                | (static_cast<std::uint32_t>(data[offset + 1u]) << 8u)
                | static_cast<std::uint32_t>(data[offset + 2u]);
            encoded.push_back(alphabet[(block >> 18u) & 0x3fu]);
            encoded.push_back(alphabet[(block >> 12u) & 0x3fu]);
            encoded.push_back(alphabet[(block >> 6u) & 0x3fu]);
            encoded.push_back(alphabet[block & 0x3fu]);
            offset += 3u;
        }

        const std::size_t remaining = size - offset;
        if (1u == remaining)
        {
            const std::uint32_t block =
                static_cast<std::uint32_t>(data[offset]) << 16u;
            encoded.push_back(alphabet[(block >> 18u) & 0x3fu]);
            encoded.push_back(alphabet[(block >> 12u) & 0x3fu]);
            encoded.append("==");
        }
        else if (2u == remaining)
        {
            const std::uint32_t block =
                (static_cast<std::uint32_t>(data[offset]) << 16u)
                | (static_cast<std::uint32_t>(data[offset + 1u]) << 8u);
            encoded.push_back(alphabet[(block >> 18u) & 0x3fu]);
            encoded.push_back(alphabet[(block >> 12u) & 0x3fu]);
            encoded.push_back(alphabet[(block >> 6u) & 0x3fu]);
            encoded.push_back('=');
        }
        return encoded;
    }

    [[nodiscard]] inline bool decode(std::string_view encoded,
        std::vector<std::uint8_t>& decoded)
    {
        decoded.clear();
        if (encoded.empty())
        {
            return true;
        }
        if (0u != (encoded.size() % 4u))
        {
            return false;
        }

        const auto value_of = [](char value) noexcept -> int
        {
            if (value >= 'A' && value <= 'Z')
            {
                return value - 'A';
            }
            if (value >= 'a' && value <= 'z')
            {
                return value - 'a' + 26;
            }
            if (value >= '0' && value <= '9')
            {
                return value - '0' + 52;
            }
            if ('+' == value)
            {
                return 62;
            }
            if ('/' == value)
            {
                return 63;
            }
            return -1;
        };

        decoded.reserve((encoded.size() / 4u) * 3u);
        for (std::size_t offset = 0; offset < encoded.size(); offset += 4u)
        {
            const bool final_block = (offset + 4u == encoded.size());
            const int a = value_of(encoded[offset]);
            const int b = value_of(encoded[offset + 1u]);
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
                if (!final_block || '=' != fourth || 0 != (b & 0x0f))
                {
                    decoded.clear();
                    return false;
                }
                decoded.push_back(static_cast<std::uint8_t>((a << 2) | (b >> 4)));
                continue;
            }

            const int c = value_of(third);
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
                if (!final_block || 0 != (c & 0x03))
                {
                    decoded.clear();
                    return false;
                }
                continue;
            }

            const int d = value_of(fourth);
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
