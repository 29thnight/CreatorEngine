#pragma once

#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <io.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace AssetDepot
{
    enum class ArtifactStoreAccess : std::uint8_t
    {
        Acquired,
        Unmanaged,
        Busy,
        Invalid,
    };

    // Local-filesystem protocol, shared by producers, loose sources, pak archives
    // and an eventual collector. One lock covers an immutable release directory
    // or one pak, not each artifact. No payload bytes are retained here.
    //
    // The sibling .asset-store.guard is persistent protocol metadata. Never move,
    // replace, unlink, or include it in backing cleanup. All engine deletion,
    // replacement and repack must retain a successful exclusive capability and
    // revalidate it immediately before mutation. External tampering can only be
    // detected, not prevented on every OS. Network/distributed storage is outside
    // this local protocol. Missing enrollment NEVER authorizes collection.
    class ArtifactStoreGuard final
    {
    public:
        ArtifactStoreGuard() = default;
        ArtifactStoreGuard(const ArtifactStoreGuard&) = delete;
        ArtifactStoreGuard& operator=(const ArtifactStoreGuard&) = delete;
        ArtifactStoreGuard(ArtifactStoreGuard&& other) noexcept
        {
            Swap(other);
        }
        ArtifactStoreGuard& operator=(ArtifactStoreGuard&& other) noexcept
        {
            ArtifactStoreGuard retired(std::move(other));
            Swap(retired);
            return *this;
        }
        ~ArtifactStoreGuard()
        {
            Close(m_handle);
        }

        // Metadata I/O: use only during source/archive construction, before a
        // mount is published. Unmanaged permits legacy reading, not collection.
        [[nodiscard]] static ArtifactStoreAccess OpenShared(const std::filesystem::path& backing,
            ArtifactStoreGuard& out, std::string& failure)
        {
            return Open(backing, false, out, failure);
        }

        // This returns a held capability, never a racy "safe to delete" query.
        // On failure out is unchanged. The caller must first retire every active
        // release/manifest reference and keep this object through physical work.
        [[nodiscard]] static ArtifactStoreAccess TryAcquireCollection(const std::filesystem::path& backing,
            ArtifactStoreGuard& out, std::string& failure)
        {
            return Open(backing, true, out, failure);
        }

        // Reserve a NEW immutable backing path before publication. Existing
        // backing cannot be retroactively enrolled while legacy readers exist.
        // The producer owns the exclusive guard through rename and completion.
        [[nodiscard]] static bool BeginPublication(const std::filesystem::path& backing,
            ArtifactStoreGuard& out, std::string& failure)
        {
            ArtifactStoreGuard candidate;
            if (!PreparePath(backing, candidate.m_backing, failure))
            {
                return false;
            }
            std::error_code error;
            if (std::filesystem::exists(candidate.m_backing, error) || error)
            {
                failure = "artifact-store enrollment requires a new immutable backing path";
                return false;
            }
            const auto status = candidate.OpenGuard(true, true, failure);
            if (status != ArtifactStoreAccess::Acquired)
            {
                return false;
            }
            // Recheck after exclusion: another producer may have published while
            // this process resolved the path. Do not overwrite its guard record.
            if (std::filesystem::exists(candidate.m_backing, error) || error)
            {
                failure = "artifact-store backing was published by another producer";
                return false;
            }
            candidate.m_publication = true;
            candidate.m_exclusive = true;
            out = std::move(candidate);
            failure.clear();
            return true;
        }

        // Call immediately after atomic publication, before reporting success.
        // A failed/partial record is fail-closed, never inferred from file age.
        [[nodiscard]] bool CommitPublication(std::string& failure)
        {
            if (!m_publication || !m_exclusive || !ValidateGuardPath(failure)
                || !ReadIdentity(m_backing, m_identity, failure, &m_incarnation))
            {
                if (failure.empty())
                {
                    failure = "artifact-store publication guard is absent";
                }
                return false;
            }
            const std::string record = std::string(kVersion) + std::to_string(m_identity.volume) + "\n"
                + std::to_string(m_identity.file) + "\n" + std::to_string(m_identity.directory) + "\n"
                + std::to_string(m_incarnation.stamp) + "\n" + std::to_string(m_incarnation.nanoseconds) + "\n";
#if defined(_WIN32)
            LARGE_INTEGER offset{};
            DWORD written{};
            if (!SetFilePointerEx(m_handle, offset, nullptr, FILE_BEGIN)
                || !WriteFile(m_handle, record.data(), static_cast<DWORD>(record.size()), &written, nullptr)
                || written != record.size() || !SetEndOfFile(m_handle) || !FlushFileBuffers(m_handle))
#else
            if (::pwrite(m_handle, record.data(), record.size(), 0) != static_cast<ssize_t>(record.size())
                || ::ftruncate(m_handle, static_cast<off_t>(record.size())) != 0 || ::fsync(m_handle) != 0)
#endif
            {
                failure = "artifact-store guard publication record could not be persisted";
                return false;
            }
            m_publication = false;
            m_managed = true;
            return ValidateBacking(failure);
        }

        [[nodiscard]] bool IsManaged() const noexcept { return m_managed; }
        [[nodiscard]] const std::filesystem::path& BackingPath() const noexcept { return m_backing; }

        // Worker-side metadata I/O. The identity in the persistent guard must
        // still describe this exact backing; never follow a replacement root.
        [[nodiscard]] bool ValidateBacking(std::string& failure) const
        {
            Identity current;
            Incarnation incarnation;
            if (m_backing.empty() || (m_managed && !ValidateGuardPath(failure))
                || !ReadIdentity(m_backing, current, failure, &incarnation)
                || current != m_identity || (m_managed && incarnation != m_incarnation))
            {
                if (failure.empty())
                {
                    failure = "artifact-store backing identity changed";
                }
                return false;
            }
            failure.clear();
            return true;
        }

        // Pak::Archive calls this after opening its native FILE, closing the
        // guard-to-file-open race without exposing or adopting native ownership.
        [[nodiscard]] bool ValidateOpenedFile(std::FILE* file, std::string& failure) const
        {
            if (!file)
            {
                failure = "artifact-store native file is absent";
                return false;
            }
            Identity current;
            Incarnation incarnation;
#if defined(_WIN32)
            const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file)));
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(handle, &information)
                || (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u)
            {
                failure = "artifact-store opened backing has no valid file identity";
                return false;
            }
            current = { information.dwVolumeSerialNumber,
                (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) | information.nFileIndexLow, 0u };
            incarnation = { (static_cast<std::uint64_t>(information.ftCreationTime.dwHighDateTime) << 32u)
                | information.ftCreationTime.dwLowDateTime, 0u };
