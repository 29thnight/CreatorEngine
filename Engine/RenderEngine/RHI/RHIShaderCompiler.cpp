#include "RHIShaderCompiler.h"

#include "RHIShaderSource.h"
#include "Vulkan/VulkanBindingModel.h"
#include "../../Utility_Framework/PathFinder.h"

#include <Windows.h>
#include <slang.h>
#include <slang-com-ptr.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace
{
    constexpr std::uint32_t kCacheMagic = 0x43534852u; // RHSC
    constexpr std::uint32_t kCacheSchema = 10u;
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

    using CreateSlangGlobalSessionProc = SlangResult (*)(
        const SlangGlobalSessionDesc*, slang::IGlobalSession**);

    struct SlangReflectionApi final
    {
        decltype(&spReflectionType_GetSpecializedElementCount) typeElementCount{};
        decltype(&spReflectionType_GetRowCount) typeRowCount{};
        decltype(&spReflectionType_GetColumnCount) typeColumnCount{};
        decltype(&spReflectionType_GetScalarType) typeScalarType{};
        decltype(&spReflectionType_GetResourceShape) typeResourceShape{};
        decltype(&spReflectionType_GetResourceAccess) typeResourceAccess{};
        decltype(&spReflectionType_GetName) typeName{};
        decltype(&spReflectionTypeLayout_GetType) layoutType{};
        decltype(&spReflectionTypeLayout_getKind) layoutKind{};
        decltype(&spReflectionTypeLayout_GetSize) layoutSize{};
        decltype(&spReflectionTypeLayout_GetFieldCount) layoutFieldCount{};
        decltype(&spReflectionTypeLayout_GetFieldByIndex) layoutField{};
        decltype(&spReflectionTypeLayout_GetElementTypeLayout) layoutElement{};
        decltype(&spReflectionVariable_GetName) variableName{};
        decltype(&spReflectionVariableLayout_GetVariable) variableLayoutVariable{};
        decltype(&spReflectionVariableLayout_GetTypeLayout) variableLayoutType{};
        decltype(&spReflectionVariableLayout_GetOffset) variableLayoutOffset{};
        decltype(&spReflectionParameter_GetBindingIndex) bindingIndex{};
        decltype(&spReflectionParameter_GetBindingSpace) bindingSpace{};
        decltype(&spReflection_GetParameterCount) parameterCount{};
        decltype(&spReflection_GetParameterByIndex) parameter{};
    };

    struct SlangRuntime final
    {
        HMODULE module{};
        HMODULE dxcModule{};
        HMODULE dxilModule{};
        CreateSlangGlobalSessionProc createGlobalSession{};
        SlangReflectionApi reflection;
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        std::filesystem::path modulePath;
        std::string identity;
        std::string loadError;
        std::once_flag loadOnce;
        std::mutex compileMutex;

        ~SlangRuntime()
        {
            // Slang COM 객체의 vtable은 DLL에 있으므로 모든 인터페이스를 먼저
            // 해제한 뒤 모듈을 내린다.
            globalSession.setNull();
            if (nullptr != module) FreeLibrary(module);
            if (nullptr != dxcModule) FreeLibrary(dxcModule);
            if (nullptr != dxilModule) FreeLibrary(dxilModule);
        }
    };

    SlangRuntime& GetSlangRuntime()
    {
        static SlangRuntime runtime;
        return runtime;
    }

    template <typename Proc>
    bool LoadSlangProc(HMODULE module, const char* name, Proc& outProc)
    {
        outProc = reinterpret_cast<Proc>(GetProcAddress(module, name));
        return nullptr != outProc;
    }

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

    std::wstring WidenUtf8(std::string_view value)
    {
        if (value.empty()) return {};
        const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (characters <= 0) return {};
        std::wstring result(static_cast<std::size_t>(characters), L'\0');
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), characters);
        return result;
    }

    std::string PathUtf8(const std::filesystem::path& path)
    {
        return NarrowUtf8(path.generic_wstring());
    }

    // 확장자 하나가 front-end를 고른다. 대소문자를 접는 이유는 자산 트리의
    // 이름이 손으로 적히기 때문이다 — ".Slang"이 조용히 hlsl로 컴파일되면
    // `import` 한 줄에서 파스 에러가 나고 원인이 확장자로 보이지 않는다.
    [[nodiscard]] bool IsSlangSource(const std::filesystem::path& path)
    {
        std::string extension = PathUtf8(path.extension());
        std::ranges::transform(extension, extension.begin(),
            [](unsigned char character)
            {
                return static_cast<char>(std::tolower(character));
            });
        return ".slang" == extension;
    }

    bool AddFileHash(const std::filesystem::path& path, Hash128& hash)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file) return false;
        std::array<char, 64 * 1024> bytes{};
        while (file)
        {
            file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            const std::streamsize read = file.gcount();
            if (read > 0) hash.Add(bytes.data(), static_cast<std::size_t>(read));
        }
        return file.eof();
    }

    bool TryLoadSlangPath(SlangRuntime& runtime, const std::filesystem::path& path)
    {
        std::error_code ec;
        if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return false;
        runtime.module = LoadLibraryW(path.c_str());
        if (nullptr == runtime.module) return false;
        runtime.modulePath = std::filesystem::weakly_canonical(path, ec);
        if (ec) runtime.modulePath = path.lexically_normal();
        return true;
    }

    void LoadSlangOnce(SlangRuntime& runtime)
    {
        // 배포 호스트는 실행 파일 옆의 고정 번들만 허용한다. 에디터/테스트 같은
        // authoring 호스트만 저장소의 동일 번들을 직접 참조할 수 있다.
        TryLoadSlangPath(runtime,
            PathFinder::RelativeToExecutable("slang-compiler.dll"));

        if (nullptr == runtime.module && !PathFinder::IsAssetAuthoringEnabled())
        {
            runtime.loadError = "실행 파일 옆 slang-compiler.dll을 로드하지 못했다";
            return;
        }

        if (nullptr == runtime.module)
        {
            const std::filesystem::path repository =
                PathFinder::BaseProjectPath().parent_path();
            TryLoadSlangPath(runtime, repository / "ThirdParty" / "Slang" / "bin"
                / "slang-compiler.dll");
        }

        if (nullptr == runtime.module)
        {
            runtime.loadError = "고정 Slang 번들의 slang-compiler.dll을 찾지 못했다";
            return;
        }

        const std::filesystem::path compilerDirectory = runtime.modulePath.parent_path();
        const std::filesystem::path dxilPath = compilerDirectory / "dxil.dll";
        const std::filesystem::path dxcPath = compilerDirectory / "dxcompiler.dll";
        runtime.dxilModule = LoadLibraryW(dxilPath.c_str());
        if (nullptr == runtime.dxilModule)
        {
            runtime.loadError = "고정 Slang 번들의 dxil.dll을 로드하지 못했다";
            return;
        }
        runtime.dxcModule = LoadLibraryW(dxcPath.c_str());
        if (nullptr == runtime.dxcModule)
        {
            runtime.loadError = "고정 Slang 번들의 dxcompiler.dll을 로드하지 못했다";
            return;
        }

        runtime.createGlobalSession = reinterpret_cast<CreateSlangGlobalSessionProc>(
            GetProcAddress(runtime.module, "slang_createGlobalSession2"));
        if (nullptr == runtime.createGlobalSession)
        {
            runtime.loadError = "slang_createGlobalSession2 진입점이 없다";
            return;
        }

        SlangReflectionApi& reflection = runtime.reflection;
        const bool reflectionLoaded =
            LoadSlangProc(runtime.module, "spReflectionType_GetSpecializedElementCount",
                reflection.typeElementCount)
            && LoadSlangProc(runtime.module, "spReflectionType_GetRowCount",
                reflection.typeRowCount)
            && LoadSlangProc(runtime.module, "spReflectionType_GetColumnCount",
                reflection.typeColumnCount)
            && LoadSlangProc(runtime.module, "spReflectionType_GetScalarType",
                reflection.typeScalarType)
            && LoadSlangProc(runtime.module, "spReflectionType_GetResourceShape",
                reflection.typeResourceShape)
            && LoadSlangProc(runtime.module, "spReflectionType_GetResourceAccess",
                reflection.typeResourceAccess)
            && LoadSlangProc(runtime.module, "spReflectionType_GetName",
                reflection.typeName)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_GetType",
                reflection.layoutType)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_getKind",
                reflection.layoutKind)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_GetSize",
                reflection.layoutSize)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_GetFieldCount",
                reflection.layoutFieldCount)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_GetFieldByIndex",
                reflection.layoutField)
            && LoadSlangProc(runtime.module, "spReflectionTypeLayout_GetElementTypeLayout",
                reflection.layoutElement)
            && LoadSlangProc(runtime.module, "spReflectionVariable_GetName",
                reflection.variableName)
            && LoadSlangProc(runtime.module, "spReflectionVariableLayout_GetVariable",
                reflection.variableLayoutVariable)
            && LoadSlangProc(runtime.module, "spReflectionVariableLayout_GetTypeLayout",
                reflection.variableLayoutType)
            && LoadSlangProc(runtime.module, "spReflectionVariableLayout_GetOffset",
                reflection.variableLayoutOffset)
            && LoadSlangProc(runtime.module, "spReflectionParameter_GetBindingIndex",
                reflection.bindingIndex)
            && LoadSlangProc(runtime.module, "spReflectionParameter_GetBindingSpace",
                reflection.bindingSpace)
            && LoadSlangProc(runtime.module, "spReflection_GetParameterCount",
                reflection.parameterCount)
            && LoadSlangProc(runtime.module, "spReflection_GetParameterByIndex",
                reflection.parameter);
        if (!reflectionLoaded)
        {
            runtime.loadError = "Slang reflection C API 진입점이 없다";
            return;
        }

        SlangGlobalSessionDesc globalDesc{};
        if (SLANG_FAILED(runtime.createGlobalSession(
            &globalDesc, runtime.globalSession.writeRef())))
        {
            runtime.loadError = "Slang global session 생성 실패";
            return;
        }

        Hash128 slangHash;
        Hash128 dxcHash;
        Hash128 dxilHash;
        if (!AddFileHash(runtime.modulePath, slangHash)
            || !AddFileHash(dxcPath, dxcHash)
            || !AddFileHash(dxilPath, dxilHash))
        {
            runtime.loadError = "Slang/DXC 번들 콘텐츠 identity 계산 실패";
            return;
        }
        const char* buildTag = runtime.globalSession->getBuildTagString();
        runtime.identity = "slang:" + std::string(nullptr == buildTag ? "unknown" : buildTag)
            + ":" + slangHash.Hex() + ":dxc:" + dxcHash.Hex()
            + ":dxil:" + dxilHash.Hex();
    }

    bool EnsureSlang(std::string& outError)
    {
        SlangRuntime& runtime = GetSlangRuntime();
        std::call_once(runtime.loadOnce, [&runtime]() { LoadSlangOnce(runtime); });
        if (runtime.globalSession && !runtime.identity.empty()) return true;
        outError = runtime.loadError.empty() ? "Slang 초기화 실패" : runtime.loadError;
        return false;
    }

    SlangReflectionTypeLayout* Raw(slang::TypeLayoutReflection* layout)
    {
        return reinterpret_cast<SlangReflectionTypeLayout*>(layout);
    }

    SlangReflectionType* Raw(slang::TypeReflection* type)
    {
        return reinterpret_cast<SlangReflectionType*>(type);
    }

    SlangReflectionVariableLayout* Raw(slang::VariableLayoutReflection* variable)
    {
        return reinterpret_cast<SlangReflectionVariableLayout*>(variable);
    }

    SlangReflection* Raw(slang::ProgramLayout* program)
    {
        return reinterpret_cast<SlangReflection*>(program);
    }

    slang::TypeReflection* SlangLayoutType(slang::TypeLayoutReflection* layout)
    {
        return reinterpret_cast<slang::TypeReflection*>(
            GetSlangRuntime().reflection.layoutType(Raw(layout)));
    }

    slang::TypeReflection::Kind SlangLayoutKind(slang::TypeLayoutReflection* layout)
    {
        return static_cast<slang::TypeReflection::Kind>(
            GetSlangRuntime().reflection.layoutKind(Raw(layout)));
    }

    std::size_t SlangLayoutElementCount(slang::TypeLayoutReflection* layout)
    {
        return GetSlangRuntime().reflection.typeElementCount(
            Raw(SlangLayoutType(layout)), nullptr);
    }

    slang::TypeLayoutReflection* SlangLayoutElement(
        slang::TypeLayoutReflection* layout)
    {
        return reinterpret_cast<slang::TypeLayoutReflection*>(
            GetSlangRuntime().reflection.layoutElement(Raw(layout)));
    }

    std::size_t SlangLayoutSize(slang::TypeLayoutReflection* layout,
        slang::ParameterCategory category)
    {
        return GetSlangRuntime().reflection.layoutSize(
            Raw(layout), static_cast<SlangParameterCategory>(category));
    }

    unsigned SlangLayoutFieldCount(slang::TypeLayoutReflection* layout)
    {
        return GetSlangRuntime().reflection.layoutFieldCount(Raw(layout));
    }

    slang::VariableLayoutReflection* SlangLayoutField(
        slang::TypeLayoutReflection* layout, unsigned index)
    {
        return reinterpret_cast<slang::VariableLayoutReflection*>(
            GetSlangRuntime().reflection.layoutField(Raw(layout), index));
    }

    slang::TypeLayoutReflection* SlangVariableType(
        slang::VariableLayoutReflection* variable)
    {
        return reinterpret_cast<slang::TypeLayoutReflection*>(
            GetSlangRuntime().reflection.variableLayoutType(Raw(variable)));
    }

    const char* SlangVariableName(slang::VariableLayoutReflection* variable)
    {
        SlangReflectionVariable* reflectionVariable =
            GetSlangRuntime().reflection.variableLayoutVariable(Raw(variable));
        return nullptr == reflectionVariable ? nullptr
            : GetSlangRuntime().reflection.variableName(reflectionVariable);
    }

    std::size_t SlangVariableOffset(slang::VariableLayoutReflection* variable,
        slang::ParameterCategory category)
    {
        return GetSlangRuntime().reflection.variableLayoutOffset(
            Raw(variable), static_cast<SlangParameterCategory>(category));
    }

    unsigned SlangBindingIndex(slang::VariableLayoutReflection* variable)
    {
        return GetSlangRuntime().reflection.bindingIndex(
            reinterpret_cast<SlangReflectionParameter*>(variable));
    }

    unsigned SlangBindingSpace(slang::VariableLayoutReflection* variable)
    {
        return GetSlangRuntime().reflection.bindingSpace(
            reinterpret_cast<SlangReflectionParameter*>(variable));
    }

    unsigned SlangParameterCount(slang::ProgramLayout* program)
    {
        return GetSlangRuntime().reflection.parameterCount(Raw(program));
    }

    slang::VariableLayoutReflection* SlangParameter(
        slang::ProgramLayout* program, unsigned index)
    {
        return reinterpret_cast<slang::VariableLayoutReflection*>(
            GetSlangRuntime().reflection.parameter(Raw(program), index));
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

    std::filesystem::path NormalizePath(const std::filesystem::path& path)
    {
        std::error_code ec;
        const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
        return ec ? path.lexically_normal() : canonical;
    }

    SlangStage MapStage(std::string_view profile, std::string& outError)
    {
        const std::size_t separator = profile.find('_');
        if (std::string_view::npos == separator || 0 == separator)
        {
            outError = "셰이더 타깃 프로필이 잘못됐다: " + std::string(profile);
            return SLANG_STAGE_NONE;
        }

        const std::string_view stage = profile.substr(0, separator);
        if ("vs" == stage) return SLANG_STAGE_VERTEX;
        if ("hs" == stage) return SLANG_STAGE_HULL;
        if ("ds" == stage) return SLANG_STAGE_DOMAIN;
        if ("gs" == stage) return SLANG_STAGE_GEOMETRY;
        if ("ps" == stage) return SLANG_STAGE_FRAGMENT;
        if ("cs" == stage) return SLANG_STAGE_COMPUTE;
        outError = "지원하지 않는 셰이더 스테이지다: " + std::string(profile);
        return SLANG_STAGE_NONE;
    }

    std::string ReadSlangDiagnostics(slang::IBlob* diagnostics)
    {
        if (nullptr == diagnostics || 0 == diagnostics->getBufferSize()) return {};
        const auto* data = static_cast<const char*>(diagnostics->getBufferPointer());
        std::size_t size = diagnostics->getBufferSize();
        while (size > 0 && '\0' == data[size - 1]) --size;
        return std::string(data, size);
    }

    bool CollectSlangDependencies(slang::IModule& module,
        const std::filesystem::path& sourcePath, const std::string& sourceText,
        std::vector<SourceUnit>& outUnits, std::string& outError)
    {
        const std::filesystem::path root = NormalizePath(sourcePath);
        std::unordered_map<std::wstring, std::filesystem::path> paths;
        paths.emplace(root.native(), root);

        const SlangInt32 count = module.getDependencyFileCount();
        for (SlangInt32 index = 0; index < count; ++index)
        {
            const char* dependency = module.getDependencyFilePath(index);
            if (nullptr == dependency || '\0' == dependency[0]) continue;
            std::filesystem::path path(WidenUtf8(dependency));
            if (path.is_relative()) path = sourcePath.parent_path() / path;
            path = NormalizePath(path);
            paths.emplace(path.native(), std::move(path));
        }

        outUnits.clear();
        outUnits.reserve(paths.size());
        outUnits.push_back({ root, sourceText });

        std::vector<std::filesystem::path> dependencies;
        dependencies.reserve(paths.size());
        for (const auto& [key, path] : paths)
        {
            if (key != root.native()) dependencies.push_back(path);
        }
        std::sort(dependencies.begin(), dependencies.end(),
            [](const auto& left, const auto& right)
            {
                return PathUtf8(left) < PathUtf8(right);
            });
        for (const std::filesystem::path& path : dependencies)
        {
            SourceUnit unit{ path };
            if (!ReadSourceFile(path, unit.text, outError)) return false;
            outUnits.push_back(std::move(unit));
        }
        return true;
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
        if (ec) return PathFinder::CachePath("Shaders/v1");
        return temp / "CreatorEngine" / "ShaderCache" / "v1";
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
        const std::vector<SourceUnit>& units, std::string_view compilerIdentity)
    {
        Hash128 hash;
        hash.Add("CreatorEngine.RHIShaderCompiler.v11.Slang.O3.column-major.dx-layout.permutation-key");
        hash.Add(compilerIdentity);
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
        if (nullptr != request.permutation && !request.permutation->Empty())
        {
            const RHIShaderPermutationKey permutationKey = request.permutation->Key();
            hash.Add(&permutationKey.lo, sizeof(permutationKey.lo));
            hash.Add(&permutationKey.hi, sizeof(permutationKey.hi));
            for (const RHIShaderPermutation::Entry& entry :
                request.permutation->Entries())
            {
                hash.Add(entry.name);
                hash.Add(entry.value);
            }
        }
        return hash.Hex();
    }

    std::optional<RHIShaderStage> MapReflectionStage(SlangStage stage)
    {
        switch (stage)
        {
        case SLANG_STAGE_VERTEX: return RHIShaderStage::Vertex;
        case SLANG_STAGE_FRAGMENT: return RHIShaderStage::Pixel;
        case SLANG_STAGE_COMPUTE: return RHIShaderStage::Compute;
        default: return std::nullopt;
        }
    }

    bool ReadArrayLayout(slang::TypeLayoutReflection* layout,
        slang::TypeLayoutReflection*& outBase, std::uint32_t& outElements,
        std::string& outError)
    {
        std::uint64_t elements = 1;
        slang::TypeLayoutReflection* base = layout;
        while (base && slang::TypeReflection::Kind::Array == SlangLayoutKind(base))
        {
            const std::size_t count = SlangLayoutElementCount(base);
            if (SLANG_UNKNOWN_SIZE == count || SLANG_UNBOUNDED_SIZE == count
                || 0 == count
                || elements > (std::numeric_limits<std::uint32_t>::max)() / count)
            {
                outError = "지원하지 않는 shader reflection 배열 크기다";
                return false;
            }
            elements *= count;
            base = SlangLayoutElement(base);
        }
        if (nullptr == base)
        {
            outError = "shader reflection type layout이 비었다";
            return false;
        }
        outBase = base;
        outElements = static_cast<std::uint32_t>(elements);
        return true;
    }

    std::optional<RHIShaderScalarKind> MapScalar(
        slang::TypeReflection::ScalarType scalar)
    {
        switch (scalar)
        {
        case slang::TypeReflection::Bool: return RHIShaderScalarKind::Bool;
        case slang::TypeReflection::Int32: return RHIShaderScalarKind::Int32;
        case slang::TypeReflection::UInt32: return RHIShaderScalarKind::UInt32;
        case slang::TypeReflection::Float32: return RHIShaderScalarKind::Float32;
        default: return std::nullopt;
        }
    }

    bool ExtractValueType(slang::TypeLayoutReflection* layout,
        RHIShaderValueType& outType, std::uint32_t& outByteSize,
        std::string& outError)
    {
        slang::TypeLayoutReflection* base = nullptr;
        std::uint32_t arrayElements = 1;
        if (!ReadArrayLayout(layout, base, arrayElements, outError)) return false;

        slang::TypeReflection* reflectedType = SlangLayoutType(base);
        if (nullptr == reflectedType)
        {
            outError = "shader field type reflection이 없다";
            return false;
        }
        const SlangReflectionApi& api = GetSlangRuntime().reflection;
        const auto scalar = MapScalar(static_cast<slang::TypeReflection::ScalarType>(
            api.typeScalarType(Raw(reflectedType))));
        if (!scalar)
        {
            const char* typeName = api.typeName(Raw(reflectedType));
            outError = "지원하지 않는 shader field scalar type이다: "
                + std::string(nullptr == typeName ? "unnamed" : typeName);
            return false;
        }

        const unsigned reflectedRows = api.typeRowCount(Raw(reflectedType));
        const unsigned reflectedColumns = api.typeColumnCount(Raw(reflectedType));
        const unsigned rows = 0 == reflectedRows ? 1 : reflectedRows;
        const unsigned columns = 0 == reflectedColumns ? 1 : reflectedColumns;
        if (rows > (std::numeric_limits<std::uint16_t>::max)()
            || columns > (std::numeric_limits<std::uint16_t>::max)())
        {
            outError = "shader field 행/열 수가 표현 범위를 넘었다";
            return false;
        }

        const std::size_t byteSize = SlangLayoutSize(
            layout, slang::ParameterCategory::Uniform);
        if (SLANG_UNKNOWN_SIZE == byteSize || SLANG_UNBOUNDED_SIZE == byteSize
            || byteSize > (std::numeric_limits<std::uint32_t>::max)())
        {
            outError = "shader field byte size를 표현할 수 없다";
            return false;
        }

        outType = { *scalar, static_cast<std::uint16_t>(rows),
            static_cast<std::uint16_t>(columns), arrayElements };
        outByteSize = static_cast<std::uint32_t>(byteSize);
        return true;
    }

    std::optional<RHIShaderResourceKind> ClassifyResource(
        slang::TypeLayoutReflection* layout)
    {
        const slang::TypeReflection::Kind kind = SlangLayoutKind(layout);
        if (slang::TypeReflection::Kind::ConstantBuffer == kind)
            return RHIShaderResourceKind::ConstantBuffer;
        if (slang::TypeReflection::Kind::SamplerState == kind)
            return RHIShaderResourceKind::Sampler;
        if (slang::TypeReflection::Kind::Resource != kind
            && slang::TypeReflection::Kind::TextureBuffer != kind
            && slang::TypeReflection::Kind::ShaderStorageBuffer != kind)
        {
            return std::nullopt;
        }

        slang::TypeReflection* reflectedType = SlangLayoutType(layout);
        if (nullptr == reflectedType) return std::nullopt;
        const SlangReflectionApi& api = GetSlangRuntime().reflection;
        const SlangResourceShape shape = static_cast<SlangResourceShape>(
            api.typeResourceShape(Raw(reflectedType)) & SLANG_RESOURCE_BASE_SHAPE_MASK);
        const SlangResourceAccess access = api.typeResourceAccess(Raw(reflectedType));
        const bool writable = SLANG_RESOURCE_ACCESS_READ != access;
        switch (shape)
        {
        case SLANG_TEXTURE_1D:
        case SLANG_TEXTURE_2D:
        case SLANG_TEXTURE_3D:
        case SLANG_TEXTURE_CUBE:
        case SLANG_TEXTURE_BUFFER:
            return writable ? RHIShaderResourceKind::StorageTexture
                : RHIShaderResourceKind::Texture;
        case SLANG_STRUCTURED_BUFFER:
            return writable ? RHIShaderResourceKind::StorageBuffer
                : RHIShaderResourceKind::StructuredBuffer;
        case SLANG_BYTE_ADDRESS_BUFFER:
            return writable ? RHIShaderResourceKind::StorageByteAddressBuffer
                : RHIShaderResourceKind::ByteAddressBuffer;
        default:
            return std::nullopt;
        }
    }

    std::uint32_t BindingShift(RHIShaderResourceKind kind)
    {
        switch (kind)
        {
        case RHIShaderResourceKind::ConstantBuffer:
            return VulkanBindingModel::kConstantBufferShift;
        case RHIShaderResourceKind::Texture:
        case RHIShaderResourceKind::StructuredBuffer:
        case RHIShaderResourceKind::ByteAddressBuffer:
            return VulkanBindingModel::kShaderResourceShift;
        case RHIShaderResourceKind::StorageTexture:
        case RHIShaderResourceKind::StorageBuffer:
        case RHIShaderResourceKind::StorageByteAddressBuffer:
            return VulkanBindingModel::kUnorderedAccessShift;
        case RHIShaderResourceKind::Sampler:
            return VulkanBindingModel::kSamplerShift;
        }
        return 0;
    }

    std::uint32_t NormalizeBinding(RHIShaderBinary output,
        RHIShaderResourceKind kind, std::uint32_t backendBinding)
    {
        if (RHIShaderBinary::SpirV != output) return backendBinding;
        const std::uint32_t shift = BindingShift(kind);
        // Slang API 버전에 따라 getBindingIndex()가 target decoration 또는
        // 원래 HLSL register를 보고한다. shifted decoration이면 되돌리고,
        // 이미 논리 register면 그대로 둔다.
        return 0 != shift && backendBinding >= shift
            ? backendBinding - shift : backendBinding;
    }

    bool ExtractSlangReflection(slang::IComponentType& linked,
        const RHIShaderCompileRequest& request, SlangStage slangStage,
        RHIShaderReflection& outReflection, std::string& outError)
    {
        const auto stage = MapReflectionStage(slangStage);
        if (!stage)
        {
            outError = "ShaderMeta가 지원하지 않는 reflection stage다";
            return false;
        }

        Slang::ComPtr<slang::IBlob> diagnostics;
        slang::ProgramLayout* program = linked.getLayout(0, diagnostics.writeRef());
        if (nullptr == program)
        {
            const std::string detail = ReadSlangDiagnostics(diagnostics.get());
            outError = std::string(request.name) + " Slang reflection 실패: "
                + (detail.empty() ? "program layout이 없다" : detail);
            return false;
        }

        RHIShaderReflection reflection;
        reflection.stage = *stage;
        const unsigned parameterCount = SlangParameterCount(program);
        reflection.resources.reserve(parameterCount);
        for (unsigned index = 0; index < parameterCount; ++index)
        {
            slang::VariableLayoutReflection* variable = SlangParameter(program, index);
            slang::TypeLayoutReflection* variableType = nullptr == variable
                ? nullptr : SlangVariableType(variable);
            if (nullptr == variableType) continue;

            slang::TypeLayoutReflection* base = nullptr;
            std::uint32_t arrayElements = 1;
            if (!ReadArrayLayout(variableType, base, arrayElements, outError))
                return false;
            const auto kind = ClassifyResource(base);
            if (!kind)
            {
                const char* variableName = SlangVariableName(variable);
                outError = "지원하지 않는 shader global resource type이다: "
                    + std::string(nullptr == variableName ? "unnamed" : variableName);
                return false;
            }

            const unsigned backendBinding = SlangBindingIndex(variable);
            const unsigned bindingSpace = SlangBindingSpace(variable);
            if ((std::numeric_limits<unsigned>::max)() == backendBinding
                || (std::numeric_limits<unsigned>::max)() == bindingSpace)
            {
                outError = "shader resource binding이 확정되지 않았다";
                return false;
            }

            RHIShaderResourceReflection resource;
            const char* variableName = SlangVariableName(variable);
            resource.name = nullptr == variableName ? "" : variableName;
            resource.kind = *kind;
            resource.registerIndex = NormalizeBinding(request.output, *kind, backendBinding);
            resource.registerSpace = bindingSpace;
            resource.arrayElements = arrayElements;

            if (RHIShaderResourceKind::ConstantBuffer == *kind)
            {
                slang::TypeLayoutReflection* element = SlangLayoutElement(base);
                if (nullptr == element)
                {
                    outError = "constant buffer element layout이 없다: " + resource.name;
                    return false;
                }
                const std::size_t byteSize = SlangLayoutSize(
                    element, slang::ParameterCategory::Uniform);
                if (SLANG_UNKNOWN_SIZE == byteSize || SLANG_UNBOUNDED_SIZE == byteSize
                    || byteSize > (std::numeric_limits<std::uint32_t>::max)())
                {
                    outError = "constant buffer byte size를 표현할 수 없다: "
                        + resource.name;
                    return false;
                }
                resource.byteSize = static_cast<std::uint32_t>(byteSize);
                const unsigned fieldCount = SlangLayoutFieldCount(element);
                resource.fields.reserve(fieldCount);
                for (unsigned fieldIndex = 0; fieldIndex < fieldCount; ++fieldIndex)
                {
                    slang::VariableLayoutReflection* field =
                        SlangLayoutField(element, fieldIndex);
                    slang::TypeLayoutReflection* fieldType = nullptr == field
                        ? nullptr : SlangVariableType(field);
                    if (nullptr == fieldType)
                    {
                        outError = "constant buffer field layout이 없다: " + resource.name;
                        return false;
                    }
                    const std::size_t offset = SlangVariableOffset(
                        field, slang::ParameterCategory::Uniform);
                    if (SLANG_UNKNOWN_SIZE == offset
                        || offset > (std::numeric_limits<std::uint32_t>::max)())
                    {
                        outError = "constant buffer field offset을 표현할 수 없다: "
                            + resource.name;
                        return false;
                    }

                    RHIShaderFieldReflection reflectedField;
                    const char* fieldName = SlangVariableName(field);
                    reflectedField.name = nullptr == fieldName ? "" : fieldName;
                    reflectedField.byteOffset = static_cast<std::uint32_t>(offset);
                    if (!ExtractValueType(fieldType, reflectedField.type,
                        reflectedField.byteSize, outError))
                    {
                        outError += ": " + resource.name + "." + reflectedField.name;
                        return false;
                    }
                    resource.fields.push_back(std::move(reflectedField));
                }
                std::ranges::sort(resource.fields, {},
                    [](const RHIShaderFieldReflection& field)
                    {
                        return std::tie(field.byteOffset, field.name);
                    });
            }
            reflection.resources.push_back(std::move(resource));
        }

        std::ranges::sort(reflection.resources, {},
            [](const RHIShaderResourceReflection& resource)
            {
                return std::tuple(resource.kind, resource.registerSpace,
                    resource.registerIndex, resource.name);
            });
        outReflection = std::move(reflection);
        outError.clear();
        return true;
    }

    class SlangShaderCompiler final : public IRHIShaderCompiler
    {
    public:
        bool Compile(const RHIShaderCompileRequest& request,
            RHIShaderBlob& outBlob, std::string& outError) override
        {
            return Process(request, &outBlob, nullptr, outError);
        }

        bool Reflect(const RHIShaderCompileRequest& request,
            RHIShaderReflection& outReflection, std::string& outError) override
        {
            return Process(request, nullptr, &outReflection, outError);
        }

    private:
        bool Process(const RHIShaderCompileRequest& request,
            RHIShaderBlob* outBlob, RHIShaderReflection* outReflection,
            std::string& outError)
        {
            if (!EnsureSlang(outError)) return false;

            const std::filesystem::path sourcePath = RHIShaderSource::Resolve(request.name);
            std::string sourceText;
            if (!ReadSourceFile(sourcePath, sourceText, outError)) return false;
            const SlangStage stage = MapStage(request.targetProfile, outError);
            if (SLANG_STAGE_NONE == stage) return false;

            SlangRuntime& runtime = GetSlangRuntime();
            std::lock_guard<std::mutex> compileGuard(runtime.compileMutex);

            std::vector<std::string> ownedArguments;
            ownedArguments.reserve(40);
            const auto addArgument = [&ownedArguments](std::string value)
            {
                ownedArguments.push_back(std::move(value));
            };
            addArgument("-target");
            addArgument(RHIShaderBinary::Dxil == request.output ? "dxil" : "spirv");
            addArgument("-profile");
            addArgument(RHIShaderBinary::Dxil == request.output ? "sm_6_0" : "spirv_1_3");
            // 소스 언어는 확장자가 정한다. Slang은 HLSL의 상위집합이라 둘을
            // 한 세션 설정으로 묶고 싶어지지만, front-end 규칙이 갈린다 —
            // .slang은 `import`·`[shader(...)]`·모듈 가시성을 알고 .hlsl은
            // 모르며, hlsl 모드로 .slang을 먹이면 그 문법이 파스 에러가 된다.
            // 그래서 이관 중에는 두 언어가 공존하고, 판정은 파일 하나 단위다.
            addArgument("-lang");
            addArgument(IsSlangSource(sourcePath) ? "slang" : "hlsl");
            addArgument("-matrix-layout-column-major");
            addArgument("-O3");
            addArgument("-warnings-as-errors");
            addArgument("all");
            addArgument("-I");
            addArgument(PathUtf8(sourcePath.parent_path()));
            if (request.options.strictMath)
            {
                addArgument("-fp-mode");
                addArgument("precise");
            }

            if (RHIShaderBinary::SpirV == request.output)
            {
                addArgument("-D__spirv__=1");
                addArgument("-fvk-use-entrypoint-name");
                // Material property upload은 HLSL cbuffer offset을 정본으로 삼는다.
                // Vulkan도 같은 offset을 사용해야 DXIL/SPIR-V reflection과 실제
                // GPU 접근이 일치한다.
                addArgument("-fvk-use-dx-layout");
                const std::array<std::pair<const char*, std::uint32_t>, 4> shifts = {{
                    { "-fvk-b-shift", VulkanBindingModel::kConstantBufferShift },
                    { "-fvk-t-shift", VulkanBindingModel::kShaderResourceShift },
                    { "-fvk-u-shift", VulkanBindingModel::kUnorderedAccessShift },
                    { "-fvk-s-shift", VulkanBindingModel::kSamplerShift },
                }};
                for (const auto& [argument, value] : shifts)
                {
                    addArgument(argument);
                    addArgument(std::to_string(value));
                    addArgument("0");
                }
            }

            if (nullptr != request.permutation)
            {
                for (const RHIShaderPermutation::Entry& entry :
                    request.permutation->Entries())
                {
                    std::string value = "-D" + entry.name + "=" + entry.value;
                    addArgument(std::move(value));
                }
            }

            std::vector<const char*> arguments;
            arguments.reserve(ownedArguments.size());
            for (const std::string& argument : ownedArguments)
                arguments.push_back(argument.c_str());

            slang::SessionDesc sessionDesc{};
            Slang::ComPtr<ISlangUnknown> auxiliary;
            if (SLANG_FAILED(runtime.globalSession->parseCommandLineArguments(
                static_cast<int>(arguments.size()), arguments.data(), &sessionDesc,
                auxiliary.writeRef())))
            {
                ++g_failures;
                outError = "Slang 세션 인자 매핑 실패: " + std::string(request.name);
                return false;
            }

            // Slang 2026.14의 command-line parser는 VulkanBindShift를
            // SessionDesc와 TargetDesc 양쪽에 싣는다. 이 상태를 modern
            // createSession API에 그대로 넘기면 shift가 중복 적용되어 리소스
            // 종류 코드(0x01/0x02/0x03)가 binding 상위 바이트로 굽힌다.
            // session 옵션 전체를 버리면 -D 매크로가 front-end에서 사라지므로,
            // target에 이미 있는 VulkanBindShift 중복본만 session에서 제외한다.
            // 전용 API probe와 동일 버전 slangc의 SPIR-V decoration을 대조해
            // b0/t100/u200/s300을 확인했다.
            std::vector<slang::CompilerOptionEntry> sessionOptions;
            sessionOptions.reserve(sessionDesc.compilerOptionEntryCount);
            for (std::uint32_t i = 0; i < sessionDesc.compilerOptionEntryCount; ++i)
            {
                const slang::CompilerOptionEntry& option =
                    sessionDesc.compilerOptionEntries[i];
                if (slang::CompilerOptionName::VulkanBindShift == option.name) continue;
                sessionOptions.push_back(option);
            }
            sessionDesc.compilerOptionEntries = sessionOptions.data();
            sessionDesc.compilerOptionEntryCount =
                static_cast<std::uint32_t>(sessionOptions.size());

            Slang::ComPtr<slang::ISession> session;
            if (SLANG_FAILED(runtime.globalSession->createSession(
                sessionDesc, session.writeRef())))
            {
                ++g_failures;
                outError = "Slang session 생성 실패: " + std::string(request.name);
                return false;
            }

            Hash128 moduleHash;
            moduleHash.Add(PathUtf8(sourcePath));
            moduleHash.Add(request.entryPoint);
            moduleHash.Add(request.targetProfile);
            const std::string moduleName = "CreatorEngine_" + moduleHash.Hex();
            const std::string sourceName = PathUtf8(sourcePath);

            Slang::ComPtr<slang::IBlob> diagnostics;
            Slang::ComPtr<slang::IModule> shaderModule(
                session->loadModuleFromSourceString(moduleName.c_str(), sourceName.c_str(),
                    sourceText.c_str(), diagnostics.writeRef()));
            if (!shaderModule)
            {
                ++g_failures;
                const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                outError = std::string(request.name) + " Slang 모듈 로드 실패: "
                    + (detail.empty() ? "원인 미상" : detail);
                return false;
            }

            std::vector<SourceUnit> units;
            if (!CollectSlangDependencies(*shaderModule, sourcePath, sourceText,
                units, outError))
            {
                ++g_failures;
                return false;
            }
            const std::string cacheKey = BuildCacheKey(request, units, runtime.identity);
            if (nullptr == outReflection && ReadCache(cacheKey, *outBlob)) return true;

            diagnostics.setNull();
            Slang::ComPtr<slang::IEntryPoint> entryPoint;
            if (SLANG_FAILED(shaderModule->findAndCheckEntryPoint(
                std::string(request.entryPoint).c_str(), stage, entryPoint.writeRef(),
                diagnostics.writeRef())))
            {
                ++g_failures;
                const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                outError = std::string(request.name) + " (" + std::string(request.entryPoint)
                    + ") Slang 엔트리 검증 실패: "
                    + (detail.empty() ? "원인 미상" : detail);
                return false;
            }

            slang::IComponentType* components[] = { shaderModule.get(), entryPoint.get() };
            diagnostics.setNull();
            Slang::ComPtr<slang::IComponentType> composite;
            if (SLANG_FAILED(session->createCompositeComponentType(components,
                static_cast<SlangInt>(std::size(components)), composite.writeRef(),
                diagnostics.writeRef())))
            {
                ++g_failures;
                const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                outError = std::string(request.name) + " Slang 프로그램 조립 실패: "
                    + (detail.empty() ? "원인 미상" : detail);
                return false;
            }

            diagnostics.setNull();
            Slang::ComPtr<slang::IComponentType> linked;
            if (SLANG_FAILED(composite->link(linked.writeRef(), diagnostics.writeRef())))
            {
                ++g_failures;
                const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                outError = std::string(request.name) + " Slang 링크 실패: "
                    + (detail.empty() ? "원인 미상" : detail);
                return false;
            }

            if (nullptr != outReflection)
            {
                if (!ExtractSlangReflection(*linked, request, stage,
                    *outReflection, outError))
                {
                    ++g_failures;
                    return false;
                }
                return true;
            }

            diagnostics.setNull();
            Slang::ComPtr<slang::IBlob> code;
            if (SLANG_FAILED(linked->getEntryPointCode(
                0, 0, code.writeRef(), diagnostics.writeRef()))
                || !code || 0 == code->getBufferSize())
            {
                ++g_failures;
                const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                outError = std::string(request.name) + " ("
                    + std::string(request.entryPoint) + "/"
                    + (RHIShaderBinary::Dxil == request.output ? "DXIL" : "SPIR-V")
                    + ") Slang 코드 생성 실패: "
                    + (detail.empty() ? "산출물이 비었다" : detail);
                return false;
            }

            outBlob->Assign(code->getBufferPointer(), code->getBufferSize());
            ++g_compiles;
            {
                std::vector<std::uint8_t> bytes(outBlob->Size());
                std::memcpy(bytes.data(), outBlob->Data(), outBlob->Size());
                std::lock_guard<std::mutex> guard(g_cacheMutex);
                g_memoryCache.emplace(cacheKey, std::move(bytes));
            }
            WriteCache(cacheKey, *outBlob);
            return true;
        }
    };

    IRHIShaderCompiler& Compiler()
    {
        static SlangShaderCompiler compiler;
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
    std::string_view targetProfile, const RHIShaderPermutation& permutation,
    RHIShaderBlob& outBlob, std::string& outError, RHIShaderCompileOptions options)
{
    const RHIShaderCompileRequest request{
        name, entryPoint, targetProfile, GetOutput(), &permutation, options
    };
    return Compiler().Compile(request, outBlob, outError);
}

bool RHIShaderCompiler::ReflectFile(std::string_view name,
    std::string_view entryPoint, std::string_view targetProfile,
    RHIShaderBinary output, const RHIShaderPermutation& permutation,
    RHIShaderReflection& outReflection, std::string& outError,
    RHIShaderCompileOptions options)
{
    const RHIShaderCompileRequest request{
        name, entryPoint, targetProfile, output, &permutation, options
    };
    return Compiler().Reflect(request, outReflection, outError);
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

