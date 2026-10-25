#pragma once
// Generated from Engine/Contracts/Core/Creator.Core.Strings.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace ce::strings
{
    inline char to_lower_char(unsigned char value)
    {
        return static_cast<char>(std::tolower(value));
    }

    inline char ascii_to_lower_char(unsigned char value) noexcept
    {
        return static_cast<char>(value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value);
    }

    inline bool is_space(unsigned char value)
    {
        return std::isspace(value) != 0;
    }

    inline void to_lower_in_place(std::string& value)
    {
        std::transform(value.begin(), value.end(), value.begin(), to_lower_char);
    }

    inline std::string to_lower(std::string_view value)
    {
        std::string result(value);
        to_lower_in_place(result);
        return result;
    }

    inline std::string ascii_to_lower(std::string_view value)
    {
        std::string result(value);
        std::transform(result.begin(), result.end(), result.begin(), ascii_to_lower_char);
        return result;
    }

    inline std::string_view trim_view(std::string_view text)
    {
        while (!text.empty() && is_space(static_cast<unsigned char>(text.front())))
        {
            text.remove_prefix(1);
        }
        while (!text.empty() && is_space(static_cast<unsigned char>(text.back())))
        {
            text.remove_suffix(1);
        }
        return text;
    }

    inline std::string_view trim_view(std::string_view text, std::string_view characters) noexcept
    {
        const auto first = text.find_first_not_of(characters);
        if (first == std::string_view::npos)
        {
            return text.substr(text.size());
        }
        const auto last = text.find_last_not_of(characters);
        return text.substr(first, last - first + 1);
    }

    inline std::string trim(std::string value)
    {
        const auto view = trim_view(value);
        const auto offset = static_cast<std::size_t>(view.data() - value.data());
        value.erase(offset + view.size());
        value.erase(0, offset);
        return value;
    }

    inline std::string remove_whitespace(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const unsigned char value : text)
        {
            if (!is_space(value))
            {
                result.push_back(static_cast<char>(value));
            }
        }
        return result;
    }

    inline bool contains_case_insensitive(std::string_view text, std::string_view needle)
    {
        return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                           [](unsigned char left, unsigned char right)
                           { return to_lower_char(left) == to_lower_char(right); }) != text.end() ||
               needle.empty();
    }

    inline std::string trim(std::string value, std::string_view characters)
    {
        const auto view = trim_view(value, characters);
        const auto offset = static_cast<std::size_t>(view.data() - value.data());
        value.erase(offset + view.size());
        value.erase(0, offset);
        return value;
    }

    inline std::string to_lower_without_whitespace(std::string_view text)
    {
        std::string result;
        result.reserve(text.size());
        for (const unsigned char value : text)
        {
            if (!is_space(value))
            {
                result.push_back(to_lower_char(value));
            }
        }
        return result;
    }

    inline bool equals_ascii_case_insensitive(std::string_view left, std::string_view right) noexcept
    {
        return left.size() == right.size() &&
               std::equal(left.begin(), left.end(), right.begin(), [](unsigned char a, unsigned char b)
                          { return ascii_to_lower_char(a) == ascii_to_lower_char(b); });
    }

    inline bool contains_case_insensitive(std::string_view text, const char* needle)
    {
        return !needle || contains_case_insensitive(text, std::string_view(needle));
    }

    inline std::vector<std::string> split(const std::string& s, char delim)
    {
        std::vector<std::string> out;
        std::string cur;
        std::istringstream iss(s);
        while (std::getline(iss, cur, delim))
        {
            out.push_back(cur);
        }
        return out;
    }

    inline std::vector<std::string> tokenize_preserve_quotes(const std::string& s)
    {
        std::vector<std::string> toks;
        std::string cur;
        bool inq = false;
        char qc = '\0';
        for (size_t i = 0; i < s.size(); ++i)
        {
            char c = s[i];
            if ((c == '\'' || c == '\"'))
            {
                if (!inq)
                {
                    inq = true;
                    qc = c;
                }
                else if (qc == c)
                {
                    inq = false;
                }
                cur.push_back(c);
                continue;
            }
            if (!inq && is_space(static_cast<unsigned char>(c)))
            {
                if (!cur.empty())
                {
                    toks.push_back(cur);
                    cur.clear();
                }
            }
            else
            {
                cur.push_back(c);
            }
        }
        if (!cur.empty())
        {
            toks.push_back(cur);
        }
        return toks;
    }

    inline std::string unquote(std::string v)
    {
        if (v.size() >= 2 && ((v.front() == '\"' && v.back() == '\"') || (v.front() == '\'' && v.back() == '\'')))
        {
            return v.substr(1, v.size() - 2);
        }
        return v;
    }

    inline bool split_endpoint(std::string_view endpoint, std::string& node, std::string& slot)
    {
        const auto trimmed = trim(std::string(endpoint));
        const auto pos = trimmed.find('.');
        if (pos == std::string::npos)
        {
            return false;
        }

        node = trim(trimmed.substr(0, pos));
        slot = trim(trimmed.substr(pos + 1));
        return !(node.empty() || slot.empty());
    }
} // namespace ce::strings
