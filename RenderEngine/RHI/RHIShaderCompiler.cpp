#ifndef DYNAMICCPP_EXPORTS
#include "RHIShaderCompiler.h"

#include "RHIShaderSource.h"
#include "Vulkan/VulkanBindingModel.h"
#include "../../Utility_Framework/PathFinder.h"

#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using Microsoft::WRL::ComPtr;

    constexpr std::uint32_t kCacheMagic = 0x43534852u; // RHSC
    constexpr std::uint32_t kCacheSchema = 4u;
    constexpr std::uint64_t kMaxCachedShaderBytes = 64ull * 1024ull * 1024ull;

    struct Hash128
    {
        std::uint64_t lo{ 1469598103934665603ull };
        std::uint64_t hi{ 1099511628211ull ^ 0x9e3779b97f4a7c15ull };

        void Add(const void* data, std::size_t size)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                lo = (lo ^ bytes[i]) * 1099511628211ull;
                hi = (hi ^ static_cast<std::uint8_t>(bytes[i] + 0x9du))
                    * 14029467366897019727ull;
            }
        }

        void Add(std::string_view value)
        {
            const std::uint64_t size = static_cast<std::uint64_t>(value.size());
            Add(&size, sizeof(size));
            Add(value.data(), value.size());
        }

        std::string Hex() const
        {
            std::ostringstream stream;
            stream << std::hex << std::setfill('0')
                << std::setw(16) << lo << std::setw(16) << hi;
            return stream.str();
        }
    };

    struct CacheHeader
    {
        std::uint32_t magic{ kCacheMagic };
        std::uint32_t schema{ kCacheSchema };
        std::uint64_t byteCount{};
        std::uint64_t contentLo{};
        std::uint64_t contentHi{};
    };

    struct SourceUnit
    {
        std::filesystem::path path;
        std::string text;
    };

    thread_local RHIShaderBinary g_output = RHIShaderBinary::Dxil;
    std::mutex g_cacheMutex;
    std::unordered_map<std::string, std::vector<std::uint8_t>> g_memoryCache;
    std::atomic<std::uint64_t> g_memoryHits{};
    std::atomic<std::uint64_t> g_diskHits{};
    std::atomic<std::uint64_t> g_compiles{};
    std::atomic<std::uint64_t> g_failures{};

    HMODULE g_dxcModule{};
    DxcCreateInstanceProc g_dxcCreate{};
    std::string g_dxcIdentity;
    std::once_flag g_dxcOnce;
    std::string g_dxcLoadError;

    std::string NarrowUtf8(std::wstring_view value)
    {
        if (value.empty()) return {};
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (bytes <= 0) return {};
        std::string result(static_cast<std::size_t>(bytes), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()),
            result.data(), bytes, nullptr, nullptr);
        return result;
    }

    std::wstring WidenAscii(std::string_view value)
    {
        return std::wstring(value.begin(), value.end());
    }

    bool TryLoadDxcPath(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return false;
        g_dxcModule = LoadLibraryW(path.c_str());
        return nullptr != g_dxcModule;
    }

    void LoadDxcOnce()
    {
        // Packaged/runtime-only hosts are closed over the executable-adjacent DXC.
        // Only authoring hosts may fall back to repository/SDK discovery.
        TryLoadDxcPath(PathFinder::RelativeToExecutable("dxcompiler.dll"));

        if (nullptr == g_dxcModule && !PathFinder::IsAssetAuthoringEnabled())
        {
            g_dxcLoadError = "실행 파일 옆 dxcompiler.dll을 로드하지 못했다";
            return;
        }

        if (nullptr == g_dxcModule)
        {
            const std::filesystem::path repository =
                PathFinder::BaseProjectPath().parent_path();
            TryLoadDxcPath(repository / "ThirdParty" / "DXC" / "bin" / "x64"
                / "dxcompiler.dll");
        }

        if (nullptr == g_dxcModule)
        {
            std::array<wchar_t, 32768> sdk{};
            DWORD length = GetEnvironmentVariableW(L"VULKAN_SDK", sdk.data(),
                static_cast<DWORD>(sdk.size()));
            if (0 == length || length >= sdk.size())
            {
                DWORD bytes = static_cast<DWORD>(sdk.size() * sizeof(wchar_t));
                if (ERROR_SUCCESS != RegGetValueW(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
                    L"VULKAN_SDK", RRF_RT_REG_SZ, nullptr, sdk.data(), &bytes))
                {
                    sdk[0] = L'\0';
                }
            }
            if (L'\0' != sdk[0])
            {
                TryLoadDxcPath(std::filesystem::path(sdk.data()) / "Bin"
                    / "dxcompiler.dll");
            }
        }

        if (nullptr == g_dxcModule) g_dxcModule = LoadLibraryW(L"dxcompiler.dll");
        if (nullptr == g_dxcModule)
        {
            g_dxcLoadError = "dxcompiler.dll을 찾지 못했다";
            return;
        }

        g_dxcCreate = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(g_dxcModule, "DxcCreateInstance"));
        if (nullptr == g_dxcCreate)
        {
            g_dxcLoadError = "DxcCreateInstance 진입점이 없다";
            return;
        }

        std::array<wchar_t, 32768> modulePath{};
        const DWORD pathLength = GetModuleFileNameW(g_dxcModule, modulePath.data(),
            static_cast<DWORD>(modulePath.size()));
        if (0 != pathLength && pathLength < modulePath.size())
        {
            const std::filesystem::path path(modulePath.data());
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(path, ec);
            g_dxcIdentity = NarrowUtf8(path.generic_wstring()) + ":"
                + std::to_string(ec ? 0 : bytes);
            const auto stamp = std::filesystem::last_write_time(path, ec);
            if (!ec) g_dxcIdentity += ":" + std::to_string(stamp.time_since_epoch().count());
        }
        if (g_dxcIdentity.empty()) g_dxcIdentity = "dxcompiler:unknown";
    }

    bool EnsureDxc(std::string& outError)
    {
        std::call_once(g_dxcOnce, LoadDxcOnce);
        if (nullptr != g_dxcCreate) return true;
        outError = g_dxcLoadError.empty() ? "DXC 초기화 실패" : g_dxcLoadError;
        return false;
    }

    bool ReadSourceFile(const std::filesystem::path& path, std::string& outText,
        std::string& outError)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            outError = "셰이더 소스를 열 수 없다: " + path.string();
            return false;
        }
        std::ostringstream buffer;
        buffer << file.rdbuf();
        outText = buffer.str();
        if (!outText.empty()) return true;
        outError = "셰이더 소스가 비었다: " + path.string();
        return false;
    }

    bool CollectSourceGraph(const std::filesystem::path& path,
        std::unordered_set<std::wstring>& visited, std::vector<SourceUnit>& outUnits,
        std::string& outError)
    {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        const std::filesystem::path resolved = ec ? path.lexically_normal() : canonical;
        const std::wstring key = resolved.native();
        if (!visited.emplace(key).second) return true;

        SourceUnit unit{};
        unit.path = resolved;
        if (!ReadSourceFile(resolved, unit.text, outError)) return false;
        outUnits.push_back(unit);

        // 조건부 include도 모두 해시에 넣는다. 실제 퍼뮤테이션보다 넓게
        // 무효화할 수는 있어도 include 변경을 놓치지는 않는다.
        static const std::regex includePattern(
            R"(^\s*#\s*include\s*\"([^\"]+)\")", std::regex::ECMAScript);
        std::istringstream lines(unit.text);
        std::string line;
        while (std::getline(lines, line))
        {
            std::smatch match;
            if (!std::regex_search(line, match, includePattern)) continue;
            const std::filesystem::path includePath =
                resolved.parent_path() / match[1].str();
            if (!CollectSourceGraph(includePath, visited, outUnits, outError)) return false;
        }
        return true;
    }

    std::wstring MapTarget(std::string_view profile, std::string& outError)
    {
        const std::size_t separator = profile.find('_');
        if (std::string_view::npos == separator || 0 == separator)
        {
            outError = "셰이더 타깃 프로필이 잘못됐다: " + std::string(profile);
            return {};
        }
        return WidenAscii(profile.substr(0, separator)) + L"_6_0";
    }

    std::filesystem::path CacheDirectory()
    {
        std::array<wchar_t, 32768> local{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local.data(),
            static_cast<DWORD>(local.size()));
        if (0 != length && length < local.size())
        {
            return std::filesystem::path(local.data()) / "CreatorEngine"
                / "ShaderCache" / "v1";
        }
        std::error_code ec;
        const auto temp = std::filesystem::temp_directory_path(ec);
        return (ec ? PathFinder::RelativeToExecutable("ShaderCache") : temp / "CreatorEngine")
            / "ShaderCache" / "v1";
    }

    Hash128 HashBytes(const void* data, std::size_t size)
    {
        Hash128 hash;
        hash.Add(data, size);
        return hash;
    }

    bool ReadCache(const std::string& key, RHIShaderBlob& outBlob)
    {
        {
            std::lock_guard<std::mutex> guard(g_cacheMutex);
            const auto found = g_memoryCache.find(key);
            if (found != g_memoryCache.end())
            {
                outBlob.Assign(found->second.data(), found->second.size());
                ++g_memoryHits;
                return true;
            }
        }

        const std::filesystem::path path = CacheDirectory() / (key + ".rsh");
        std::ifstream file(path, std::ios::binary);
        CacheHeader header{};
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header))) return false;
        if (kCacheMagic != header.magic || kCacheSchema != header.schema
            || 0 == header.byteCount || header.byteCount > kMaxCachedShaderBytes)
        {
            return false;
        }

        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(header.byteCount));
        if (!file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()))) return false;
        const Hash128 content = HashBytes(bytes.data(), bytes.size());
        if (content.lo != header.contentLo || content.hi != header.contentHi) return false;

        outBlob.Assign(bytes.data(), bytes.size());
        {
            std::lock_guard<std::mutex> guard(g_cacheMutex);
            g_memoryCache.emplace(key, std::move(bytes));
        }
        ++g_diskHits;
        return true;
    }

    void WriteCache(const std::string& key, const RHIShaderBlob& blob)
    {
        if (!blob.IsValid()) return;
        const std::filesystem::path directory = CacheDirectory();
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) return;

        const std::filesystem::path path = directory / (key + ".rsh");
        if (std::filesystem::is_regular_file(path, ec) && !ec) return;

        const std::filesystem::path temp = directory /
            (key + "." + std::to_string(GetCurrentProcessId()) + "."
                + std::to_string(GetCurrentThreadId()) + ".tmp");
        const Hash128 content = HashBytes(blob.Data(), blob.Size());
        CacheHeader header{};
        header.byteCount = static_cast<std::uint64_t>(blob.Size());
        header.contentLo = content.lo;
        header.contentHi = content.hi;

        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (!file) return;
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            file.write(static_cast<const char*>(blob.Data()),
                static_cast<std::streamsize>(blob.Size()));
            if (!file) return;
        }

        std::filesystem::rename(temp, path, ec);
        if (ec)
        {
            // 다른 스레드/프로세스가 같은 키를 먼저 썼다면 그 파일이 정답이다.
            std::filesystem::remove(temp, ec);
        }
    }

    std::string BuildCacheKey(const RHIShaderCompileRequest& request,
        const std::vector<SourceUnit>& units)
    {
        Hash128 hash;
        hash.Add("CreatorEngine.RHIShaderCompiler.v4.O3.options");
        hash.Add(g_dxcIdentity);
        hash.Add(request.name);
        hash.Add(request.entryPoint);
        hash.Add(request.targetProfile);
        hash.Add(&request.output, sizeof(request.output));
        hash.Add(&request.options, sizeof(request.options));
        for (const SourceUnit& unit : units)
        {
            hash.Add(NarrowUtf8(unit.path.generic_wstring()));
            hash.Add(unit.text);
        }
        if (nullptr != request.defines)
        {
            for (const RHIShaderDefine* define = request.defines;
                nullptr != define->Name; ++define)
            {
                hash.Add(define->Name);
                hash.Add(nullptr == define->Definition ? "" : define->Definition);
            }
        }
        return hash.Hex();
    }

    class DxcShaderCompiler final : public IRHIShaderCompiler
    {
    public:
        bool Compile(const RHIShaderCompileRequest& request,
            RHIShaderBlob& outBlob, std::string& outError) override
        {
            if (!EnsureDxc(outError)) return false;

            const std::filesystem::path sourcePath = RHIShaderSource::Resolve(request.name);
            std::vector<SourceUnit> units;
            std::unordered_set<std::wstring> visited;
            if (!CollectSourceGraph(sourcePath, visited, units, outError)) return false;

            const std::string cacheKey = BuildCacheKey(request, units);
            if (ReadCache(cacheKey, outBlob)) return true;

            const std::wstring target = MapTarget(request.targetProfile, outError);
            if (target.empty()) return false;

            ComPtr<IDxcUtils> utils;
            ComPtr<IDxcCompiler3> compiler;
            if (FAILED(g_dxcCreate(CLSID_DxcUtils, IID_PPV_ARGS(&utils)))
                || FAILED(g_dxcCreate(CLSID_DxcCompiler, IID_PPV_ARGS(&compiler))))
            {
                outError = "DXC 인스턴스 생성 실패";
                return false;
            }

            ComPtr<IDxcIncludeHandler> includes;
            if (FAILED(utils->CreateDefaultIncludeHandler(&includes)))
            {
                outError = "DXC include handler 생성 실패";
                return false;
            }

            const std::wstring sourceName = sourcePath.wstring();
            const std::wstring includeDir = sourcePath.parent_path().wstring();
            const std::wstring entry = WidenAscii(request.entryPoint);
            std::vector<std::wstring> ownedArguments;
            ownedArguments.reserve(32);
            ownedArguments.push_back(sourceName);
            ownedArguments.push_back(L"-E");
            ownedArguments.push_back(entry);
            ownedArguments.push_back(L"-T");
            ownedArguments.push_back(target);
            ownedArguments.push_back(L"-I");
            ownedArguments.push_back(includeDir);
            ownedArguments.push_back(L"-O3");
            if (request.options.strictMath) ownedArguments.push_back(L"-Gis");

            if (RHIShaderBinary::SpirV == request.output)
            {
                ownedArguments.push_back(L"-spirv");
                ownedArguments.push_back(L"-fspv-target-env=vulkan1.3");
                const std::array<std::pair<const wchar_t*, std::uint32_t>, 4> shifts = {{
                    { L"-fvk-b-shift", VulkanBindingModel::kConstantBufferShift },
                    { L"-fvk-t-shift", VulkanBindingModel::kShaderResourceShift },
                    { L"-fvk-u-shift", VulkanBindingModel::kUnorderedAccessShift },
                    { L"-fvk-s-shift", VulkanBindingModel::kSamplerShift },
                }};
                for (const auto& [argument, value] : shifts)
                {
                    ownedArguments.push_back(argument);
                    ownedArguments.push_back(std::to_wstring(value));
                    ownedArguments.push_back(L"0");
                }
            }

            if (nullptr != request.defines)
            {
                for (const RHIShaderDefine* define = request.defines;
                    nullptr != define->Name; ++define)
                {
                    std::wstring value = WidenAscii(define->Name);
                    if (nullptr != define->Definition && '\0' != define->Definition[0])
                    {
                        value += L"=" + WidenAscii(define->Definition);
                    }
                    ownedArguments.push_back(L"-D");
                    ownedArguments.push_back(std::move(value));
                }
            }

            std::vector<const wchar_t*> arguments;
            arguments.reserve(ownedArguments.size());
            for (const std::wstring& argument : ownedArguments)
                arguments.push_back(argument.c_str());

            DxcBuffer source{};
            source.Ptr = units.front().text.data();
            source.Size = units.front().text.size();
            source.Encoding = DXC_CP_UTF8;

            ComPtr<IDxcResult> result;
            const HRESULT invoked = compiler->Compile(&source, arguments.data(),
                static_cast<UINT32>(arguments.size()), includes.Get(),
                IID_PPV_ARGS(&result));

            std::string diagnostics;
            if (nullptr != result)
            {
                ComPtr<IDxcBlobUtf8> errors;
                if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS,
                    IID_PPV_ARGS(&errors), nullptr)) && nullptr != errors
                    && 0 != errors->GetStringLength())
                {
                    diagnostics = errors->GetStringPointer();
                }
            }

            HRESULT status = invoked;
            if (SUCCEEDED(invoked) && nullptr != result) result->GetStatus(&status);
            if (FAILED(status) || nullptr == result)
            {
                ++g_failures;
                outError = std::string(request.name) + " ("
                    + std::string(request.entryPoint) + "/"
                    + (RHIShaderBinary::Dxil == request.output ? "DXIL" : "SPIR-V")
                    + ") 컴파일 실패: "
                    + (diagnostics.empty() ? "원인 미상" : diagnostics);
                return false;
            }

            ComPtr<IDxcBlob> object;
            if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object), nullptr))
                || nullptr == object || 0 == object->GetBufferSize())
            {
                ++g_failures;
                outError = std::string(request.name) + " — 셰이더 산출물이 비었다";
                return false;
            }

            outBlob.Assign(object->GetBufferPointer(), object->GetBufferSize());
            ++g_compiles;
            {
                std::vector<std::uint8_t> bytes(outBlob.Size());
                std::memcpy(bytes.data(), outBlob.Data(), outBlob.Size());
                std::lock_guard<std::mutex> guard(g_cacheMutex);
                g_memoryCache.emplace(cacheKey, std::move(bytes));
            }
            WriteCache(cacheKey, outBlob);
            return true;
        }
    };

    IRHIShaderCompiler& Compiler()
    {
        static DxcShaderCompiler compiler;
        return compiler;
    }
}

