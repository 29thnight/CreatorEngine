#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Distribution.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif

#include "../../../Engine/Utility_Framework/Runtime/EngineVersion.h"


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace ce::distribution
{
    // Generated version input remains owned by update-engine-version.ps1.
    inline constexpr auto& product_name = CreatorEngineVersion::ProductName;
    inline constexpr auto& feature_release = CreatorEngineVersion::FeatureRelease;
    inline constexpr auto& build = CreatorEngineVersion::Build;
    inline constexpr bool local_development = CreatorEngineVersion::LocalDevelopment;

    struct identity
    {
        std::string channel{"preview"};
        std::string build_id{"unpublished"};
        std::string payload_digest;
    };

    const identity& current_identity();

    inline std::map<std::string, std::string> read_metadata_file(const std::filesystem::path& path)
    {
        if (std::filesystem::file_size(path) > 4096)
        {
            throw std::runtime_error("Engine metadata exceeds limit");
        }
        std::ifstream stream(path);
        if (!stream)
        {
            throw std::runtime_error("Engine metadata is missing");
        }
        std::map<std::string, std::string> values;
        std::string line;
        while (std::getline(stream, line))
        {
            const auto separator = line.find('=');
            if (separator == std::string::npos || separator == 0 || line.find('\r') != std::string::npos ||
                !values.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
            {
                throw std::runtime_error("Invalid engine metadata");
            }
        }
        return values;
    }
} // namespace ce::distribution
