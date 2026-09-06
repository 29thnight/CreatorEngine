// LC4 (PHASE 14.5) — endpoint 파일.
//
// ★ 이 파일도 Win32 API 를 쓴다(프로세스 생존 확인·파일 ACL). 소켓과 같은
//   이유로 플랫폼 의존은 `.cpp` 안에 가둔다 — 헤더는 중립이다(§4.1).

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <aclapi.h>

#pragma comment(lib, "Advapi32.lib")

#include "ServiceEndpointFile.h"
#include "SocketPlatform.h"
#include "JsonValue.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>
#include <sstream>

namespace CommandService
{
    namespace
    {
        const char kBase64Url[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

        std::string Base64Url(const unsigned char* data, std::size_t size)
        {
            std::string out;
            out.reserve(((size + 2) / 3) * 4);
            for (std::size_t i = 0; i < size; i += 3)
            {
                const unsigned b0 = data[i];
                const unsigned b1 = (i + 1 < size) ? data[i + 1] : 0u;
                const unsigned b2 = (i + 2 < size) ? data[i + 2] : 0u;

                out.push_back(kBase64Url[b0 >> 2]);
                out.push_back(kBase64Url[((b0 & 0x03) << 4) | (b1 >> 4)]);
                if (i + 1 < size) out.push_back(kBase64Url[((b1 & 0x0F) << 2) | (b2 >> 6)]);
                if (i + 2 < size) out.push_back(kBase64Url[b2 & 0x3F]);
            }
            // padding 을 붙이지 않는다. URL-safe 로 쓰이고, 길이는 고정이라
            // 되읽을 때 필요가 없다.
            return out;
        }

        bool IsProcessAlive(uint32_t pid)
        {
            if (0 == pid) return false;

            const HANDLE process = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (nullptr == process)
            {
                // 접근이 거부됐다면 그 pid 는 살아 있는 **다른 사용자의** 프로세스다.
                // 없어서 못 여는 것과 구분한다 — 구분하지 않으면 남의 프로세스가
                // 쓰는 포트를 우리 것으로 착각하고 덮어쓴다.
                return ERROR_ACCESS_DENIED == ::GetLastError();
            }

            DWORD exitCode = 0;
            const bool got = (FALSE != ::GetExitCodeProcess(process, &exitCode));
            ::CloseHandle(process);
            return got && (STILL_ACTIVE == exitCode);
        }

        /// 현재 사용자 SID 만 접근할 수 있는 DACL 을 만든다.
        ///
        /// ★ **처음 구현은 보안 극장이었다.**
        ///
        ///   `SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL)` 을 부르고 주석에
        ///   "상속을 끊고 소유자 권한만 남긴다"고 적어 뒀다. 그 함수는 읽기전용·
        ///   숨김 같은 **속성 비트**만 건드리고 ACL 은 손도 대지 않는다. 즉
        ///   endpoint.json 은 부모 디렉터리에서 상속한 권한을 그대로 가졌고,
        ///   그 안에 평문 토큰이 들어 있다 — 같은 머신의 다른 계정이 읽으면
        ///   실행 표면의 자물쇠가 통째로 넘어간다.
        ///
        ///   이제 프로세스 토큰의 SID 로 명시적 DACL 을 만들고 상속을 끊는다
        ///   (`PROTECTED_DACL_SECURITY_INFORMATION`).
        bool BuildOwnerOnlyDacl(std::vector<unsigned char>& outSelfRelative, std::string& outError)
        {
            HANDLE token = nullptr;
            if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token))
            {
                outError = "OpenProcessToken 실패";
                return false;
            }

            DWORD size = 0;
            ::GetTokenInformation(token, TokenUser, nullptr, 0, &size);
            std::vector<unsigned char> buffer(size);
            if (0 == size || !::GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
            {
                ::CloseHandle(token);
                outError = "GetTokenInformation(TokenUser) 실패";
                return false;
            }
            ::CloseHandle(token);

            const TOKEN_USER* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());

            EXPLICIT_ACCESSW access{};
            access.grfAccessPermissions = GENERIC_ALL;
            access.grfAccessMode        = SET_ACCESS;
            access.grfInheritance       = NO_INHERITANCE;
            access.Trustee.TrusteeForm  = TRUSTEE_IS_SID;
            access.Trustee.TrusteeType  = TRUSTEE_IS_USER;
            access.Trustee.ptstrName    = static_cast<LPWSTR>(user->User.Sid);

            PACL acl = nullptr;
            if (ERROR_SUCCESS != ::SetEntriesInAclW(1, &access, nullptr, &acl))
            {
                outError = "SetEntriesInAclW 실패";
                return false;
            }

            SECURITY_DESCRIPTOR absolute{};
            if (!::InitializeSecurityDescriptor(&absolute, SECURITY_DESCRIPTOR_REVISION)
                || !::SetSecurityDescriptorDacl(&absolute, TRUE, acl, FALSE)
                // 상속을 끊는다. 이것이 없으면 부모 디렉터리의 권한이 다시 붙는다.
                || !::SetSecurityDescriptorControl(&absolute,
                        SE_DACL_PROTECTED, SE_DACL_PROTECTED))
            {
                ::LocalFree(acl);
                outError = "보안 서술자 구성 실패";
                return false;
            }

