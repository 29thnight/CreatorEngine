#include "EngineRuntimePaths.h"
#include "RHIShaderCompiler.h"
#include "RHIShaderVerifiedCache.h"

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
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <sstream>
#include <thread>
#include <tuple>
#include <utility>
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
    std::unordered_map<std::string, RHIShaderCompiler::VerifiedShader> g_verifiedCache;
    std::mutex g_progressMutex;
    RHIShaderCompiler::Progress g_progress;
    class CompileProgress;
    CompileProgress* g_firstActiveCompile{};
    CompileProgress* g_lastActiveCompile{};
    std::uint64_t g_activeCompileCount{};

    class CompileProgress final
    {
    public:
        CompileProgress(const RHIShaderCompileRequest& request, const std::string& error)
            : m_name(request.name), m_entryPoint(request.entryPoint), m_error(error)
        {
            std::lock_guard guard(g_progressMutex);
            // Request views and intrusive links allocate nothing. Once linked,
            // optional progress text cannot throw out of this constructor.
            Append();
            ++g_activeCompileCount;
            RefreshSnapshot(*this, true);
        }

        ~CompileProgress() noexcept
        {
            std::lock_guard guard(g_progressMutex);
            Detach();
            --g_activeCompileCount;
            ++g_progress.completedRequests;
            RefreshSnapshot(*this, false, &m_error);
        }

        CompileProgress(const CompileProgress&) = delete;
        CompileProgress& operator=(const CompileProgress&) = delete;

        void Phase(const char* phase, bool recompiling = false)
        {
            std::lock_guard guard(g_progressMutex);
            m_phase = phase;
            m_recompiling |= recompiling;
            // Keep a representative stage for each live request so completing
            // the newest one can immediately reveal another request's stage.
            Detach();
            Append();
            RefreshSnapshot(*this);
        }

    private:
        void Append() noexcept
        {
            m_previous = g_lastActiveCompile;
            m_next = nullptr;
            if (m_previous)
            {
                m_previous->m_next = this;
            }
            else
            {
                g_firstActiveCompile = this;
            }
            g_lastActiveCompile = this;
        }

        void Detach() noexcept
        {
            if (m_previous)
            {
                m_previous->m_next = m_next;
            }
            else
            {
                g_firstActiveCompile = m_next;
            }
            if (m_next)
            {
                m_next->m_previous = m_previous;
            }
            else
            {
                g_lastActiveCompile = m_previous;
            }
            m_previous = nullptr;
            m_next = nullptr;
        }

        // All access to the active list and request stages holds g_progressMutex.
        // Diagnostics are best effort: keep counts truthful even if a text copy
        // cannot allocate, and never replace or throw over the compiler's error.
        static void RefreshSnapshot(const CompileProgress& completed, bool clearError = false,
                                    const std::string* error = nullptr) noexcept
        {
            g_progress.activeRequests = g_activeCompileCount;
            g_progress.active = g_activeCompileCount != 0;
            g_progress.recompiling = false;
            for (const auto* request = g_firstActiveCompile; request; request = request->m_next)
            {
                g_progress.recompiling |= request->m_recompiling;
            }
            ++g_progress.revision;
            const auto& representative = g_lastActiveCompile ? *g_lastActiveCompile : completed;
            try
            {
                g_progress.name = representative.m_name;
                g_progress.entryPoint = representative.m_entryPoint;
                g_progress.phase = representative.m_phase;
                if (error)
                {
                    g_progress.lastError = *error;
                }
                else if (clearError)
                {
                    g_progress.lastError.clear();
                }
            }
            catch (...)
            {
                g_progress.name.clear();
                g_progress.entryPoint.clear();
                g_progress.phase.clear();
                g_progress.lastError.clear();
            }
        }

        // The request's text outlives its Process scope, including this tracker.
        const std::string_view m_name;
        const std::string_view m_entryPoint;
        const std::string& m_error;
        const char* m_phase = "Resolving shader dependencies";
        bool m_recompiling{};
        CompileProgress* m_previous{};
        CompileProgress* m_next{};
    };
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

    // A Slang global session is not thread-safe, so each concurrent compile owns
    // one. Slot 0 is the bootstrap session; more are made on demand up to the cap.
    struct SlangSlot final
    {
        Slang::ComPtr<slang::IGlobalSession> globalSession;
        bool busy{};
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
        std::mutex slotsMutex;
        std::condition_variable slotFreed;
        std::vector<std::unique_ptr<SlangSlot>> slots;

        ~SlangRuntime()
        {
            // Slang COM 객체의 vtable은 DLL에 있으므로 모든 인터페이스를 먼저
            // 해제한 뒤 모듈을 내린다.
            slots.clear();
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

    // Each slot holds a full Slang core module, so the cap trades memory for
    // parallel material compiles.
    constexpr std::size_t kMaxSlangSlots = 4;

    class SlotLease final
    {
    public:
        SlotLease() = default;
        SlotLease(SlangRuntime& runtime, SlangSlot& slot) : m_runtime(&runtime), m_slot(&slot) {}
        SlotLease(SlotLease&& other) noexcept
            : m_runtime(std::exchange(other.m_runtime, nullptr)), m_slot(std::exchange(other.m_slot, nullptr)) {}
        SlotLease& operator=(SlotLease&& other) noexcept
        {
            if (this != &other)
            {
                Release();
                m_runtime = std::exchange(other.m_runtime, nullptr);
                m_slot = std::exchange(other.m_slot, nullptr);
            }
            return *this;
        }
        SlotLease(const SlotLease&) = delete;
        SlotLease& operator=(const SlotLease&) = delete;
        ~SlotLease() { Release(); }

        SlangSlot* Slot() const { return m_slot; }

    private:
        void Release()
        {
            if (nullptr == m_slot) return;
            {
                std::lock_guard<std::mutex> lock(m_runtime->slotsMutex);
                m_slot->busy = false;
            }
            m_runtime->slotFreed.notify_one();
            m_slot = nullptr;
        }

        SlangRuntime* m_runtime{};
        SlangSlot* m_slot{};
    };

    SlotLease AcquireSlot(SlangRuntime& runtime, std::string& outError)
    {
        const std::size_t cap = RHIShaderCompiler::MaxParallelCompiles();
        std::unique_lock<std::mutex> lock(runtime.slotsMutex);
        for (;;)
        {
            for (const auto& slot : runtime.slots)
            {
                if (!slot->busy)
                {
                    slot->busy = true;
                    return SlotLease(runtime, *slot);
                }
            }
            if (runtime.slots.size() < cap)
            {
                auto created = std::make_unique<SlangSlot>();
                created->busy = true;
                SlangSlot& slot = *created;
                runtime.slots.push_back(std::move(created));
                lock.unlock();
                // Creating a global session loads the core module (hundreds of ms);
                // other threads keep using the existing slots meanwhile.
                SlangGlobalSessionDesc desc{};
                if (SLANG_FAILED(runtime.createGlobalSession(&desc, slot.globalSession.writeRef())))
                {
                    lock.lock();
                    std::erase_if(runtime.slots, [&](const auto& item) { return item.get() == &slot; });
                    lock.unlock();
                    runtime.slotFreed.notify_one();
                    outError = "Slang global session 추가 생성 실패";
                    return {};
                }
                return SlotLease(runtime, slot);
            }
            runtime.slotFreed.wait(lock);
        }
    }

    struct ReusedModule final
    {
        std::string key;
        Slang::ComPtr<slang::ISession> session;
        Slang::ComPtr<slang::IModule> module;
    };

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
            ResolveEngineRuntimeDirectory() / L"Common" / L"slang-compiler.dll");

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
        runtime.slots.push_back(std::make_unique<SlangSlot>());
        runtime.slots.back()->globalSession = runtime.globalSession;

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
        if ("ms" == stage)
        {
            return SLANG_STAGE_MESH;
        }
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

    constexpr std::uint32_t kVerifiedCacheMagic = 0x56534852u; // RHSV
    constexpr std::uint32_t kVerifiedCacheSchema = 1u;
    struct VerifiedCacheHeader
    {
        std::uint32_t magic{ kVerifiedCacheMagic };
        std::uint32_t schema{ kVerifiedCacheSchema };
        std::uint64_t byteCount{};
        std::uint64_t contentLo{}, contentHi{};
        std::uint64_t keyLo{}, keyHi{};
    };

    bool ReadVerifiedCache(const std::string& key, RHIShaderStage stage,
        RHIShaderBlob* outBlob, RHIShaderReflection& outReflection)
    {
        {
            std::lock_guard guard(g_cacheMutex);
            if (const auto found = g_verifiedCache.find(key); found != g_verifiedCache.end())
            {
                if (found->second.reflection.stage != stage) return false;
                if (outBlob) *outBlob = found->second.bytecode;
                outReflection = found->second.reflection;
                ++g_memoryHits;
                return true;
            }
        }
        const auto path = CacheDirectory() / (key + ".rsv");
        std::ifstream file(path, std::ios::binary);
        VerifiedCacheHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) ||
            header.magic != kVerifiedCacheMagic || header.schema != kVerifiedCacheSchema ||
            !header.byteCount || header.byteCount > rhi_shader_verified_cache::kMaxPayloadBytes)
            return false;
        const auto keyHash = HashBytes(key.data(), key.size());
        if (header.keyLo != keyHash.lo || header.keyHi != keyHash.hi) return false;
        std::error_code ec;
        if (std::filesystem::file_size(path, ec) != sizeof(header) + header.byteCount || ec) return false;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(header.byteCount));
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return false;
        const auto hash = HashBytes(bytes.data(), bytes.size());
        if (hash.lo != header.contentLo || hash.hi != header.contentHi) return false;
        RHIShaderCompiler::VerifiedShader candidate;
        if (!rhi_shader_verified_cache::Decode(bytes, stage, candidate.bytecode, candidate.reflection))
            return false;
        candidate.dependencyIdentity = key;
        if (outBlob) *outBlob = candidate.bytecode;
        outReflection = candidate.reflection;
        {
            std::lock_guard guard(g_cacheMutex);
            g_verifiedCache.emplace(key, std::move(candidate));
        }
        ++g_diskHits;
        return true;
    }

    void WriteVerifiedCache(const std::string& key, const RHIShaderBlob& blob,
        const RHIShaderReflection& reflection)
    {
        // Publish the pair together, never infer reflection from a bytecode-only hit.
        {
            std::lock_guard guard(g_cacheMutex);
            g_verifiedCache.insert_or_assign(key,
                RHIShaderCompiler::VerifiedShader{blob, reflection, key});
        }
        std::vector<std::uint8_t> bytes;
        if (!rhi_shader_verified_cache::Encode(blob, reflection, bytes)) return;
        const auto directory = CacheDirectory();
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        if (ec) return;
        static std::atomic<std::uint64_t> serial{};
        const auto path = directory / (key + ".rsv");
        const auto temp = directory / (key + "." + std::to_string(GetCurrentProcessId()) + "."
            + std::to_string(GetCurrentThreadId()) + "." + std::to_string(++serial) + ".rsv.tmp");
        const auto hash = HashBytes(bytes.data(), bytes.size());
        const auto keyHash = HashBytes(key.data(), key.size());
        VerifiedCacheHeader header;
        header.byteCount = bytes.size();
        header.contentLo = hash.lo; header.contentHi = hash.hi;
        header.keyLo = keyHash.lo; header.keyHi = keyHash.hi;
        bool written = false;
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (file)
            {
                file.write(reinterpret_cast<const char*>(&header), sizeof(header));
                file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                file.flush();
                written = static_cast<bool>(file);
            }
        }
        // Atomic replacement also repairs a corrupt/old record after fallback.
        // Failure to persist a valid compile is not a compilation failure.
        if (!written || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            std::filesystem::remove(temp, ec);
    }

    std::string BuildCacheKey(const RHIShaderCompileRequest& request,
        const std::vector<SourceUnit>& units, std::string_view compilerIdentity)
    {
        Hash128 hash;
        hash.Add("CreatorEngine.RHIShaderCompiler.v13.Slang.O3.column-major.dx-layout.verified-layout-v1.include-paths.permutation-key");
        hash.Add(compilerIdentity);
        hash.Add(request.name);
        hash.Add(request.entryPoint);
        hash.Add(request.targetProfile);
        if (request.targetProfile.starts_with("ms_"))
        {
            hash.Add("mesh-v1.sm_6_5.spirv_1_5.spvMeshShadingEXT");
        }
        hash.Add(&request.output, sizeof(request.output));
        if (request.output == RHIShaderBinary::SpirV)
        {
            const std::array shifts{VulkanBindingModel::kConstantBufferShift, VulkanBindingModel::kShaderResourceShift,
                VulkanBindingModel::kUnorderedAccessShift, VulkanBindingModel::kSamplerShift};
            hash.Add(shifts.data(), sizeof(shifts));
        }
        hash.Add(&request.options.strictMath, sizeof(request.options.strictMath));
        hash.Add(&request.options.fineDerivatives, sizeof(request.options.fineDerivatives));
        for (const auto& directory : request.options.includeDirectories)
            hash.Add(PathUtf8(directory));
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

    // 캐시 키에는 Slang 이 실제로 읽은 의존 파일이 들어간다. 그 목록을 알려면 Slang 이
    // 소스를 구문·의미 분석까지 해야 해서, 캐시에 있어도 셰이더 하나에 수십~수백 ms 가 든다.
    // 그래서 요청마다 지난번 목록을 따로 적어 두고, 다음에는 그 파일들의 지금 내용으로 키를
    // 만들어 먼저 찾는다. 키가 내용을 담으므로 파일이 바뀌면 빗나가고 Slang 경로로 간다.
    constexpr std::string_view kDependencyListMagic = "CreatorEngine.ShaderDependencies.v1";

    std::filesystem::path DependencyListPath(const RHIShaderCompileRequest& request,
        const std::filesystem::path& root, std::string_view compilerIdentity)
    {
        // 의존 목록 없이 루트 경로만 넣은 키다. 내용은 넣지 않아 원본을 고쳐도 같은 목록을 찾는다.
        const std::string key = BuildCacheKey(request, { SourceUnit{ root } }, compilerIdentity);
        return CacheDirectory() / (key + ".rsd");
    }

    std::optional<std::vector<std::filesystem::path>> ReadDependencyList(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        std::string line;
        if (!std::getline(file, line) || line != kDependencyListMagic) return std::nullopt;
        std::vector<std::filesystem::path> dependencies;
        while (std::getline(file, line))
        {
            if (!line.empty()) dependencies.emplace_back(WidenUtf8(line));
        }
        return dependencies;
    }

    void WriteDependencyList(const std::filesystem::path& path, const std::vector<SourceUnit>& units)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return;
        static std::atomic<std::uint64_t> serial{};
        const auto temp = path.parent_path() / (path.filename().string() + "." + std::to_string(GetCurrentProcessId())
            + "." + std::to_string(GetCurrentThreadId()) + "." + std::to_string(++serial) + ".tmp");
        bool written = false;
        {
            std::ofstream file(temp, std::ios::binary | std::ios::trunc);
            if (file)
            {
                file << kDependencyListMagic << '\n';
                for (std::size_t index = 1; index < units.size(); ++index)
                    file << PathUtf8(units[index].path) << '\n';
                file.flush();
                written = static_cast<bool>(file);
            }
        }
        // 목록은 힌트일 뿐이라 쓰기 실패는 컴파일 실패가 아니다.
        if (!written || !MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            std::filesystem::remove(temp, ec);
    }

    // 기록한 의존 파일과 같은 이름의 파일이 다른 검색 위치에 생기면 Slang 은 그쪽을 읽을 수 있다.
    // 기록한 파일만 보면 이 변화를 못 보므로, 그런 후보가 하나라도 있으면 빠른 길을 쓰지 않는다.
    // 표기는 파일 이름과 폴더 하나를 붙인 이름("Includes/X.slang")까지 본다.
    bool HasShadowCandidate(const std::vector<SourceUnit>& units, const RHIShaderCompileRequest& request)
    {
        std::vector<std::filesystem::path> locations;
        const auto addLocation = [&locations](const std::filesystem::path& directory)
        {
            if (directory.empty() || std::ranges::find(locations, directory) != locations.end()) return;
            locations.push_back(directory);
        };
        for (const SourceUnit& unit : units)
        {
            addLocation(unit.path.parent_path());
            addLocation(unit.path.parent_path().parent_path()); // "../X.slang" 표기
        }
        for (const auto& directory : request.options.includeDirectories)
        {
            const std::filesystem::path normalized = NormalizePath(directory);
            addLocation(normalized);
            addLocation(normalized.parent_path());
        }
        for (std::size_t index = 1; index < units.size(); ++index)
        {
            const std::filesystem::path& path = units[index].path;
            const std::filesystem::path spellings[] = {
                path.filename(), path.parent_path().filename() / path.filename() };
            for (const auto& location : locations)
            {
                for (const auto& spelling : spellings)
                {
                    const std::filesystem::path candidate = (location / spelling).lexically_normal();
                    if (candidate == path) continue;
                    std::error_code ec;
                    if (std::filesystem::exists(candidate, ec) || ec) return true;
                }
            }
        }
        return false;
    }

    std::optional<RHIShaderStage> MapReflectionStage(SlangStage stage)
    {
        switch (stage)
        {
        case SLANG_STAGE_VERTEX: return RHIShaderStage::Vertex;
        case SLANG_STAGE_FRAGMENT: return RHIShaderStage::Pixel;
        case SLANG_STAGE_COMPUTE: return RHIShaderStage::Compute;
        case SLANG_STAGE_MESH: return RHIShaderStage::Mesh;
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

}

// Members are destroyed in reverse order: modules go before the slot is released,
// so no other thread touches that global session while they are freed.
struct RHIShaderCompiler::ModuleReuseState final
{
    SlotLease lease;
    std::vector<ReusedModule> modules;
};

namespace
{
    thread_local RHIShaderCompiler::ModuleReuseState* t_moduleReuse = nullptr;

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

        bool Verify(const RHIShaderCompileRequest& request,
            RHIShaderCompiler::VerifiedShader& outShader, std::string& outError)
        {
            RHIShaderCompiler::VerifiedShader candidate;
            if (!Process(request, &candidate.bytecode, &candidate.reflection, outError, &candidate.dependencyIdentity,
                    &candidate.dependencies))
                return false;
            outShader = std::move(candidate);
            return true;
        }

    private:
        // 지난번 의존 목록으로 캐시를 찾는다. Slang 을 부르지 않으므로 컴파일 칸도 잡지 않는다.
        // 빗나가거나 목록을 믿을 수 없으면 false 를 돌려 Slang 경로가 다시 해석하게 한다.
        static bool ReadCachedWithoutSlang(const RHIShaderCompileRequest& request,
            const std::filesystem::path& sourcePath, const std::string& sourceText, SlangStage stage,
            const std::vector<std::filesystem::path>& recordedDependencies, std::string_view compilerIdentity,
            RHIShaderBlob* outBlob, RHIShaderReflection* outReflection, std::string& outError,
            std::string* outIdentity, std::vector<std::filesystem::path>* outDependencies, CompileProgress& progress)
        {
            std::vector<SourceUnit> units;
            units.reserve(recordedDependencies.size() + 1);
            units.push_back({ NormalizePath(sourcePath), sourceText });
            for (const auto& path : recordedDependencies)
            {
                SourceUnit unit{ path };
                std::string ignored;
                if (!ReadSourceFile(path, unit.text, ignored)) return false;
                units.push_back(std::move(unit));
            }
            if (HasShadowCandidate(units, request)) return false;

            progress.Phase("Checking shader cache");
            const std::string cacheKey = BuildCacheKey(request, units, compilerIdentity);
            if (nullptr == outReflection)
            {
                if (!ReadCache(cacheKey, *outBlob)) return false;
            }
            else
            {
                const auto reflectionStage = MapReflectionStage(stage);
                if (!reflectionStage || !ReadVerifiedCache(cacheKey, *reflectionStage, outBlob, *outReflection))
                    return false;
                if (outIdentity) *outIdentity = cacheKey;
            }
            if (outDependencies) *outDependencies = recordedDependencies;
            outError.clear();
            return true;
        }

        bool Process(const RHIShaderCompileRequest& request,
            RHIShaderBlob* outBlob, RHIShaderReflection* outReflection,
            std::string& outError, std::string* outIdentity = nullptr,
            std::vector<std::filesystem::path>* outDependencies = nullptr)
        {
            outError.clear();
            CompileProgress progress(request, outError);
            if (!EnsureSlang(outError)) return false;

            const std::filesystem::path sourcePath = RHIShaderSource::Resolve(request.name);
            std::string sourceText;
            if (!ReadSourceFile(sourcePath, sourceText, outError)) return false;
            const SlangStage stage = MapStage(request.targetProfile, outError);
            if (SLANG_STAGE_NONE == stage) return false;

            SlangRuntime& runtime = GetSlangRuntime();
            const std::filesystem::path dependencyListPath =
                DependencyListPath(request, NormalizePath(sourcePath), runtime.identity);
            const auto recordedDependencies = ReadDependencyList(dependencyListPath);
            if (recordedDependencies &&
                ReadCachedWithoutSlang(request, sourcePath, sourceText, stage, *recordedDependencies,
                    runtime.identity, outBlob, outReflection, outError, outIdentity, outDependencies, progress))
            {
                return true;
            }

            RHIShaderCompiler::ModuleReuseState* const reuse = t_moduleReuse;
            SlotLease ownLease;
            progress.Phase("Waiting for shader compiler slot");
            if (nullptr == reuse) ownLease = AcquireSlot(runtime, outError);
            else if (nullptr == reuse->lease.Slot()) reuse->lease = AcquireSlot(runtime, outError);
            SlangSlot* const slot = nullptr == reuse ? ownLease.Slot() : reuse->lease.Slot();
            if (nullptr == slot)
            {
                ++g_failures;
                return false;
            }
            slang::IGlobalSession& globalSession = *slot->globalSession;
            progress.Phase("Resolving shader dependencies");

            std::vector<std::string> ownedArguments;
            ownedArguments.reserve(40);
            const auto addArgument = [&ownedArguments](std::string value)
            {
                ownedArguments.push_back(std::move(value));
            };
            addArgument("-target");
            addArgument(RHIShaderBinary::Dxil == request.output ? "dxil" : "spirv");
            addArgument("-profile");
            const bool meshStage = stage == SLANG_STAGE_MESH;
            addArgument(RHIShaderBinary::Dxil == request.output
                ? (meshStage ? "sm_6_5" : "sm_6_0")
                : (meshStage ? "spirv_1_5" : "spirv_1_3"));
            if (meshStage && request.output == RHIShaderBinary::SpirV)
            {
                addArgument("-capability");
                addArgument("spvMeshShadingEXT");
            }
            if (request.output == RHIShaderBinary::SpirV && request.options.fineDerivatives)
            {
                addArgument("-capability");
                addArgument("spvDerivativeControl");
            }
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
            for (const auto& directory : request.options.includeDirectories)
            {
                addArgument("-I");
                addArgument(PathUtf8(directory));
            }
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

            // Session options carry no stage or entry, so a material's stages for one
            // backend share both the session and the parsed module.
            std::string reuseKey;
            if (nullptr != reuse)
            {
                for (const std::string& argument : ownedArguments)
                    reuseKey.append(argument).push_back('\0');
                reuseKey.append(PathUtf8(sourcePath)).push_back('\0');
                reuseKey.append(sourceText);
            }
            Slang::ComPtr<slang::ISession> session;
            Slang::ComPtr<slang::IModule> shaderModule;
            Slang::ComPtr<slang::IBlob> diagnostics;
            const ReusedModule* reused = nullptr;
            if (nullptr != reuse)
            {
                const auto found = std::ranges::find(reuse->modules, reuseKey, &ReusedModule::key);
                if (found != reuse->modules.end()) reused = &*found;
            }
            if (nullptr != reused)
            {
                session = reused->session;
                shaderModule = reused->module;
            }
            else
            {
                slang::SessionDesc sessionDesc{};
                Slang::ComPtr<ISlangUnknown> auxiliary;
                if (SLANG_FAILED(globalSession.parseCommandLineArguments(
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

                if (SLANG_FAILED(globalSession.createSession(
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

                shaderModule = session->loadModuleFromSourceString(moduleName.c_str(), sourceName.c_str(),
                    sourceText.c_str(), diagnostics.writeRef());
                if (!shaderModule)
                {
                    ++g_failures;
                    const std::string detail = ReadSlangDiagnostics(diagnostics.get());
                    outError = std::string(request.name) + " Slang 모듈 로드 실패: "
                        + (detail.empty() ? "원인 미상" : detail);
                    return false;
                }
                if (nullptr != reuse) reuse->modules.push_back({std::move(reuseKey), session, shaderModule});
            }

            std::vector<SourceUnit> units;
            if (!CollectSlangDependencies(*shaderModule, sourcePath, sourceText,
                units, outError))
            {
                ++g_failures;
                return false;
            }
            if (outDependencies)
            {
                // units[0] is the root source; the rest are what Slang resolved.
                outDependencies->clear();
                for (std::size_t index = 1; index < units.size(); ++index)
                    outDependencies->push_back(units[index].path);
            }
            if (!recordedDependencies ||
                !std::ranges::equal(*recordedDependencies, units | std::views::drop(1), {}, {}, &SourceUnit::path))
                WriteDependencyList(dependencyListPath, units);
            progress.Phase("Checking shader cache");
            const std::string cacheKey = BuildCacheKey(request, units, runtime.identity);
            if (nullptr == outReflection && ReadCache(cacheKey, *outBlob)) return true;
            // Resolve current imports first: new/shadowed includes must invalidate
            // a hit even if all previously recorded dependency files are unchanged.
            if (outReflection)
            {
                const auto reflectionStage = MapReflectionStage(stage);
                if (reflectionStage && ReadVerifiedCache(cacheKey, *reflectionStage, outBlob, *outReflection))
                {
                    if (outIdentity) *outIdentity = cacheKey;
                    outError.clear();
                    return true;
                }
            }

            diagnostics.setNull();
            progress.Phase(outBlob ? "Recompiling shader (cache miss)" : "Reflecting shader", outBlob != nullptr);
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
                if (nullptr == outBlob)
                    return true;
            }

            diagnostics.setNull();
            Slang::ComPtr<slang::IBlob> code;
            progress.Phase("Generating shader code", true);
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
            progress.Phase("Saving compiled shader cache");
            WriteCache(cacheKey, *outBlob);
            if (outReflection) WriteVerifiedCache(cacheKey, *outBlob, *outReflection);
            if (nullptr != outIdentity)
                *outIdentity = cacheKey;
            return true;
        }
    };

    SlangShaderCompiler& Compiler()
    {
        static SlangShaderCompiler compiler;
        return compiler;
    }
}

std::size_t RHIShaderCompiler::MaxParallelCompiles()
{
    return std::clamp<std::size_t>(std::thread::hardware_concurrency() / 2, 1, kMaxSlangSlots);
}

RHIShaderCompiler::ModuleReuseScope::ModuleReuseScope()
    : m_state(new ModuleReuseState), m_previous(t_moduleReuse)
{
    t_moduleReuse = m_state;
}

RHIShaderCompiler::ModuleReuseScope::~ModuleReuseScope()
{
    t_moduleReuse = m_previous;
    delete m_state;
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

bool RHIShaderCompiler::VerifyFile(std::string_view name, std::string_view entryPoint, std::string_view targetProfile,
    RHIShaderBinary output, const RHIShaderPermutation& permutation, VerifiedShader& outShader,
    std::string& outError, RHIShaderCompileOptions options)
{
    const RHIShaderCompileRequest request{name, entryPoint, targetProfile, output, &permutation, std::move(options)};
    return Compiler().Verify(request, outShader, outError);
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

RHIShaderCompiler::Progress RHIShaderCompiler::GetProgress()
{
    std::lock_guard guard(g_progressMutex);
    return g_progress;
}

void RHIShaderCompiler::ClearMemoryCache()
{
    std::lock_guard<std::mutex> guard(g_cacheMutex);
    g_memoryCache.clear();
    g_verifiedCache.clear();
}
