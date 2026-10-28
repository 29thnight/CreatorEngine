module;
#include "../../Utility_Framework/Runtime/EngineVersion.h"

export module Creator.Core.Distribution;
export import "Creator.Containers.h";
import std;

export namespace ce::distribution
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

    inline ce::ordered_node_map<std::string, std::string, std::less<>> read_metadata_file(const std::filesystem::path& path)
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
        ce::ordered_node_map<std::string, std::string, std::less<>> values;
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