            DWORD relativeSize = 0;
            ::MakeSelfRelativeSD(&absolute, nullptr, &relativeSize);
            outSelfRelative.assign(relativeSize, 0);
            if (0 == relativeSize
                || !::MakeSelfRelativeSD(&absolute, outSelfRelative.data(), &relativeSize))
            {
                ::LocalFree(acl);
                outError = "MakeSelfRelativeSD 실패";
                return false;
            }

            ::LocalFree(acl);
            return true;
        }
    }

    std::string GenerateToken()
    {
        unsigned char bytes[32] = {};
        if (!FillRandomBytes(bytes, sizeof(bytes))) return {};
        return Base64Url(bytes, sizeof(bytes));
    }

    uint32_t CurrentProcessId() noexcept
    {
        return static_cast<uint32_t>(::GetCurrentProcessId());
    }

    std::string UtcTimestamp()
    {
        const std::time_t now = std::time(nullptr);
        std::tm utc{};
        ::gmtime_s(&utc, &now);
        char buffer[32] = {};
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
        return buffer;
    }

    bool WriteEndpointFile(const std::string& path, const EndpointInfo& info, std::string& outError)
    {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);

        JsonValue root = JsonValue::Object();
        root.Set("schemaVersion", JsonValue::Int(info.schemaVersion));
        root.Set("pid",           JsonValue::Int(static_cast<int64_t>(info.pid)));
        root.Set("port",          JsonValue::Int(static_cast<int64_t>(info.port)));
        root.Set("token",         JsonValue::String(info.token));
        root.Set("host",          JsonValue::String(info.host));
        root.Set("project",       JsonValue::String(info.project));
        root.Set("role",          JsonValue::String(info.role));
        root.Set("startedUtc",    JsonValue::String(info.startedUtc));

        const std::string text = root.Serialize();

        // ★ 권한을 **먼저** 세우고 그 핸들로 쓴다.
        //
        //   쓴 뒤에 권한을 고치면 그 사이에 토큰이 노출된 파일이 존재한다.
        //   `CreateFileW` 에 보안 서술자를 주면 그 창이 없다.
        std::vector<unsigned char> descriptor;
        if (!BuildOwnerOnlyDacl(descriptor, outError))
        {
            // ★ 권한을 못 세우면 **쓰지 않는다.**
            //
            //   토큰은 이 실행 표면의 유일한 자물쇠다. 보호할 수 없는데 적어 두는
            //   것보다 서비스를 안 여는 편이 낫다 — 호출자가 이 실패를 보고 뜨지
            //   않는다(Service::Start).
            outError = "endpoint 파일 권한을 세울 수 없다: " + outError;
            return false;
        }

        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength              = sizeof(attributes);
        attributes.bInheritHandle       = FALSE;
        attributes.lpSecurityDescriptor = descriptor.data();

        const std::wstring wide(path.begin(), path.end());
        const HANDLE handle = ::CreateFileW(wide.c_str(), GENERIC_WRITE, 0, &attributes,
                                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (INVALID_HANDLE_VALUE == handle)
        {
            outError = "endpoint 파일을 열 수 없다: " + path;
            return false;
        }

        DWORD wrote = 0;
        const bool ok = (FALSE != ::WriteFile(handle, text.data(),
                                              static_cast<DWORD>(text.size()), &wrote, nullptr))
                        && (wrote == text.size());
        ::CloseHandle(handle);

        if (!ok)
        {
            outError = "endpoint 파일을 쓰지 못했다: " + path;
            std::error_code removeEc;
            std::filesystem::remove(path, removeEc);
            return false;
        }

        // 이미 있던 파일에 CREATE_ALWAYS 로 덮어쓴 경우 기존 DACL 이 유지되므로,
        // 상속 차단을 한 번 더 명시한다.
        ::SetNamedSecurityInfoW(const_cast<LPWSTR>(wide.c_str()), SE_FILE_OBJECT,
                                DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                nullptr, nullptr,
                                [&descriptor]() -> PACL
                                {
                                    PACL acl = nullptr;
                                    BOOL present = FALSE;
                                    BOOL defaulted = FALSE;
                                    ::GetSecurityDescriptorDacl(
                                        reinterpret_cast<PSECURITY_DESCRIPTOR>(descriptor.data()),
                                        &present, &acl, &defaulted);
                                    return present ? acl : nullptr;
                                }(),
                                nullptr);
        return true;
    }

    bool ReclaimStaleEndpointFile(const std::string& path, bool& outAliveOwner)
    {
        outAliveOwner = false;

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) return true;

        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            // 읽을 수 없는 파일은 판단할 수 없다. 지우지 않는다 — 남의 것일 수 있다.
            outAliveOwner = true;
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        file.close();

        const JsonParseResult parsed = ParseJson(buffer.str());
        uint32_t pid = 0;
        if (parsed.ok)
        {
            if (const JsonValue* value = parsed.value.Find("pid"))
            {
                if (JsonValue::Kind::Int == value->GetKind())
                {
                    pid = static_cast<uint32_t>(value->AsInt());
                }
            }
        }

        if (IsProcessAlive(pid))
        {
            // 같은 프로젝트에 서비스가 이미 떠 있다. 덮어쓰면 그쪽 클라이언트가
            // 우리에게 오게 된다.
            outAliveOwner = true;
            return false;
        }

        // 주인이 죽었다. 회수한다.
        std::filesystem::remove(path, ec);
        return true;
    }

    void RemoveEndpointFile(const std::string& path) noexcept
    {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
}
