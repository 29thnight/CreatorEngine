#pragma once

#include "ProjectLayerSettings.h"
#include <bit>
#include <vector>

namespace ce::layers
{
// Canonical CLYR v1, little endian. No SDK layouts, pointers, or runtime revision are serialized.
class ProjectLayerSettingsCodec final
{
  public:
    static constexpr std::size_t max_bytes = 64 * 1024;
    static constexpr std::size_t max_name_bytes = max_layer_name_bytes;

    static result<std::vector<std::byte>> Encode(const project_layer_snapshot& source)
    {
        if (!LayerCatalog::Validate(source.catalog) || !ce::physics::PhysicsCollisionPolicy::Validate(source.policy))
            return std::unexpected(error::invalid_definition);

        try
        {
            std::vector<std::byte> output;
            output.reserve(2048);
            Put(output, 0x52594c43u, 4); // CLYR
            Put(output, 1, 4);
            Put(output, source.catalog.next_id, 8);
            const auto count =
                std::ranges::count_if(source.catalog.definitions, [](const auto& item) { return bool(item); });
            Put(output, static_cast<std::uint64_t>(count), 4);
            for (const auto& item : source.catalog.definitions)
            {
                if (!item)
                    continue;

                if (item->name.size() > max_name_bytes || !ValidLayerName(item->name))
                    return std::unexpected(error::invalid_definition);

                Put(output, item->id.value, 8);
                Put(output, item->slot.Value(), 1);
                Put(output, item->retired, 1);
                Put(output, item->name.size(), 2);
                for (const unsigned char byte : item->name)
                    output.push_back(std::byte{byte});
            }

            for (const auto value : source.policy.matrix)
                output.push_back(std::byte{value});

            const auto checksum = Checksum(output);
            Put(output, checksum, 8);
            return output;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error::out_of_memory);
        }
    }

    static result<project_layer_snapshot> Decode(std::span<const std::byte> input)
    {
        if (input.size() < 20 + 12 + 7 + 1024 + 8 || input.size() > max_bytes)
            return std::unexpected(error::corrupt_data);

        std::size_t checksum_cursor = input.size() - 8;
        const auto stored_checksum = Get(input, checksum_cursor, 8);
        if (!stored_checksum || *stored_checksum != Checksum(input.first(input.size() - 8)))
            return std::unexpected(error::corrupt_data);

        input = input.first(input.size() - 8);
        std::size_t cursor = 0;
        const auto magic = Get(input, cursor, 4);
        const auto version = Get(input, cursor, 4);
        const auto next = Get(input, cursor, 8);
        const auto count = Get(input, cursor, 4);
        if (!magic || *magic != 0x52594c43u || !version || *version != 1 || !next || !count || !*count || *count > 32)
            return std::unexpected(error::corrupt_data);

        try
        {
            project_layer_snapshot output;
            output.catalog.next_id = *next;
            for (std::uint64_t i = 0; i < *count; ++i)
            {
                const auto id = Get(input, cursor, 8);
                const auto slot = Get(input, cursor, 1);
                const auto retired = Get(input, cursor, 1);
                const auto length = Get(input, cursor, 2);
                if (!id || !slot || *slot >= 32 || !retired || *retired > 1 || !length || !*length ||
                    *length > max_name_bytes || *length > input.size() - cursor || output.catalog.definitions[*slot])
                    return std::unexpected(error::corrupt_data);

                std::string name(reinterpret_cast<const char*>(input.data() + cursor),
                                 static_cast<std::size_t>(*length));
                cursor += static_cast<std::size_t>(*length);
                if (!ValidLayerName(name))
                    return std::unexpected(error::corrupt_data);

                output.catalog.definitions[*slot] =
                    layer_definition{layer_id{*id}, layer_slot::Make(static_cast<std::uint32_t>(*slot)).value(),
                                     std::move(name), bool(*retired)};
            }

            if (input.size() - cursor != output.policy.matrix.size())
                return std::unexpected(error::corrupt_data);

            for (auto& value : output.policy.matrix)
                value = std::to_integer<std::uint8_t>(input[cursor++]);

            if (!LayerCatalog::Validate(output.catalog) ||
                !ce::physics::PhysicsCollisionPolicy::Validate(output.policy))
                return std::unexpected(error::invalid_definition);

            return output;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error::out_of_memory);
        }
    }

  private:
    static void Put(std::vector<std::byte>& output, std::uint64_t value, std::size_t bytes)
    {
        for (std::size_t i = 0; i < bytes; ++i)
            output.push_back(std::byte{static_cast<std::uint8_t>(value >> (i * 8))});
    }

    static result<std::uint64_t> Get(std::span<const std::byte> input, std::size_t& cursor, std::size_t bytes) noexcept
    {
        if (cursor > input.size() || bytes > input.size() - cursor)
            return std::unexpected(error::corrupt_data);

        std::uint64_t value = 0;
        for (std::size_t i = 0; i < bytes; ++i)
            value |= std::uint64_t{std::to_integer<std::uint8_t>(input[cursor++])} << (i * 8);

        return value;
    }

    static std::uint64_t Checksum(std::span<const std::byte> input) noexcept
    {
        std::uint64_t value = 14695981039346656037ull;
        for (const auto byte : input)
            value = (value ^ std::to_integer<std::uint8_t>(byte)) * 1099511628211ull;

        return value;
    }
};
} // namespace ce::layers
