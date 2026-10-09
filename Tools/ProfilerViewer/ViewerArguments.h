#pragma once

#include "ProfilerViewerProtocol.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <span>
#include <string>
#include <string_view>

namespace ce::profiler_viewer
{
    struct viewer_arguments
    {
        connection_options connection;
        std::filesystem::path open_path;
        std::uint32_t ui_scale_milli = 0;
        bool live = false;
    };

    template <typename T>
    bool parse_decimal(std::wstring_view text, T& result)
    {
        if (text.empty())
        {
            return false;
        }
        T value = 0;
        for (const wchar_t character : text)
        {
            if (character < L'0' || character > L'9')
            {
                return false;
            }
            const T digit = static_cast<T>(character - L'0');
            if (value > (std::numeric_limits<T>::max() - digit) / 10)
            {
                return false;
            }
            value = value * 10 + digit;
        }
        result = value;
        return value != 0;
    }

    inline bool safe_capture_path(const std::filesystem::path& input,
                                  std::filesystem::path& result)
    {
        const std::wstring raw = input.native();
        if (raw.empty() || raw.size() > 32760 || raw.starts_with(L"\\\\") ||
            raw.starts_with(L"\\??\\") || raw.find(L'\0') != std::wstring::npos)
        {
            return false;
        }
        std::error_code error;
        const auto absolute = std::filesystem::absolute(input, error).lexically_normal();
        const std::wstring native = absolute.native();
        if (error || native.size() < 3 || native[1] != L':' ||
            (native[2] != L'\\' && native[2] != L'/') || native.find(L':', 2) != std::wstring::npos)
        {
            return false;
        }
        auto extension = absolute.extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t c)
        {
            return c >= L'A' && c <= L'Z' ? static_cast<wchar_t>(c + L'a' - L'A') : c;
        });
        if (extension != L".ceprof" && extension != L".cedx")
        {
            return false;
        }
        // Do not open/read/index the capture on the UI thread. The presenter
        // worker validates file kind, limits, checksums and source availability.
        result = absolute;
        return true;
    }

    inline bool parse_arguments(std::span<const std::wstring_view> values, viewer_arguments& result)
    {
        result = {};
        if (values.empty())
        {
            return true;
        }
        if (values.size() == 2 && values[0] == L"--open")
        {
            return safe_capture_path(std::filesystem::path(values[1]), result.open_path);
        }
        if (values.size() != 8 && values.size() != 10)
        {
            return false;
        }
        unsigned seen = 0;
        for (std::size_t index = 0; index < values.size(); index += 2)
        {
            const auto flag = values[index];
            const auto value = values[index + 1];
            unsigned bit = 0;
            bool valid = false;
            if (flag == L"--parent")
            {
                bit = 1;
                valid = parse_decimal(value, result.connection.target_pid);
            }
            else if (flag == L"--created")
            {
                bit = 2;
                valid = parse_decimal(value, result.connection.target_creation_time);
            }
            else if (flag == L"--session")
            {
                bit = 4;
                valid = parse_decimal(value, result.connection.windows_session_id);
            }
            else if (flag == L"--ui-scale-milli")
            {
                bit = 16;
                valid = parse_decimal(value, result.ui_scale_milli) &&
                    result.ui_scale_milli >= 500 && result.ui_scale_milli <= 3000;
            }
            else if (flag == L"--nonce")
            {
                bit = 8;
                valid = value.size() == result.connection.nonce.size() * 2;
                for (std::size_t digit = 0; valid && digit < value.size(); ++digit)
                {
                    const wchar_t c = value[digit];
                    const int nibble = c >= L'0' && c <= L'9' ? c - L'0' :
                        (c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1);
                    if (nibble < 0)
                    {
                        valid = false;
                    }
                    else
                    {
                        auto& byte = result.connection.nonce[digit / 2];
                        byte = static_cast<std::uint8_t>((byte << 4) | nibble);
                    }
                }
                valid = valid && result.connection.nonce != session_nonce{};
            }
            if (!valid || bit == 0 || (seen & bit) != 0)
            {
                return false;
            }
            seen |= bit;
        }
        result.live = seen == 15 || seen == 31;
        return result.live;
    }
}