#else
            struct stat information{};
            if (::fstat(::fileno(file), &information) != 0 || !S_ISREG(information.st_mode))
            {
                failure = "artifact-store opened backing has no valid file identity";
                return false;
            }
            current = { static_cast<std::uint64_t>(information.st_dev),
                static_cast<std::uint64_t>(information.st_ino), 0u };
            incarnation = PosixIncarnation(information);
#endif
            if (current != m_identity || (m_managed && incarnation != m_incarnation))
            {
                failure = "artifact-store opened file differs from the leased backing";
                return false;
            }
            return ValidateBacking(failure);
        }

        [[nodiscard]] bool RevalidateCollection(std::string& failure) const
        {
            if (!m_exclusive || !m_managed || m_publication)
            {
                failure = "physical collection requires a held exclusive artifact-store capability";
                return false;
            }
            return ValidateBacking(failure);
        }

    private:
        struct Identity final
        {
            std::uint64_t volume{};
            std::uint64_t file{};
            std::uint64_t directory{};
            friend bool operator==(const Identity&, const Identity&) = default;
        };
        // Backing incarnation is distinct from the guard's stable file identity:
        // publishing the guard record changes the guard inode's own ctime.
        struct Incarnation final
        {
            std::uint64_t stamp{};
            std::uint64_t nanoseconds{};
            friend bool operator==(const Incarnation&, const Incarnation&) = default;
        };
#if !defined(_WIN32)
        [[nodiscard]] static Incarnation PosixIncarnation(const struct stat& information) noexcept
        {
#if defined(__APPLE__)
            return { static_cast<std::uint64_t>(information.st_ctimespec.tv_sec),
                static_cast<std::uint64_t>(information.st_ctimespec.tv_nsec) };
#else
            return { static_cast<std::uint64_t>(information.st_ctim.tv_sec),
                static_cast<std::uint64_t>(information.st_ctim.tv_nsec) };
#endif
        }
#endif
#if defined(_WIN32)
        using NativeHandle = HANDLE;
        static inline const NativeHandle kInvalidHandle = INVALID_HANDLE_VALUE;
#else
        using NativeHandle = int;
        static constexpr NativeHandle kInvalidHandle = -1;
