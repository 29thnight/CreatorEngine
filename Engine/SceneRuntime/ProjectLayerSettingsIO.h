#pragma once

#include "ProjectLayerSettingsCodec.h"
#include <filesystem>
#include <fstream>

class ProjectLayerSettingsIO final
{
  public:
    static constexpr const char* filename = "Layers.celayers";

    static ce::layers::result<project_layer_snapshot> Read(const std::filesystem::path& path)
    {
        try
        {
            std::ifstream input(path, std::ios::binary | std::ios::ate);
            if (!input)
                return std::unexpected(ce::layers::error::io_failure);

            const auto size = input.tellg();
            if (size <= 0 || size > static_cast<std::streamoff>(ce::layers::ProjectLayerSettingsCodec::max_bytes))
                return std::unexpected(ce::layers::error::corrupt_data);

            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
                return std::unexpected(ce::layers::error::io_failure);

            return ce::layers::ProjectLayerSettingsCodec::Decode(bytes);
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(ce::layers::error::out_of_memory);
        }
        catch (const std::filesystem::filesystem_error&)
        {
            return std::unexpected(ce::layers::error::io_failure);
        }
    }

    // The Editor callback owns staging/atomic publication. Player installs no writer.
    template<class Writer>
    static ce::layers::result<void> Write(const project_layer_snapshot& snapshot, Writer&& writer)
    {
        const auto encoded = ce::layers::ProjectLayerSettingsCodec::Encode(snapshot);
        if (!encoded)
            return std::unexpected(encoded.error());

        if (!std::invoke(std::forward<Writer>(writer), std::span<const std::byte>(*encoded)))
            return std::unexpected(ce::layers::error::io_failure);

        return {};
    }
};
