#pragma once

#include "../../Render/Temporal/TemporalReconstruction.h"
#include <string>

#ifndef CREATOR_ENABLE_XESS_SDK
#define CREATOR_ENABLE_XESS_SDK 0
#endif
#ifndef CREATOR_ENABLE_XESS_VULKAN_SDK
#define CREATOR_ENABLE_XESS_VULKAN_SDK 0
#endif

// Header/source contract, not a claim that an arbitrary installed DLL is this revision.
inline constexpr const char* kXeSSSdkVersion = "Intel XeSS SDK 3.0.2 headers";
inline constexpr const char* kXeSSSdkRevision = "8fe81bdbbaf00b3c1b733fd0d830c333dc84e6f0";

inline std::string XeSSComponentVersion(const char* component, uint16_t major, uint16_t minor, uint16_t patch)
{
    return std::string(component) + " API " + std::to_string(major) + "." + std::to_string(minor) + "." +
        std::to_string(patch);
}

#if defined(_WIN32) && (CREATOR_ENABLE_XESS_SDK || CREATOR_ENABLE_XESS_VULKAN_SDK)
#include <Windows.h>
#include <filesystem>

// SDK-only loader. The host chooses an absolute, trusted deployment directory.
// Do not search the working directory, PATH, or load unapproved downloaded code.
class XeSSModule final
{
public:
    XeSSModule() = default;
    ~XeSSModule()
    {
        if (m_module)
            FreeLibrary(m_module);
    }
    XeSSModule(const XeSSModule&) = delete;
    XeSSModule& operator=(const XeSSModule&) = delete;

    TemporalResult Load(const wchar_t* directory, const wchar_t* filename)
    {
        if (m_module) return { TemporalStatus::AlreadyInitialized };
        if (!directory || !*directory || !std::filesystem::path(directory).is_absolute())
            return { TemporalStatus::InvalidInput };
        const auto path = std::filesystem::path(directory) / filename;
        m_module = LoadLibraryExW(path.c_str(), nullptr,
            LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
        return m_module ? TemporalResult{ TemporalStatus::Success }
                        : TemporalResult{ TemporalStatus::RuntimeUnavailable, GetLastError() };
    }

    template<typename T>
    bool Resolve(T& function, const char* name) const
    {
        function = reinterpret_cast<T>(GetProcAddress(m_module, name));
        return function != nullptr;
    }

private:
    HMODULE m_module{ nullptr };
};

inline TemporalResult XeSSMissingExport()
{
    return { TemporalStatus::SdkVersionMismatch, ERROR_PROC_NOT_FOUND };
}
#endif