#endif
        static constexpr std::string_view kVersion = "CreatorEngineArtifactStore1\n";
        NativeHandle m_handle{ kInvalidHandle };
        std::filesystem::path m_backing;
        Identity m_identity{};
        Incarnation m_incarnation{};
        Identity m_guardIdentity{};
        bool m_managed{};
        bool m_exclusive{};
        bool m_publication{};

        void Swap(ArtifactStoreGuard& other) noexcept
        {
            std::swap(m_handle, other.m_handle);
            m_backing.swap(other.m_backing);
            std::swap(m_identity, other.m_identity);
            std::swap(m_incarnation, other.m_incarnation);
            std::swap(m_guardIdentity, other.m_guardIdentity);
            std::swap(m_managed, other.m_managed);
            std::swap(m_exclusive, other.m_exclusive);
            std::swap(m_publication, other.m_publication);
        }

        static void Close(NativeHandle handle) noexcept
        {
            if (handle != kInvalidHandle)
            {
#if defined(_WIN32)
                CloseHandle(handle);
#else
                ::close(handle);
#endif
            }
        }

        [[nodiscard]] static bool NoAliases(const std::filesystem::path& path, std::string& failure)
        {
            for (auto probe = path; !probe.empty(); probe = probe.parent_path())
            {
#if defined(_WIN32)
                auto probeName = probe.native();
                const bool extendedDriveRoot = probeName.starts_with(L"\\\\?\\")
                    && (probeName.size() == 6u || (probeName.size() == 7u && probeName.back() == L'\\'))
                    && probeName[5u] == L':';
                if (extendedDriveRoot && probeName.size() == 6u)
                {
                    probeName += L'\\';
                }
                const auto attributes = GetFileAttributesW(probeName.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES)
                {
                    const auto error = GetLastError();
                    if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
                    {
                        failure = "artifact-store path attributes are unavailable";
                        return false;
                    }
                }
                else if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0u)
#else
                struct stat information{};
                if (::lstat(probe.c_str(), &information) != 0)
                {
                    if (errno != ENOENT)
                    {
                        failure = "artifact-store path attributes are unavailable";
                        return false;
                    }
                }
                else if (S_ISLNK(information.st_mode))
#endif
                {
                    failure = "artifact-store path crosses a symlink or reparse point";
                    return false;
                }
#if defined(_WIN32)
                if (extendedDriveRoot)
                {
                    break;
                }
#endif
                if (probe == probe.parent_path())
                {
                    break;
                }
            }
            return true;
        }

        [[nodiscard]] static bool PreparePath(const std::filesystem::path& path,
            std::filesystem::path& out, std::string& failure)
        {
            failure.clear();
            std::error_code error;
            if (path.empty())
            {
                failure = "artifact-store backing path is empty";
                return false;
            }
            const auto absolute = std::filesystem::absolute(path, error);
            if (error || !NoAliases(absolute, failure))
            {
                if (failure.empty())
                {
                    failure = "artifact-store backing path cannot be resolved";
                }
                return false;
            }
            out = std::filesystem::weakly_canonical(absolute, error);
            if (error || !NoAliases(out, failure))
            {
                failure = "artifact-store canonical backing path cannot be resolved";
                return false;
            }
#if defined(_WIN32)
            // Resolve short-name/drive aliases to the same sidecar namespace.
            // For a new publication, canonicalize its existing parent instead.
            const bool exists = std::filesystem::exists(out, error);
            if (error)
            {
                failure = "artifact-store backing existence cannot be checked";
                return false;
            }
            const auto resolved = exists ? out : out.parent_path();
            const HANDLE handle = CreateFileW(resolved.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                failure = "artifact-store canonical backing identity cannot be opened";
                return false;
            }
            struct CanonicalHandle final
            {
                HANDLE value;
                ~CanonicalHandle() { CloseHandle(value); }
            };
            const CanonicalHandle retainedHandle{ handle };
            const DWORD required = GetFinalPathNameByHandleW(handle, nullptr, 0u, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            std::vector<wchar_t> name(static_cast<std::size_t>(required) + 1u);
            const DWORD length = required == 0u ? 0u : GetFinalPathNameByHandleW(handle, name.data(),
                static_cast<DWORD>(name.size()), FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
            if (length == 0u || length >= name.size())
            {
                failure = "artifact-store canonical backing name cannot be read";
                return false;
            }
            std::wstring canonicalName(name.data(), length);
            if (canonicalName.starts_with(L"\\\\?\\UNC\\"))
            {
                failure = "artifact-store leases require a local filesystem";
                return false;
            }
            std::wstring driveName = canonicalName;
            if (driveName.starts_with(L"\\\\?\\"))
            {
                driveName.erase(0u, 4u);
            }
            const auto canonical = std::filesystem::path(canonicalName);
            const auto driveRoot = std::filesystem::path(driveName).root_path();
            const auto driveType = GetDriveTypeW(driveRoot.c_str());
            if (driveType != DRIVE_FIXED && driveType != DRIVE_REMOVABLE
                && driveType != DRIVE_RAMDISK && driveType != DRIVE_CDROM)
            {
                failure = "artifact-store leases require a local filesystem";
                return false;
            }
            out = exists ? canonical : canonical / out.filename();
#endif
            // A root cannot have a persistent sibling outside its cleanup scope.
            if (out.filename().empty() || out == out.root_path())
            {
                failure = "artifact-store backing must have a distinct parent directory";
                return false;
            }
            return true;
        }

        [[nodiscard]] std::filesystem::path GuardPath() const
        {
            auto path = m_backing;
            path += ".asset-store.guard";
            return path;
        }

        [[nodiscard]] static bool ReadIdentity(const std::filesystem::path& path,
            Identity& out, std::string& failure, Incarnation* incarnation = nullptr)
        {
            if (!NoAliases(path, failure))
            {
                return false;
            }
#if defined(_WIN32)
            const HANDLE handle = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
            {
                failure = "artifact-store backing identity cannot be opened";
                return false;
            }
            BY_HANDLE_FILE_INFORMATION information{};
            const bool valid = GetFileInformationByHandle(handle, &information)
                && (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0u
                && ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u
                    || information.nNumberOfLinks == 1u);
            CloseHandle(handle);
            if (!valid)
            {
                failure = "artifact-store backing is aliased or has invalid identity";
                return false;
            }
            out = { information.dwVolumeSerialNumber,
                (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) | information.nFileIndexLow,
                (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u ? 1u : 0u };
            if (incarnation)
            {
                *incarnation = { (static_cast<std::uint64_t>(information.ftCreationTime.dwHighDateTime) << 32u)
                    | information.ftCreationTime.dwLowDateTime, 0u };
            }
#else
            struct stat information{};
            if (::lstat(path.c_str(), &information) != 0
                || (!S_ISREG(information.st_mode) && !S_ISDIR(information.st_mode))
                || (S_ISREG(information.st_mode) && information.st_nlink != 1u))
            {
                failure = "artifact-store backing is absent, aliased or not a regular file/directory";
                return false;
            }
            out = { static_cast<std::uint64_t>(information.st_dev),
                static_cast<std::uint64_t>(information.st_ino), S_ISDIR(information.st_mode) ? 1u : 0u };
            if (incarnation)
            {
                *incarnation = PosixIncarnation(information);
            }
#endif
            return true;
        }

        [[nodiscard]] bool ValidateGuardPath(std::string& failure) const
        {
            Identity current;
            if (m_handle == kInvalidHandle || !ReadIdentity(GuardPath(), current, failure)
                || current != m_guardIdentity)
            {
                if (failure.empty())
                {
                    failure = "artifact-store persistent guard was replaced";
                }
                return false;
            }
            return true;
        }

        [[nodiscard]] ArtifactStoreAccess OpenGuard(bool exclusive, bool create, std::string& failure)
        {
            const auto path = GuardPath();
            if (!NoAliases(path, failure))
            {
                return ArtifactStoreAccess::Invalid;
            }
#if defined(_WIN32)
            m_handle = CreateFileW(path.c_str(), GENERIC_READ | (create ? GENERIC_WRITE : 0u),
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, create ? OPEN_ALWAYS : OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (m_handle == kInvalidHandle)
            {
                const auto error = GetLastError();
                if (!create && error == ERROR_FILE_NOT_FOUND)
                {
                    return ArtifactStoreAccess::Unmanaged;
                }
                failure = "artifact-store guard cannot be opened";
                return ArtifactStoreAccess::Invalid;
            }
            BY_HANDLE_FILE_INFORMATION information{};
            if (!GetFileInformationByHandle(m_handle, &information)
                || (information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0u
                || information.nNumberOfLinks != 1u)
            {
                failure = "artifact-store guard is not an unaliased regular file";
                return ArtifactStoreAccess::Invalid;
            }
            m_guardIdentity = { information.dwVolumeSerialNumber,
                (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) | information.nFileIndexLow, 0u };
            OVERLAPPED position{};
            if (!LockFileEx(m_handle, LOCKFILE_FAIL_IMMEDIATELY | (exclusive ? LOCKFILE_EXCLUSIVE_LOCK : 0u),
                0u, MAXDWORD, MAXDWORD, &position))
            {
                const auto error = GetLastError();
                failure = "artifact-store backing is leased or its guard cannot be locked";
                return error == ERROR_LOCK_VIOLATION ? ArtifactStoreAccess::Busy : ArtifactStoreAccess::Invalid;
            }
#else
            m_handle = ::open(path.c_str(), (create ? O_RDWR | O_CREAT : O_RDONLY)
                | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW, 0600);
            if (m_handle == kInvalidHandle)
            {
                if (!create && errno == ENOENT)
                {
                    return ArtifactStoreAccess::Unmanaged;
                }
                failure = "artifact-store guard cannot be opened";
                return ArtifactStoreAccess::Invalid;
            }
            struct stat information{};
            if (::fstat(m_handle, &information) != 0 || !S_ISREG(information.st_mode) || information.st_nlink != 1u)
            {
                failure = "artifact-store guard is not an unaliased regular file";
                return ArtifactStoreAccess::Invalid;
            }
            m_guardIdentity = { static_cast<std::uint64_t>(information.st_dev),
                static_cast<std::uint64_t>(information.st_ino), 0u };
            if (::flock(m_handle, (exclusive ? LOCK_EX : LOCK_SH) | LOCK_NB) != 0)
            {
                const int error = errno;
                failure = "artifact-store backing is leased or its guard cannot be locked";
                return error == EWOULDBLOCK || error == EAGAIN ? ArtifactStoreAccess::Busy : ArtifactStoreAccess::Invalid;
            }
#endif
            return ValidateGuardPath(failure) ? ArtifactStoreAccess::Acquired : ArtifactStoreAccess::Invalid;
        }

        [[nodiscard]] bool ReadRecord(std::string& failure)
        {
            std::array<char, 256u> bytes{};
            std::size_t length{};
#if defined(_WIN32)
            DWORD read{};
            if (!ReadFile(m_handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr))
#else
            const auto read = ::pread(m_handle, bytes.data(), bytes.size(), 0);
            if (read < 0)
#endif
            {
                failure = "artifact-store guard record cannot be read";
                return false;
            }
            length = static_cast<std::size_t>(read);
            std::string_view record(bytes.data(), length);
            if (!record.starts_with(kVersion) || length == bytes.size())
            {
                failure = "artifact-store guard is incomplete or uses an unsupported protocol";
                return false;
            }
            record.remove_prefix(kVersion.size());
            for (auto* value : { &m_identity.volume, &m_identity.file, &m_identity.directory,
                &m_incarnation.stamp, &m_incarnation.nanoseconds })
            {
                const auto end = record.find('\n');
                if (end == std::string_view::npos || end == 0u)
                {
                    failure = "artifact-store guard identity is malformed";
                    return false;
                }
                const auto parsed = std::from_chars(record.data(), record.data() + end, *value);
                if (parsed.ec != std::errc{} || parsed.ptr != record.data() + end)
                {
                    failure = "artifact-store guard identity is malformed";
                    return false;
                }
                record.remove_prefix(end + 1u);
            }
            if (!record.empty() || m_identity.directory > 1u || m_incarnation.nanoseconds >= 1000000000u)
            {
                failure = "artifact-store guard identity contains invalid fields";
                return false;
            }
            return true;
        }

        [[nodiscard]] static ArtifactStoreAccess Open(const std::filesystem::path& backing,
            bool exclusive, ArtifactStoreGuard& out, std::string& failure)
        {
            ArtifactStoreGuard candidate;
            if (!PreparePath(backing, candidate.m_backing, failure))
            {
                return ArtifactStoreAccess::Invalid;
            }
            const auto status = candidate.OpenGuard(exclusive, false, failure);
            if (status == ArtifactStoreAccess::Unmanaged)
            {
                if (exclusive)
                {
                    failure = "legacy/unmanaged artifact-store backing is not eligible for collection";
                    return status;
                }
                if (!ReadIdentity(candidate.m_backing, candidate.m_identity, failure, &candidate.m_incarnation))
                {
                    return ArtifactStoreAccess::Invalid;
                }
                out = std::move(candidate);
                failure.clear();
                return status;
            }
            if (status != ArtifactStoreAccess::Acquired || !candidate.ReadRecord(failure))
            {
                return status == ArtifactStoreAccess::Acquired ? ArtifactStoreAccess::Invalid : status;
            }
            candidate.m_managed = true;
            candidate.m_exclusive = exclusive;
            if (!candidate.ValidateBacking(failure))
            {
                return ArtifactStoreAccess::Invalid;
            }
            out = std::move(candidate);
            return ArtifactStoreAccess::Acquired;
        }
    };
}
