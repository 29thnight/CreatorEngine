#pragma once

// Editor와 helper가 같은 경계 검사를 사용해야 한쪽의 완화가 우회 경로가 되지 않는다.
// 이 파일은 Windows 전용 구현 내부에서만 포함한다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <AclAPI.h>
#include <sddl.h>

#include <array>
#include <cstring>
#include <cwchar>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "DxCaptureProtocol.h"

namespace ce::dx_capture::security
{
    class handle
    {
    public:
        handle() = default;
        explicit handle(HANDLE value) : value_(value) {}
        ~handle() { reset(); }
        handle(const handle&) = delete;
        handle& operator=(const handle&) = delete;
        handle(handle&& other) noexcept : value_(other.release()) {}
        handle& operator=(handle&& other) noexcept
        {
            reset(other.release());
            return *this;
        }
        HANDLE get() const { return value_; }
        explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
        HANDLE release() { return std::exchange(value_, nullptr); }
        void reset(HANDLE value = nullptr)
        {
            if (*this)
            {
                CloseHandle(value_);
            }
            value_ = value;
        }

    private:
        HANDLE value_ = nullptr;
    };

    struct local_deleter
    {
        void operator()(void* value) const { LocalFree(value); }
    };
    using local_memory = std::unique_ptr<void, local_deleter>;