RHIShaderBinary RHIShaderCompiler::GetOutput()
{
    return g_output;
}

void RHIShaderCompiler::SetOutput(RHIShaderBinary output)
{
    g_output = output;
}

RHIShaderCompiler::ScopedOutput::ScopedOutput(RHIShaderBinary output)
    : m_previous(GetOutput())
{
    SetOutput(output);
}

RHIShaderCompiler::ScopedOutput::~ScopedOutput()
{
    SetOutput(m_previous);
}

bool RHIShaderCompiler::CompileFile(std::string_view name, std::string_view entryPoint,
    std::string_view targetProfile, const RHIShaderDefine* defines,
    RHIShaderBlob& outBlob, std::string& outError, RHIShaderCompileOptions options)
{
    const RHIShaderCompileRequest request{
        name, entryPoint, targetProfile, GetOutput(), defines, options
    };
    return Compiler().Compile(request, outBlob, outError);
}

RHIShaderCompiler::Stats RHIShaderCompiler::GetStats()
{
    return { g_memoryHits.load(), g_diskHits.load(), g_compiles.load(),
        g_failures.load() };
}

void RHIShaderCompiler::ResetStats()
{
    g_memoryHits.store(0);
    g_diskHits.store(0);
    g_compiles.store(0);
    g_failures.store(0);
}

void RHIShaderCompiler::ClearMemoryCache()
{
    std::lock_guard<std::mutex> guard(g_cacheMutex);
    g_memoryCache.clear();
}

#endif // !DYNAMICCPP_EXPORTS
