#include "../../Containers/ContainerAPI.h"
#pragma once
#include "ModelVertexLayout.h"
#include "../../Utility_Framework/Foundation/StandardLibrary.h"

namespace assets::quantized
{
// LE packet: 8-word header, then six-word scalar descriptors and bit streams.
// All fixed-point channels must reconstruct the original IEEE bits exactly.
inline constexpr std::uint32_t Magic = 0x31514743u, Version = 1, HeaderBytes = 32, DescriptorBytes = 24;
inline constexpr std::size_t MaximumBytes = 256ull << 20;
inline void Require(bool b, const char* message)
{
    if (!b)
    {
        throw std::runtime_error(message);
    }
}
inline std::uint32_t Word(std::span<const std::byte> bytes, std::size_t offset)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "Truncated quantized word");
    std::uint32_t value{};
    if constexpr (std::endian::native == std::endian::little)
    {
        // memcpy permits unaligned packet words; retain the extent check above.
        std::memcpy(&value, bytes.data() + offset, sizeof(value));
    }
    else
    {
        for (unsigned i = 0; i < 4; ++i)
        {
            value |= std::uint32_t(std::to_integer<unsigned char>(bytes[offset + i])) << (i * 8);
        }
    }
    return value;
}
inline void Put(ce::dynamic_array<std::byte>& bytes, std::size_t offset, std::uint32_t value)
{
    Require(offset <= bytes.size() && bytes.size() - offset >= 4, "Quantized write range");
    if constexpr (std::endian::native == std::endian::little)
        std::memcpy(bytes.data() + offset, &value, sizeof(value));
    else
        for (unsigned i = 0; i < 4; ++i)
            bytes[offset + i] = std::byte((value >> (i * 8)) & 255u);
}
inline bool Encode(std::uint32_t mask, std::uint32_t count, std::span<const std::byte> raw, ce::dynamic_array<std::byte>& out,
                   std::string& error)
{
    out.clear();
    try
    {
        const auto stride = StrideOf(mask);
        Require(IsSupportedModelVertexLayout(mask) && count && raw.size() == std::uint64_t(count) * stride &&
                    raw.size() <= MaximumBytes - HeaderBytes,
                "Invalid quantized geometry layout/count");
        const auto channels = stride / 4;
        ce::dynamic_array<std::byte> packet(HeaderBytes + channels * DescriptorBytes);
        // Reuse channel scratch during canonical validation of every meshlet.
        ce::dynamic_array<std::uint32_t> values(count), codes;
        codes.reserve(count);
        for (unsigned channel = 0; channel < channels; ++channel)
        {
            bool integer =
                Has(mask, VertexAttribute::BoneIndices) && channel * 4 == OffsetOf(mask, VertexAttribute::BoneIndices);
            values.resize(count);
            codes.clear();
            bool constant = true;
            float minimum = std::numeric_limits<float>::max(), maximum = -minimum;
            int exponent = 128;
            for (unsigned vertex = 0; vertex < count; ++vertex)
            {
                const auto bits = values[vertex] = Word(raw, std::size_t(vertex) * stride + channel * 4);
                constant = constant && bits == values[0];
                if (!integer)
                {
                    const auto value = std::bit_cast<float>(bits);
                    Require(std::isfinite(value), "Nonfinite quantized geometry attribute");
                    minimum = (std::min)(minimum, value);
                    maximum = (std::max)(maximum, value);
                    const auto absolute = bits & 0x7fffffffu;
                    if (absolute)
                    {
                        const auto e = int(absolute >> 23);
                        const auto mantissa = (absolute & 0x7fffffu) | (e ? 0x800000u : 0u);
                        exponent = (std::min)(exponent, (e ? e - 150 : -149) + int(std::countr_zero(mantissa)));
                    }
                }
            }
            unsigned mode = constant ? 0 : 2, width = constant ? 0 : 32;
            float scale{};
            if (!constant && !integer && exponent >= -126 && exponent <= 127 &&
                (minimum == 0 || std::abs(minimum) >= std::numeric_limits<float>::min()))
            {
                scale = std::ldexp(1.f, exponent);
                const double range = (double(maximum) - minimum) / double(scale);
                if (range > 0 && range <= 0xffffffu)
                {
                    const auto largest = static_cast<std::uint32_t>(range);
                    width = largest <= 255 ? 8 : largest <= 1023 ? 10 : largest <= 65535 ? 16 : 24;
                    bool exact = true;
                    for (auto bits : values)
                    {
                        const double q = (double(std::bit_cast<float>(bits)) - minimum) / scale;
                        if (q < 0 || q > largest || q != std::floor(q))
                        {
                            exact = false;
                            break;
                        }
                        const auto code = static_cast<std::uint32_t>(q);
                        const float product = float(code) * scale;
                        const float decoded = minimum + product;
                        if (std::bit_cast<std::uint32_t>(decoded) != bits)
                        {
                            exact = false;
                            break;
                        }
                        codes.push_back(code);
                    }
                    if (exact)
                    {
                        mode = 1;
                    }
                }
            }
            if (mode == 2)
            {
                width = 32;
                codes.assign(values.begin(), values.end());
            }
            const auto descriptor = HeaderBytes + channel * DescriptorBytes;
            Put(packet, descriptor, mode);
            Put(packet, descriptor + 4, width);
            Put(packet, descriptor + 8, mode == 0 ? values[0] : mode == 1 ? std::bit_cast<std::uint32_t>(minimum) : 0);
            Put(packet, descriptor + 12, mode == 1 ? std::bit_cast<std::uint32_t>(scale) : 0);
            if (mode)
            {
                const auto offset = packet.size();
                const auto words = (std::uint64_t(count) * width + 31) / 32 + 1; // Safe cross-word load guard.
                Require(words * 4 <= MaximumBytes - packet.size(), "Quantized packet byte budget");
                packet.resize(offset + words * 4);
                Put(packet, descriptor + 16, static_cast<std::uint32_t>(offset));
                Put(packet, descriptor + 20, static_cast<std::uint32_t>(words));
                for (unsigned vertex = 0; vertex < count; ++vertex)
                {
                    const auto bit = std::uint64_t(vertex) * width;
                    const auto word = offset + (bit / 32) * 4;
                    const auto shift = unsigned(bit % 32);
                    // Full words have no neighbouring packed bits to preserve.
                    Put(packet, word, width == 32 ? codes[vertex] : Word(packet, word) | (codes[vertex] << shift));
                    if (shift + width > 32)
                    {
                        Put(packet, word + 4, Word(packet, word + 4) | (codes[vertex] >> (32 - shift)));
                    }
                }
            }
        }
        bool rawPacket = packet.size() >= raw.size() + HeaderBytes;
        if (rawPacket)
        {
            packet.assign(HeaderBytes, std::byte{});
            packet.insert(packet.end(), raw.begin(), raw.end());
        }
        Put(packet, 0, Magic);
        Put(packet, 4, Version);
        Put(packet, 8, rawPacket ? 1 : 0);
        Put(packet, 12, mask);
        Put(packet, 16, count);
        Put(packet, 20, stride);
        Put(packet, 24, channels);
        Put(packet, 28, static_cast<std::uint32_t>(packet.size()));
        out = std::move(packet);
        error.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
inline bool Decode(std::span<const std::byte> packet, std::uint32_t mask, std::uint32_t count,
                   ce::dynamic_array<std::byte>& out, std::string& error)
{
    out.clear();
    try
    {
        const auto stride = StrideOf(mask);
        Require(IsSupportedModelVertexLayout(mask) && count &&
                    std::uint64_t(count) * stride <= MaximumBytes - HeaderBytes && packet.size() <= MaximumBytes &&
                    Word(packet, 0) == Magic && Word(packet, 4) == Version && Word(packet, 12) == mask &&
                    Word(packet, 16) == count && Word(packet, 20) == stride && Word(packet, 24) == stride / 4 &&
                    Word(packet, 28) == packet.size(),
                "Quantized header/layout mismatch");
        const auto rawMode = Word(packet, 8);
        Require(rawMode <= 1, "Unknown quantized packet mode");
        ce::dynamic_array<std::byte> raw(std::size_t(count) * stride);
        if (rawMode)
        {
            Require(packet.size() == raw.size() + HeaderBytes, "Quantized raw packet size");
            std::copy(packet.begin() + HeaderBytes, packet.end(), raw.begin());
        }
        else
        {
            const auto headerEnd = HeaderBytes + stride / 4 * DescriptorBytes;
            Require(packet.size() >= headerEnd, "Truncated quantized descriptors");
            for (unsigned channel = 0; channel < stride / 4; ++channel)
            {
                const auto d = HeaderBytes + channel * DescriptorBytes;
                const auto mode = Word(packet, d), width = Word(packet, d + 4), origin = Word(packet, d + 8);
                const auto offset = Word(packet, d + 16), words = Word(packet, d + 20);
                Require(mode <= 2 && ((mode == 0 && width == 0 && offset == 0 && words == 0) ||
                                      (mode == 1 && (width == 8 || width == 10 || width == 16 || width == 24)) ||
                                      (mode == 2 && width == 32)),
                        "Invalid quantized channel mode/width");
                if (mode)
                {
                    Require(offset >= headerEnd && offset % 4 == 0 &&
                                words == (std::uint64_t(count) * width + 31) / 32 + 1 &&
                                std::uint64_t(offset) + std::uint64_t(words) * 4 <= packet.size(),
                            "Invalid quantized stream range");
                }
                const float scale = mode == 1 ? std::bit_cast<float>(Word(packet, d + 12)) : 0.f;
                for (unsigned vertex = 0; vertex < count; ++vertex)
                {
                    auto bits = origin;
                    if (mode)
                    {
                        const auto bit = std::uint64_t(vertex) * width;
                        const auto shift = unsigned(bit % 32);
                        const auto address = offset + (bit / 32) * 4;
                        auto q = Word(packet, address) >> shift;
                        if (shift + width > 32)
                        {
                            q |= Word(packet, address + 4) << (32 - shift);
                        }
                        if (width < 32)
                        {
                            q &= (1u << width) - 1;
                        }
                        bits = q;
                        if (mode == 1)
                        {
                            const float product = float(q) * scale;
                            bits = std::bit_cast<std::uint32_t>(std::bit_cast<float>(origin) + product);
                        }
                    }
                    Put(raw, std::size_t(vertex) * stride + channel * 4, bits);
                }
            }
        }
        ce::dynamic_array<std::byte> canonical;
        Require(Encode(mask, count, raw, canonical, error) && canonical.size() == packet.size() &&
                    std::equal(canonical.begin(), canonical.end(), packet.begin()),
                "Noncanonical or corrupt quantized payload");
        out = std::move(raw);
        error.clear();
        return true;
    }
    catch (const std::exception& e)
    {
        error = e.what();
        return false;
    }
}
} // namespace assets::quantized
