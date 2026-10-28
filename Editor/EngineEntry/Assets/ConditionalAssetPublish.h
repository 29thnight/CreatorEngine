#include "../../../Engine/Containers/ContainerAPI.h"
#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Engine/Platform/WindowsSDK/Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif
#include "../../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace Editor::AuthoringPublish
{
    inline constexpr std::wstring_view recoverySuffix = L".tmp.autosave-original";

    inline std::filesystem::path RecoveryPath(const std::filesystem::path& destination)
    {
        return std::filesystem::path(destination.native() + std::wstring(recoverySuffix));
    }

    class FileHandle final
    {
    public:
        explicit FileHandle(HANDLE value) : value_(value) {}
        ~FileHandle() { Close(); }
        FileHandle(const FileHandle&) = delete;
        FileHandle& operator=(const FileHandle&) = delete;
        HANDLE Get() const { return value_; }
        void Close()
        {
            if (value_ != INVALID_HANDLE_VALUE)
            {
                ::CloseHandle(value_);
                value_ = INVALID_HANDLE_VALUE;
            }
        }
    private:
        HANDLE value_{ INVALID_HANDLE_VALUE };
    };

    inline bool RenameWithoutReplacement(HANDLE handle, const std::filesystem::path& destination)
    {
        const auto nameBytes = destination.native().size() * sizeof(wchar_t);
        if (nameBytes > (std::numeric_limits<DWORD>::max)() - sizeof(FILE_RENAME_INFO))
        {
            return false;
        }
        ce::dynamic_array<std::byte> storage(sizeof(FILE_RENAME_INFO) + nameBytes);
        auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
        rename->ReplaceIfExists = FALSE;
        rename->RootDirectory = nullptr;
        rename->FileNameLength = static_cast<DWORD>(nameBytes);
        std::memcpy(rename->FileName, destination.c_str(), nameBytes);
        return ::SetFileInformationByHandle(handle, FileRenameInfo, rename,
            static_cast<DWORD>(storage.size())) != FALSE;
    }

    // A leftover exists only between retiring the original and completing or
    // rolling back publication. Never replace a file that another writer created.
    inline bool Recover(const std::filesystem::path& destination, std::string& failure)
    {
        const auto backup = RecoveryPath(destination);
        std::error_code error;
        const bool exists = std::filesystem::exists(backup, error);
        if (error)
        {
            failure = "Could not inspect automatic save recovery: " + backup.string();
            return false;
        }
        if (!exists)
        {
            return true;
        }
        // A live publisher holds DELETE access without sharing delete. Opening
        // this recovery handle therefore also excludes another running publisher.
        FileHandle original(::CreateFileW(backup.c_str(), GENERIC_READ | DELETE,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (original.Get() == INVALID_HANDLE_VALUE)
        {
            failure = "Automatic save recovery is busy: " + backup.string();
            return false;
        }
        const bool destinationExists = std::filesystem::exists(destination, error);
        if (error || (!destinationExists && !RenameWithoutReplacement(original.Get(), destination)))
        {
            failure = "Automatic save original retained for recovery: " + backup.string();
            return false;
        }
        original.Close();
        if (destinationExists)
        {
            std::filesystem::remove(backup, error);
            if (error)
            {
                failure = "Could not remove completed automatic save recovery: " + backup.string();
                return false;
            }
        }
        return true;
    }

    struct NoObserver
    {
        void operator()() const noexcept {}
    };

    // The observer is a compile-time test seam; production instantiates NoObserver.
    // Canonical readers may briefly find no file, but never a partial payload.
    template<class Observer = NoObserver>
    bool Publish(const std::filesystem::path& destination, const std::filesystem::path& staged,
        std::string_view expected, std::string& failure, Observer afterRetire = {})
    {
        failure.clear();
        if (!Recover(destination, failure))
        {
            return false;
        }
        const auto backup = RecoveryPath(destination);
        FileHandle original(::CreateFileW(destination.c_str(), GENERIC_READ | DELETE,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (original.Get() == INVALID_HANDLE_VALUE)
        {
            failure = "The source changed on disk or is busy; pending edits were not written. (Win32="
                + std::to_string(::GetLastError()) + ")";
            return false;
        }
        BY_HANDLE_FILE_INFORMATION information{};
        LARGE_INTEGER size{};
        if (!::GetFileInformationByHandle(original.Get(), &information)
            || (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
            || !::GetFileSizeEx(original.Get(), &size) || size.QuadPart < 0
            || static_cast<unsigned long long>(size.QuadPart) != expected.size())
        {
            failure = "The source changed on disk; pending edits were not written.";
            return false;
        }
        char buffer[64u * 1024u];
        size_t offset{};
        while (offset < expected.size())
        {
            const auto count = static_cast<DWORD>((std::min)(sizeof(buffer), expected.size() - offset));
            DWORD read{};
            if (!::ReadFile(original.Get(), buffer, count, &read, nullptr) || read != count
                || std::memcmp(buffer, expected.data() + offset, count) != 0)
            {
                failure = "The source changed on disk; pending edits were not written.";
                return false;
            }
            offset += count;
        }
        if (!RenameWithoutReplacement(original.Get(), backup))
        {
            failure = "Could not reserve automatic save original. (Win32="
                + std::to_string(::GetLastError()) + ")";
            return false;
        }
        bool published{};
        try
        {
            afterRetire();
            // No REPLACE_EXISTING: a competing pathname creation wins.
            published = ::MoveFileExW(staged.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        }
        catch (...)
        {
            // Even an observer exception leaves either the restored original or
            // a recovery copy; it must not overwrite a competing destination.
            RenameWithoutReplacement(original.Get(), destination);
            throw;
        }
        if (!published)
        {
            const auto publishError = ::GetLastError();
            const bool restored = RenameWithoutReplacement(original.Get(), destination);
            original.Close();
            std::error_code error;
            if (!restored && std::filesystem::exists(destination, error) && !error)
            {
                std::filesystem::remove(backup, error);
            }
            failure = "The source changed on disk or publication failed; pending edits were not written. (Win32="
                + std::to_string(publishError) + ")";
            if (std::filesystem::exists(backup, error))
            {
                failure += " Original retained at " + backup.string();
            }
            return false;
        }
        original.Close();
        std::error_code error;
        std::filesystem::remove(backup, error);
        // A cleanup failure leaves a recoverable journal; publication succeeded.
        return true;
    }
}
