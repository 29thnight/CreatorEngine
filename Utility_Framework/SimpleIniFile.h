#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <optional>
#include <filesystem>
#include <algorithm>
#include <functional>

struct string_hash
{
    using hash_type = std::hash<std::string_view>;
    using is_transparent = void;

    std::size_t operator()(const char* str) const { return hash_type{}(str); }
    std::size_t operator()(std::string_view str) const { return hash_type{}(str); }
    std::size_t operator()(const std::string& str) const { return hash_type{}(str); }
};

class SimpleIniFile
{
public:
    using Section = std::unordered_map<std::string, std::string, string_hash, std::equal_to<>>;
    using SectionMap = std::unordered_map<std::string, Section, string_hash, std::equal_to<>>;

    SimpleIniFile() = default;
    explicit SimpleIniFile(const std::filesystem::path& filepath) { Load(filepath); }

    void Load(const std::filesystem::path& filepath)
    {
        std::ifstream file(filepath);
        if (!file)
            throw std::runtime_error("Failed to open INI file: " + filepath.string());

        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            Trim(line);
            if (line.empty() || line[0] == ';')
                continue;

            if (line.front() == '[' && line.back() == ']')
            {
                currentSection = TrimCopy(line.substr(1, line.length() - 2));
            }
            else if (auto pos = line.find('='); pos != std::string::npos)
            {
                std::string key = TrimCopy(line.substr(0, pos));
                std::string value = TrimCopy(line.substr(pos + 1));
                GetOrCreateSection(currentSection)[std::move(key)] = std::move(value);
            }
        }
    }

    void Save(const std::filesystem::path& filepath) const
    {
        std::ofstream file(filepath);
        if (!file)
            throw std::runtime_error("Failed to save INI file: " + filepath.string());

        for (const auto& [sectionName, keyValues] : sections_)
        {
            if (!sectionName.empty())
                file << '[' << sectionName << "]\n";

            for (const auto& [key, value] : keyValues)
                file << key << '=' << value << '\n';

            file << '\n';
        }
    }

    std::optional<std::string> TryGetValue(std::string_view section, std::string_view key) const
    {
        if (auto secIt = sections_.find(section); secIt != sections_.end())
        {
            if (auto keyIt = secIt->second.find(key); keyIt != secIt->second.end())
                return keyIt->second;
        }
        return std::nullopt;
    }

    std::string GetValue(std::string_view section, std::string_view key, const std::string& defaultValue = "") const
    {
        if (auto val = TryGetValue(section, key))
            return *val;
        return defaultValue;
    }

    bool HasSection(std::string_view section) const
    {
        return sections_.contains(section);
    }

    const Section* TryGetSection(std::string_view section) const
    {
        if (auto it = sections_.find(section); it != sections_.end())
            return &it->second;
        return nullptr;
    }

    Section& GetOrCreateSection(std::string_view section)
    {
        if (auto it = sections_.find(section); it != sections_.end())
            return it->second;

        return sections_.try_emplace(std::string(section), Section{}).first->second;
    }

    Section& operator[](std::string_view section)
    {
        return GetOrCreateSection(section);
    }

    const SectionMap& GetAll() const noexcept { return sections_; }

private:
    SectionMap sections_;

    static std::string TrimCopy(std::string_view str)
    {
        const auto begin = str.find_first_not_of(" \t\n\r\f\v");
        const auto end = str.find_last_not_of(" \t\n\r\f\v");

        return (begin == std::string_view::npos) ? "" : std::string(str.substr(begin, end - begin + 1));
    }

    static void Trim(std::string& str)
    {
        str = TrimCopy(str);
    }
};
