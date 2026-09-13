#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>

// A bounded native projection of the JSON distribution manifest. The publisher
// emits it and the distribution verifier checks exact equivalence. Keeping this
// primitive string map avoids linking an authoring/content parser into each host.
inline std::map<std::string, std::string> ReadEngineMetadataFile(const std::filesystem::path& path)
{
    if (std::filesystem::file_size(path) > 4096) throw std::runtime_error("Engine metadata exceeds limit");
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error("Engine metadata is missing");
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(stream, line))
    {
        const auto separator = line.find('=');
        if (separator == std::string::npos || separator == 0 || line.find('\r') != std::string::npos ||
            !values.emplace(line.substr(0, separator), line.substr(separator + 1)).second)
            throw std::runtime_error("Invalid engine metadata");
    }
    return values;
}