    inline std::uint64_t creation_time(HANDLE process)
    {
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process, &creation, &exit, &kernel, &user))
        {
            return 0;
        }
        return (static_cast<std::uint64_t>(creation.dwHighDateTime) << 32) | creation.dwLowDateTime;
    }

    inline bool token_user(HANDLE process, std::vector<std::byte>& buffer)
    {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &raw))
        {
            return false;
        }
        handle token(raw);
        DWORD bytes = 0;
        GetTokenInformation(token.get(), TokenUser, nullptr, 0, &bytes);
        if (bytes == 0 || bytes > 65536)
        {
            return false;
        }
        buffer.resize(bytes);
        return GetTokenInformation(token.get(), TokenUser, buffer.data(), bytes, &bytes) != FALSE;
    }

    inline bool same_user(HANDLE first, HANDLE second)
    {
        std::vector<std::byte> first_user, second_user;
        return token_user(first, first_user) && token_user(second, second_user) &&
               EqualSid(reinterpret_cast<TOKEN_USER*>(first_user.data())->User.Sid,
                        reinterpret_cast<TOKEN_USER*>(second_user.data())->User.Sid);
    }

    inline bool logon_sid(HANDLE process, std::vector<std::byte>& sid)
    {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &raw))
        {
            return false;
        }
        handle token(raw);
        DWORD bytes = 0;
        GetTokenInformation(token.get(), TokenLogonSid, nullptr, 0, &bytes);
        if (bytes == 0 || bytes > 65536)
        {
            return false;
        }
        std::vector<std::byte> groups(bytes);
        if (!GetTokenInformation(token.get(), TokenLogonSid, groups.data(), bytes, &bytes))
        {
            return false;
        }
        const auto* value = reinterpret_cast<const TOKEN_GROUPS*>(groups.data());
        if (value->GroupCount != 1 || !IsValidSid(value->Groups[0].Sid))
        {
            return false;
        }
        sid.resize(GetLengthSid(value->Groups[0].Sid));
        return CopySid(static_cast<DWORD>(sid.size()), sid.data(), value->Groups[0].Sid) != FALSE;
    }

    inline bool same_logon(HANDLE first, HANDLE second)
    {
        std::vector<std::byte> first_sid, second_sid;
        return logon_sid(first, first_sid) && logon_sid(second, second_sid) &&
               EqualSid(first_sid.data(), second_sid.data());
    }

    inline bool query_elevation(HANDLE process, bool& result)
    {
        HANDLE raw = nullptr;
        if (!OpenProcessToken(process, TOKEN_QUERY, &raw))
        {
            return false;
        }
        handle token(raw);
        TOKEN_ELEVATION elevation{};
        DWORD bytes = sizeof(elevation);
        if (!GetTokenInformation(token.get(), TokenElevation, &elevation, bytes, &bytes))
        {
            return false;
        }
        result = elevation.TokenIsElevated != 0;
        return true;
    }

    inline bool process_identity(HANDLE process, DWORD process_id, std::uint64_t created, DWORD session)
    {
        DWORD actual_session = 0;
        return process && GetProcessId(process) == process_id && created != 0 &&
               creation_time(process) == created && ProcessIdToSessionId(process_id, &actual_session) &&
               actual_session == session && WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    }

    inline std::wstring image_path(HANDLE process)
    {
        std::wstring result(32768, L'\0');
        DWORD size = static_cast<DWORD>(result.size());
        if (!QueryFullProcessImageNameW(process, 0, result.data(), &size))
        {
            return {};
        }
        result.resize(size);
        return result;
    }

    inline std::wstring directory(const std::wstring& path)
    {
        const auto last = path.find_last_of(L'\\');
        return last == std::wstring::npos ? std::wstring{} : path.substr(0, last);
    }

    inline bool equal_path(const std::wstring& first, const std::wstring& second)
    {
        return !first.empty() && first.size() == second.size() &&
               CompareStringOrdinal(first.c_str(), static_cast<int>(first.size()), second.c_str(),
                                    static_cast<int>(second.size()), TRUE) == CSTR_EQUAL;
    }

    inline bool editor_image(const std::wstring& path)
    {
        const auto last = path.find_last_of(L'\\');
        const auto name = path.substr(last == std::wstring::npos ? 0 : last + 1);
        return equal_path(name, L"CreatorEditor.runtime.exe") || equal_path(name, L"CreatorEditor.exe");
    }

    inline std::wstring nonce_text(const session_nonce& nonce)
    {
        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring result;
        result.reserve(32);
        for (auto byte : nonce)
        {
            result.push_back(digits[byte >> 4]);
            result.push_back(digits[byte & 15]);
        }
        return result;
    }

    inline std::wstring pipe_name(DWORD parent_id, const session_nonce& nonce)
    {
        return L"\\\\.\\pipe\\CreatorEngine.DxCapture.v1." + std::to_wstring(parent_id) + L"." + nonce_text(nonce);
    }

    inline bool trusted_sid(PSID sid)
    {
        if (!sid || !IsValidSid(sid))
        {
            return false;
        }
        if (IsWellKnownSid(sid, WinLocalSystemSid) || IsWellKnownSid(sid, WinBuiltinAdministratorsSid))
        {
            return true;
        }
        PSID installer = nullptr;
        if (!ConvertStringSidToSidW(L"S-1-5-80-956008885-3418522649-1831038044-1853292631-2271478464", &installer))
        {
            return false;
        }
        local_memory release(installer);
        return EqualSid(sid, installer) != FALSE;
    }

    inline bool protected_acl(HANDLE file, bool directory, bool container)
    {
        PSID owner = nullptr;
        PACL acl = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (GetSecurityInfo(file, SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                            &owner, nullptr, &acl, nullptr, &descriptor) != ERROR_SUCCESS)
        {
            return false;
        }
        local_memory release(descriptor);
        if (!trusted_sid(owner) || !acl || !IsValidAcl(acl))
        {
            return false;
        }
        // 상위 폴더의 새 형제 생성은 이미 열린 자식을 바꾸지 않는다. helper 폴더의
        // 새 파일 생성은 DLL planting이므로 별도로 금지한다. 삭제·ACL 변경은 모두 금지다.
        DWORD dangerous = DELETE | WRITE_DAC | WRITE_OWNER | FILE_WRITE_ATTRIBUTES | FILE_WRITE_EA;
        dangerous |= directory ? FILE_DELETE_CHILD : FILE_WRITE_DATA | FILE_APPEND_DATA;
        if (container)
        {
            dangerous |= FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY;
        }
        for (DWORD index = 0; index < acl->AceCount; ++index)
        {
            void* raw = nullptr;
            if (!GetAce(acl, index, &raw))
            {
                return false;
            }
            const auto* header = static_cast<ACE_HEADER*>(raw);
            if ((header->AceFlags & INHERIT_ONLY_ACE) != 0 || header->AceType == ACCESS_DENIED_ACE_TYPE)
            {
                continue;
            }
            // callback/object ACE를 추측해서 허용하지 않는다. 지원하지 않는 ACL은 fail closed다.
            if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            {
                return false;
            }
            const auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
            auto mask = ace->Mask;
            GENERIC_MAPPING mapping{FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
            MapGenericMask(&mask, &mapping);
            if ((mask & dangerous) != 0 && !trusted_sid(const_cast<DWORD*>(&ace->SidStart)))
            {
                return false;
            }
        }
        return true;
    }

    struct pinned_image
    {
        std::vector<handle> handles;
        std::wstring path;

        HANDLE file() const { return handles.empty() ? nullptr : handles.back().get(); }

        bool open(const std::wstring& candidate, bool require_protected)
        {
            handles.clear();
            path.clear();
            if (candidate.size() < 4 || candidate.size() > 32760 || candidate[1] != L':' || candidate[2] != L'\\' ||
                candidate.find(L':', 2) != std::wstring::npos || candidate.find(L'/') != std::wstring::npos)
            {
                return false;
            }
            const std::wstring root = candidate.substr(0, 3);
            if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED)
            {
                return false;
            }
            // 각 경로 성분을 잠근 채 검사한다. 마지막 파일만 검사하면 부모 junction
            // 교체 또는 rename으로 검증 후 다른 EXE를 실행할 수 있다.
            std::size_t end = 2;
            while (end < candidate.size())
            {
                const bool root_component = end == 2;
                if (!root_component)
                {
                    const auto begin = candidate.rfind(L'\\', end - 1) + 1;
                    const auto component = candidate.substr(begin, end - begin);
                    if (component.empty() || component == L"." || component == L".." ||
                        component.back() == L'.' || component.back() == L' ')
                    {
                        return false;
                    }
                }
                const bool is_directory = end < candidate.size();
                const auto part = root_component ? root : candidate.substr(0, end);
                handle opened(CreateFileW(part.c_str(), READ_CONTROL | FILE_READ_ATTRIBUTES |
                                         (is_directory ? 0 : GENERIC_READ),
                                         FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr));
                BY_HANDLE_FILE_INFORMATION information{};
                if (!opened || !GetFileInformationByHandle(opened.get(), &information) ||
                    (information.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 ||
                    ((information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) != is_directory)
                {
                    return false;
                }
                if (require_protected &&
                    !protected_acl(opened.get(), is_directory, is_directory && end == candidate.find_last_of(L'\\')))
                {
                    return false;
                }
                handles.push_back(std::move(opened));
                const auto next = candidate.find(L'\\', root_component ? 3 : end + 1);
                end = next == std::wstring::npos ? candidate.size() : next;
            }
            // 위 loop는 디렉터리만 연다. executable은 별도로 열어 최종 이름까지 대조한다.
            handle executable(CreateFileW(candidate.c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr,
                                          OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
            BY_HANDLE_FILE_INFORMATION information{};
            if (!executable || !GetFileInformationByHandle(executable.get(), &information) ||
                (information.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) != 0 ||
                information.nNumberOfLinks != 1 ||
                (require_protected && !protected_acl(executable.get(), false, false)))
            {
                return false;
            }
            std::wstring final_path(32768, L'\0');
            const DWORD length = GetFinalPathNameByHandleW(executable.get(), final_path.data(),
                                                           static_cast<DWORD>(final_path.size()), FILE_NAME_NORMALIZED);
            if (length == 0 || length >= final_path.size())
            {
                return false;
            }
            final_path.resize(length);
            if (!equal_path(final_path, L"\\\\?\\" + candidate))
            {
                return false;
            }
            handles.push_back(std::move(executable));
            path = candidate;
            return true;
        }
    };

    inline bool system_imports_only(HANDLE executable)
    {
        using rtl_get_version = LONG(WINAPI*)(OSVERSIONINFOW*);
        const auto version_function = reinterpret_cast<rtl_get_version>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
        OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (!version_function || version_function(&version) != 0 || version.dwMajorVersion < 10 ||
            (version.dwMajorVersion == 10 && version.dwBuildNumber < 14393))
        {
            return false;
        }
        LARGE_INTEGER length{};
        if (!GetFileSizeEx(executable, &length) || length.QuadPart < sizeof(IMAGE_DOS_HEADER) ||
            length.QuadPart > 64 * 1024 * 1024)
        {
            return false;
        }
        handle mapping(CreateFileMappingW(executable, nullptr, PAGE_READONLY, 0, 0, nullptr));
        if (!mapping)
        {
            return false;
        }
        const auto* bytes = static_cast<const std::byte*>(MapViewOfFile(mapping.get(), FILE_MAP_READ, 0, 0, 0));
        if (!bytes)
        {
            return false;
        }
        struct unmap
        {
            const void* address;
            ~unmap() { UnmapViewOfFile(address); }
        } release{bytes};
        const auto size = static_cast<std::size_t>(length.QuadPart);
        const auto read = [bytes, size]<class T>(std::size_t offset, T& output)
        {
            if (offset > size || sizeof(T) > size - offset)
            {
                return false;
            }
            std::memcpy(&output, bytes + offset, sizeof(T));
            return true;
        };
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS64 headers{};
        if (!read(0, dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
            !read(static_cast<std::size_t>(dos.e_lfanew), headers) || headers.Signature != IMAGE_NT_SIGNATURE ||
            headers.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
            headers.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64) ||
            headers.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            headers.OptionalHeader.NumberOfRvaAndSizes != IMAGE_NUMBEROF_DIRECTORY_ENTRIES ||
            headers.FileHeader.NumberOfSections == 0 || headers.FileHeader.NumberOfSections > 96 ||
            headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT].VirtualAddress != 0 ||
            headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR].VirtualAddress != 0)
        {
            return false;
        }
        std::array<IMAGE_SECTION_HEADER, 96> sections{};
        const auto section_offset = static_cast<std::size_t>(dos.e_lfanew) + sizeof(headers);
        for (std::size_t index = 0; index < headers.FileHeader.NumberOfSections; ++index)
        {
            if (!read(section_offset + index * sizeof(IMAGE_SECTION_HEADER), sections[index]))
            {
                return false;
            }
        }
        const auto offset_for = [&](DWORD rva, std::size_t amount) -> std::size_t
        {
            for (std::size_t index = 0; index < headers.FileHeader.NumberOfSections; ++index)
            {
                const auto& section = sections[index];
                if (rva >= section.VirtualAddress)
                {
                    const auto delta = static_cast<std::uint64_t>(rva) - section.VirtualAddress;
                    const auto offset = static_cast<std::uint64_t>(section.PointerToRawData) + delta;
                    if (delta <= section.SizeOfRawData && amount <= section.SizeOfRawData - delta &&
                        offset <= size && amount <= size - offset)
                    {
                        return static_cast<std::size_t>(offset);
                    }
                }
            }
            return size;
        };
        const auto imports = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        const auto load_config = headers.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_LOAD_CONFIG];
        constexpr auto flag_offset = offsetof(IMAGE_LOAD_CONFIG_DIRECTORY64, DependentLoadFlags);
        const auto config_offset = offset_for(load_config.VirtualAddress, flag_offset + sizeof(WORD));
        DWORD config_size = 0;
        WORD dependent_flags = 0;
        if (load_config.VirtualAddress == 0 || load_config.Size < flag_offset + sizeof(WORD) ||
            config_offset == size || !read(config_offset, config_size) ||
            config_size < flag_offset + sizeof(WORD) || !read(config_offset + flag_offset, dependent_flags) ||
            dependent_flags != LOAD_LIBRARY_SEARCH_SYSTEM32)
        {
            return false;
        }
        if (imports.VirtualAddress == 0 || imports.Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) || imports.Size > 65536)
        {
            return false;
        }
        const auto import_offset = offset_for(imports.VirtualAddress, imports.Size);
        if (import_offset == size)
        {
            return false;
        }
        for (std::size_t index = 0; index < imports.Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++index)
        {
            IMAGE_IMPORT_DESCRIPTOR descriptor{};
            if (!read(import_offset + index * sizeof(descriptor), descriptor))
            {
                return false;
            }
            if (descriptor.Name == 0)
            {
                return descriptor.FirstThunk == 0 && descriptor.OriginalFirstThunk == 0;
            }
            const auto name_offset = offset_for(descriptor.Name, 1);
            std::string name;
            for (std::size_t character = 0; character < 128; ++character)
            {
                char value = 0;
                if (!read(name_offset + character, value))
                {
                    return false;
                }
                if (value == 0)
                {
                    break;
                }
                if (value >= 'A' && value <= 'Z')
                {
                    value = static_cast<char>(value + ('a' - 'A'));
                }
                name.push_back(value);
            }
            // /MT helper만 승인한다. CRT/임의 DLL은 main 진입 전 로드되므로
            // SetDefaultDllDirectories만으로 안전하다고 판정할 수 없다.
            constexpr const char* system_names[]{"kernel32.dll", "kernelbase.dll", "ntdll.dll", "advapi32.dll",
                "sechost.dll", "bcrypt.dll", "bcryptprimitives.dll", "tdh.dll", "ole32.dll", "oleaut32.dll",
                "shell32.dll", "user32.dll", "gdi32.dll", "rpcrt4.dll", "shlwapi.dll", "version.dll"};
            bool allowed = false;
            for (const auto* candidate : system_names)
            {
                allowed = allowed || name == candidate;
            }
            if (!allowed)
            {
                return false;
            }
        }
        return false;
    }

    inline bool isolated_helper_directory(const std::wstring& path)
    {
        WIN32_FIND_DATAW entry{};
        const auto pattern = directory(path) + L"\\*";
        const HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry,
                                              FindExSearchNameMatch, nullptr, 0);
        if (search == INVALID_HANDLE_VALUE)
        {
            return false;
        }
        bool found = false;
        bool valid = true;
        do
        {
            const std::wstring name = entry.cFileName;
            if (name == L"." || name == L"..")
            {
                continue;
            }
            // 이름만 System DLL이어도 앱 폴더의 기존 파일이 먼저 로드될 수 있다.
            // elevated 배포 폴더는 EXE 하나만 허용하여 DLL/.local/외부 manifest를 막는다.
            if (!equal_path(name, L"CreatorDxCaptureHelper.exe") || found ||
                (entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0)
            {
                valid = false;
                break;
            }
            found = true;
        } while (FindNextFileW(search, &entry));
        const auto result = GetLastError();
        FindClose(search);
        return valid && found && result == ERROR_NO_MORE_FILES;
    }

    // 취소가 비정상적으로 늦더라도 UI 종료와 OVERLAPPED 버퍼 수명을 맞바꾸지 않는다.
    // timeout 한 번이면 채널을 폐기하므로 미완료 I/O 보존은 세션당 최대 한 번이다.
    struct operation
    {
        OVERLAPPED overlapped{};
        handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
        message packet{};
        operation() { overlapped.hEvent = event.get(); }
    };

    inline bool finish_operation(HANDLE pipe, std::unique_ptr<operation>& operation, DWORD timeout,
                                 HANDLE peer, HANDLE stop, DWORD& bytes)
    {
        HANDLE waits[3]{operation->event.get(), peer, stop};
        DWORD count = 1;
        if (peer)
        {
            waits[count++] = peer;
        }
        if (stop)
        {
            waits[count++] = stop;
        }
        const DWORD result = WaitForMultipleObjects(count, waits, FALSE, timeout);
        if (result == WAIT_OBJECT_0)
        {
            return GetOverlappedResult(pipe, &operation->overlapped, &bytes, FALSE) != FALSE;
        }
        const DWORD failure = result == WAIT_TIMEOUT ? ERROR_TIMEOUT : ERROR_OPERATION_ABORTED;
        CancelIoEx(pipe, &operation->overlapped);
        if (WaitForSingleObject(operation->event.get(), 100) != WAIT_OBJECT_0)
        {
            operation.release();
        }
        SetLastError(failure);
        return false;
    }

    inline bool transfer(HANDLE pipe, message& packet, bool writing, HANDLE peer = nullptr, HANDLE stop = nullptr)
    {
        auto pending = std::make_unique<operation>();
        if (!pending->event)
        {
            return false;
        }
        if (writing)
        {
            pending->packet = packet;
        }
        DWORD bytes = 0;
        const BOOL completed = writing ?
            WriteFile(pipe, &pending->packet, sizeof(message), &bytes, &pending->overlapped) :
            ReadFile(pipe, &pending->packet, sizeof(message), &bytes, &pending->overlapped);
        if (!completed && (GetLastError() != ERROR_IO_PENDING ||
                           !finish_operation(pipe, pending, io_timeout_ms, peer, stop, bytes)))
        {
            return false;
        }
        if (bytes != sizeof(message))
        {
            SetLastError(ERROR_INVALID_DATA);
            return false;
        }
        if (!writing)
        {
            packet = pending->packet;
        }
        return true;
    }

    inline bool message_available(HANDLE pipe, bool& available)
    {
        DWORD bytes = 0;
        DWORD current = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes, &current))
        {
            return false;
        }
        available = bytes != 0;
        if (available && current != sizeof(message))
        {
            SetLastError(ERROR_INVALID_DATA);
            return false;
        }
        return true;
    }
}
