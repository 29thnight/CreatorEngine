#pragma once
#include "NetWireIds.h"
#include <cstddef>
#include <span>

namespace ce::net
{
    inline constexpr std::uint32_t protocol_magic = 0x544e4543; // bytes C E N T
    inline constexpr std::uint16_t protocol_version = 1;
    inline constexpr std::uint32_t protocol_header_bytes = 36;
    // Host value; never memcpy/reinterpret this struct to/from the network.
    // Layout on wire is the fields below in order, little endian, no padding.
    struct NetProtocolHeader final
    {
        std::uint32_t magic{protocol_magic};
        std::uint16_t version{protocol_version};
        NetMessageTypeId message{};
        std::uint64_t compatibility{};
        NetTick server_tick{};
        NetTick baseline_tick{};
        std::uint32_t payload_bits{};
    };
    enum class NetDecodeResult : std::uint8_t
    {
        Ok, Truncated, BadMagic, VersionMismatch, SchemaMismatch, InvalidMessage,
        InvalidBaseline, PayloadTooLarge, SizeMismatch, NonzeroPadding
    };
    // Cursors change only on successful scalar operations. No allocation and no
    // unaligned loads. Count/length policy is supplied by the receiving adapter.
    class NetWriter
    {
    public:
        explicit NetWriter(std::span<std::byte> output) noexcept : output_(output) {}
        bool Write(std::uint64_t value, std::uint32_t bytes) noexcept
        {
            if (!bytes || bytes > 8 || bytes > output_.size() - offset_) return false;
            if (bytes < 8 && (value >> (bytes << 3))) return false;
            for (std::uint32_t i = 0; i < bytes; ++i) output_[offset_ + i] = std::byte((value >> (i << 3)) & 0xffu);
            offset_ += bytes; return true;
        }
        std::uint64_t Written() const noexcept { return offset_; }
    private:
        std::span<std::byte> output_;
        std::uint64_t offset_{};
    };
    class NetReader
    {
    public:
        explicit NetReader(std::span<const std::byte> input) noexcept : input_(input) {}
        bool Read(std::uint64_t& value, std::uint32_t bytes) noexcept
        {
            if (!bytes || bytes > 8 || bytes > input_.size() - offset_) return false;
            std::uint64_t decoded = 0;
            for (std::uint32_t i = 0; i < bytes; ++i) decoded |= std::uint64_t(std::to_integer<std::uint8_t>(input_[offset_ + i])) << (i << 3);
            value = decoded; offset_ += bytes; return true;
        }
        std::uint64_t ReadBytes() const noexcept { return offset_; }
    private:
        std::span<const std::byte> input_;
        std::uint64_t offset_{};
    };
    inline bool EncodeHeader(const NetProtocolHeader& header, std::span<std::byte> output) noexcept
    {
        if (output.size() < protocol_header_bytes) return false;
        NetWriter writer(output.first(protocol_header_bytes));
        return writer.Write(header.magic, 4) && writer.Write(header.version, 2) && writer.Write(header.message.value, 2)
            && writer.Write(header.compatibility, 8) && writer.Write(header.server_tick.value, 8)
            && writer.Write(header.baseline_tick.value, 8) && writer.Write(header.payload_bits, 4);
    }
    // Exactly one complete application message; no trailing bytes. Header and
    // payload outputs stay empty on all failures. Unknown nonzero IDs still need
    // an N2/N4 message allowlist before payload dispatch.
    inline NetDecodeResult DecodeMessage(std::span<const std::byte> input, std::uint64_t compatibility,
        std::uint32_t max_payload_bytes, NetProtocolHeader& header, std::span<const std::byte>& payload) noexcept
    {
        header = {}; payload = {};
        if (input.size() < protocol_header_bytes) return NetDecodeResult::Truncated;
        NetReader reader(input.first(protocol_header_bytes));
        NetProtocolHeader decoded;
        std::uint64_t value;
        reader.Read(value, 4); decoded.magic = static_cast<std::uint32_t>(value);
        reader.Read(value, 2); decoded.version = static_cast<std::uint16_t>(value);
        reader.Read(value, 2); decoded.message.value = static_cast<std::uint16_t>(value);
        reader.Read(decoded.compatibility, 8);
        reader.Read(decoded.server_tick.value, 8);
        reader.Read(decoded.baseline_tick.value, 8);
        reader.Read(value, 4); decoded.payload_bits = static_cast<std::uint32_t>(value);
        if (decoded.magic != protocol_magic) return NetDecodeResult::BadMagic;
        if (decoded.version != protocol_version) return NetDecodeResult::VersionMismatch;
        if (!compatibility || decoded.compatibility != compatibility) return NetDecodeResult::SchemaMismatch;
        if (!decoded.message.value) return NetDecodeResult::InvalidMessage;
        if (decoded.baseline_tick.value > decoded.server_tick.value) return NetDecodeResult::InvalidBaseline;
        const std::uint64_t bytes = (std::uint64_t(decoded.payload_bits) + 7u) / 8u;
        if (bytes > max_payload_bytes) return NetDecodeResult::PayloadTooLarge;
        if (input.size() - protocol_header_bytes != bytes) return NetDecodeResult::SizeMismatch;
        if (bytes && (decoded.payload_bits & 7u)
            && (std::to_integer<std::uint8_t>(input.back()) >> (decoded.payload_bits & 7u))) return NetDecodeResult::NonzeroPadding;
        header = decoded; payload = input.subspan(protocol_header_bytes); return NetDecodeResult::Ok;
    }
}
