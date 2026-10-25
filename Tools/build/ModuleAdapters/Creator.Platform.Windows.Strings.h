#pragma once
// Generated from Engine/Contracts/Platform/Windows/Creator.Platform.Windows.Strings.ixx; do not edit.
// Regenerate with: py Tools/build/generate-module-adapters.py
#if !defined(CE_CONTRACT_STANDALONE)
#error "Native consumers must import the named module directly."
#endif


#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#include "../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"

namespace ce::strings
{
    inline std::string ansi_to_utf8(const std::string& ansi_str)
    {
        // ANSI → Wide
        int wide_len = MultiByteToWideChar(CP_ACP, 0, ansi_str.c_str(), -1, nullptr, 0);
        std::wstring wide(wide_len, 0);
        MultiByteToWideChar(CP_ACP, 0, ansi_str.c_str(), -1, &wide[0], wide_len);

        // Wide → UTF-8
        int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string utf8(utf8_len, 0);
        WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, &utf8[0], utf8_len, nullptr, nullptr);

        return utf8;
    }

    inline std::string utf8_to_ansi(const std::string& utf8_str)
    {
        if (utf8_str.empty())
        {
            return {};
        }

        // UTF-8 -> Wide
        int wide_len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str.c_str(), -1, nullptr, 0);
        if (wide_len <= 0)
        {
            return {};
        }

        std::wstring wide(wide_len, 0);
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_str.c_str(), -1, &wide[0], wide_len) <= 0)
        {
            return {};
        }

        // Wide -> ANSI (ACP)
        BOOL used_default = FALSE; // true면 일부 문자가 치환(손실)됨
        int ansi_len =
            WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, nullptr, 0, nullptr, &used_default);
        if (ansi_len <= 0)
        {
            return {};
        }

        std::string ansi(ansi_len, 0);
        if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, wide.c_str(), -1, &ansi[0], ansi_len, nullptr,
                                &used_default) <= 0)
        {
            return {};
        }

        // std::string은 널 종료가 필요없으니 끝의 '\0' 제거
        if (!ansi.empty() && ansi.back() == '\0')
        {
            ansi.pop_back();
        }

        // 필요하다면 usedDefault를 확인해서 로깅/경고 가능
        // if (used_default) { /* 일부 문자가 치환됨 */ }

        return ansi;
    }

    inline bool try_wide_to_utf8(std::wstring_view value, std::string& output, bool strict = true)
    {
        if (value.empty())
        {
            output.clear();
            return true;
        }
        if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }
        const auto length = static_cast<int>(value.size());
        const DWORD flags = strict ? WC_ERR_INVALID_CHARS : 0;
        const int count = WideCharToMultiByte(CP_UTF8, flags, value.data(), length, nullptr, 0, nullptr, nullptr);
        if (count <= 0)
        {
            return false;
        }
        std::string result(static_cast<std::size_t>(count), '\0');
        if (WideCharToMultiByte(CP_UTF8, flags, value.data(), length, result.data(), count, nullptr, nullptr) != count)
        {
            return false;
        }
        output = std::move(result);
        return true;
    }

    inline bool try_utf8_to_wide(std::string_view value, std::wstring& output, bool strict = true)
    {
        if (value.empty())
        {
            output.clear();
            return true;
        }
        if (value.size() > static_cast<std::size_t>((std::numeric_limits<int>::max)()))
        {
            return false;
        }
        const auto length = static_cast<int>(value.size());
        const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
        const int count = MultiByteToWideChar(CP_UTF8, flags, value.data(), length, nullptr, 0);
        if (count <= 0)
        {
            return false;
        }
        std::wstring result(static_cast<std::size_t>(count), L'\0');
        if (MultiByteToWideChar(CP_UTF8, flags, value.data(), length, result.data(), count) != count)
        {
            return false;
        }
        output = std::move(result);
        return true;
    }

    inline std::string wide_to_utf8(std::wstring_view value, bool strict = false)
    {
        std::string result;
        try_wide_to_utf8(value, result, strict);
        return result;
    }

    inline std::wstring utf8_to_wide(std::string_view value, bool strict = false)
    {
        std::wstring result;
        try_utf8_to_wide(value, result, strict);
        return result;
    }
    inline std::string wide_to_utf8(const wchar_t* value, bool strict = false)
    {
        return wide_to_utf8(value ? std::wstring_view(value) : std::wstring_view{}, strict);
    }

    inline std::wstring utf8_to_wide(const char* value, bool strict = false)
    {
        return utf8_to_wide(value ? std::string_view(value) : std::string_view{}, strict);
    }
} // namespace ce::strings
