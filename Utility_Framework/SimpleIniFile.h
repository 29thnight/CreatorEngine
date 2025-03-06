#pragma once
#include <string>
#include <map>
#include <fstream>
#include <sstream>
#include <stdexcept>

class SimpleIniFile
{
public:
    SimpleIniFile() = default;
    explicit SimpleIniFile(const std::string& filename) { Load(filename); }

    void Load(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open())
        {
            throw std::runtime_error("Could not open file: " + filename);
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line))
        {
            // Remove leading and trailing whitespace
            Trim(line);
            if (line.empty() || line[0] == ';') // Skip empty lines and comments
                continue;

            if (line[0] == '[' && line.back() == ']') // Section header
            {
                currentSection = line.substr(1, line.length() - 2);
            }
            else // Key-value pair
            {
                auto delimiterPos = line.find('=');
                if (delimiterPos != std::string::npos)
                {
                    std::string key = TrimCopy(line.substr(0, delimiterPos));
                    std::string value = TrimCopy(line.substr(delimiterPos + 1));
                    if (!currentSection.empty())
                    {
                        sections_[currentSection][key] = value;
                    }
                }
            }
        }
    }

    std::string GetValue(const std::string& section, const std::string& key, const std::string& defaultValue = "") const
    {
        auto sectionIt = sections_.find(section);
        if (sectionIt != sections_.end())
        {
            auto keyIt = sectionIt->second.find(key);
            if (keyIt != sectionIt->second.end())
            {
                return keyIt->second;
            }
        }
        return defaultValue;
    }

private:
    std::map<std::string, std::map<std::string, std::string>> sections_;

    static std::string TrimCopy(const std::string& str)
    {
        std::string trimmed = str;
        trimmed.erase(trimmed.find_last_not_of(" \t\n\r\f\v") + 1);
        trimmed.erase(0, trimmed.find_first_not_of(" \t\n\r\f\v"));
        return trimmed;
    }

    static void Trim(std::string& str)
    {
        str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
        str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
    }
};
