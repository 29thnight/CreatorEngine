#pragma once
#include <string>
#include <charconv>
#include <type_traits>
#include <stdexcept>
#include <optional>
#include "StringHelper.h"

// Helper for static_assert fallback
template<typename>
constexpr bool always_false = false;

template<typename T>
T convertFromString(const std::string& str)
{
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        T value{};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc())
            throw std::runtime_error("Invalid integer: " + str);
        return value;
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        T value{};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc())
            throw std::runtime_error("Invalid float: " + str);
        return value;
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        return str == "true" || str == "1" || str == "TRUE";
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return std::string(AnsiToUtf8(str));
    }
    else
    {
        static_assert(always_false<T>, "Unsupported type for convertFromString()");
    }
}

template<typename T>
std::optional<T> tryConvertFromString(const std::string& str)
{
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>)
    {
        T value{};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value);
        if (ec != std::errc())
            return std::nullopt;
        return value;
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        T value{};
        auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), value, std::chars_format::general);
        if (ec != std::errc())
            return std::nullopt;
        return value;
    }
    else if constexpr (std::is_same_v<T, bool>)
    {
        if (str == "true" || str == "1" || str == "TRUE") return true;
        if (str == "false" || str == "0" || str == "FALSE") return false;
        return std::nullopt;
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return std::string(AnsiToUtf8(str));
    }
    else
    {
        static_assert([] { return false; }(), "Unsupported type for tryConvertFromString()");
    }
}

class CSVCell
{
public:
    explicit CSVCell(const std::string& value) : m_value(value) {}

    template<typename T>
    T as() const
    {
        return convertFromString<T>(m_value);
    }

    template<typename T>
    std::optional<T> try_as() const
    {
        return tryConvertFromString<T>(m_value);
    }

private:
    const std::string& m_value;
};

class CSVRowView
{
public:
    CSVRowView(const std::vector<std::string>& row,
        const std::unordered_map<std::string, std::size_t>& headerMap)
        : m_row(row), m_headerMap(headerMap) {
    }

    CSVCell operator[](const std::string& column) const
    {
        auto it = m_headerMap.find(column);
        if (it == m_headerMap.end())
            throw std::out_of_range("Invalid column name: " + column);
        return CSVCell(m_row[it->second]);
    }

    CSVCell operator[](const int _index) const
    {
        if (_index >= m_row.size())
            throw std::out_of_range("Index out of range");
        return CSVCell(m_row[_index]);
    }

    int GetSize() const { return m_row.size(); }
private:
    const std::vector<std::string>& m_row;
    const std::unordered_map<std::string, std::size_t>& m_headerMap;
};