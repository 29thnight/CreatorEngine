#include "PrsmScriptHost.h"
#include "PrsmBindingContext.h"
#include "PrsmBindingRegistry.h"

// PrSM 런타임(ThirdParty/PrSM, PRSM_HOST_BUILD) — 이 번역 단위 밖으로 새지 않는다.
#include "gc_runtime.h"
#include "handle_table.h"
#include "prsm_abi.h"
#include "prsm_jit.h"
#include "prsm_reload_bridge.h"
#include "prsm_task_bridge.h"
#include "task_bridge.h"
#include "task_scheduler.h"


#include "Entity.h"
#include "LogSystem.h"
#include "PathFinder.h"
#include "ProfileScope.h"
#include "ScriptObjectRegistry.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <unordered_map>

#include <windows.h>

namespace
{
    constexpr std::string_view kLogTag = "[PrSM] ";
    constexpr DWORD kCompileTimeoutMs = 120'000;

    void PrsmLog(spdlog::level::level_enum level, const std::string& message)
    {
        Debug::PrintLog(level, std::string(kLogTag) + message);
    }

    std::string Text(CeStr value)
    {
        return (nullptr != value.ptr && 0 != value.len) ? std::string(value.ptr, value.len) : std::string{};
    }

    std::string ReadFile(const file::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        std::ostringstream buffer;
        buffer << stream.rdbuf();
        return buffer.str();
    }

    // prsmc 를 자식 프로세스로 돌린다. 컴파일러 결함이 에디터를 죽이지 않게 하는 경계다.
    // stdout·stderr 를 파일 하나로 모아 진단으로 돌려준다.
    bool RunCompiler(const std::wstring& commandLine, const file::path& logPath, std::string& output)
    {
        SECURITY_ATTRIBUTES inherit{ sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
        HANDLE log = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (INVALID_HANDLE_VALUE == log)
        {
            output = "cannot create compiler log " + logPath.string();
            return false;
        }

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = log;
        startup.hStdError = log;
        PROCESS_INFORMATION process{};
        std::wstring mutableLine = commandLine;
        const BOOL created = CreateProcessW(nullptr, mutableLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        CloseHandle(log);
        if (!created)
        {
            output = "cannot start prsmc (error " + std::to_string(GetLastError()) + ")";
            return false;
        }

        const DWORD waited = WaitForSingleObject(process.hProcess, kCompileTimeoutMs);
        DWORD exitCode = 1;
        if (WAIT_OBJECT_0 != waited)
        {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
            output = "prsmc timed out\n";
        }
        GetExitCodeProcess(process.hProcess, &exitCode);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        output += ReadFile(logPath);
        return WAIT_OBJECT_0 == waited && 0 == exitCode;
    }

    std::wstring Quote(const file::path& path)
    {
        return L"\"" + path.wstring() + L"\"";
    }

    // project.json 이 없을 때 한 번 만든다. 언어 규칙은 디렉터리를 훑지 않으므로(D-034)
    // 소스 목록은 이 파일이 정본이고, 이것은 그 파일을 처음 쓰는 에디터 도구 동작이다.
    bool EnsureProjectManifest(const file::path& root, std::string& diagnostics)
    {
        const file::path manifest = root / L"project.json";
        if (file::exists(manifest))
        {
            return true;
        }
        std::vector<std::string> sources;
        for (const auto& entry : file::recursive_directory_iterator(root))
        {
            if (entry.is_regular_file() && entry.path().extension() == L".prsm")
            {
                auto relative = entry.path().lexically_relative(root).generic_u8string();
                sources.emplace_back(relative.begin(), relative.end());
            }
        }
        if (sources.empty())
        {
            return false;
        }
        std::sort(sources.begin(), sources.end());
        std::ostringstream json;
        json << "{\n  \"format\": \"prsm-project\",\n  \"version\": 1,\n  \"sources\": [";
        for (size_t i = 0; i < sources.size(); ++i)
        {
            json << (i ? ", " : "") << '"' << sources[i] << '"';
        }
        json << "]\n}\n";
        std::ofstream(manifest, std::ios::binary) << json.str();
        diagnostics += "created " + manifest.string() + " listing " + std::to_string(sources.size()) + " source(s)\n";
        return true;
    }

    PrsmOperation ToOperation(PrsmLifecycle phase)
    {
        switch (phase)
        {
        case PrsmLifecycle::Initialized:       return PRSM_OP_INITIALIZED;
        case PrsmLifecycle::AddedToScene:      return PRSM_OP_ADDED_TO_SCENE;
        case PrsmLifecycle::Enable:            return PRSM_OP_ENABLE;
        case PrsmLifecycle::BeginSimulation:   return PRSM_OP_BEGIN_SIMULATION;
        case PrsmLifecycle::EndSimulation:     return PRSM_OP_END_SIMULATION;
        case PrsmLifecycle::Disable:           return PRSM_OP_DISABLE;
        case PrsmLifecycle::RemovingFromScene: return PRSM_OP_REMOVING_FROM_SCENE;
        case PrsmLifecycle::Uninitializing:    return PRSM_OP_UNINITIALIZING;
        }
        return PRSM_OP_INITIALIZED;
    }

    // 직렬화 스칼라의 저장 폭(바이트). 지원하지 않는 타입은 0.
    std::uint32_t ScalarWidth(std::uint32_t type)
    {
        switch (type)
        {
        case PRSM_T_BOOL: case PRSM_T_INT8: case PRSM_T_UINT8: return 1;
        case PRSM_T_INT16: case PRSM_T_UINT16: return 2;
        case PRSM_T_INT32: case PRSM_T_UINT32: case PRSM_T_FLOAT32: case PRSM_T_CHAR: return 4;
        case PRSM_T_INT64: case PRSM_T_UINT64: case PRSM_T_FLOAT64: return 8;
        default: return 0;
        }
    }

    bool IsFloatType(std::uint32_t type)
    {
        return PRSM_T_FLOAT32 == type || PRSM_T_FLOAT64 == type;
    }

    // 정확한 비트가 그 타입의 정규 값인가(bool 은 0/1, char 는 유니코드 스칼라).
    bool IsCanonical(std::uint32_t type, std::uint64_t bits)
    {
        if (PRSM_T_BOOL == type) return bits <= 1;
        if (PRSM_T_CHAR == type) return bits <= 0x10FFFF && (bits < 0xD800 || bits > 0xDFFF);
        const std::uint32_t width = ScalarWidth(type);
        return 8 == width || bits < (std::uint64_t{ 1 } << (width * 8));
    }

    struct SavedField
    {
        std::uint32_t type = 0;
        std::uint64_t bits = 0;
    };

    std::map<std::string, SavedField, std::less<>> ParseFields(const std::vector<std::string>& fieldData)
    {
        std::map<std::string, SavedField, std::less<>> parsed;
        for (const std::string& entry : fieldData)
        {
            const size_t first = entry.find('|');
            const size_t second = std::string::npos == first ? first : entry.find('|', first + 1);
            if (std::string::npos == second) continue;
            SavedField field{};
            const char* begin = entry.data();
            if (std::from_chars(begin, begin + first, field.type).ec != std::errc{}) continue;
            if (std::from_chars(begin + first + 1, begin + second, field.bits, 16).ec != std::errc{}) continue;
            parsed.insert_or_assign(entry.substr(second + 1), field);
        }
        return parsed;
    }
} // namespace

struct PrsmScriptHost::Impl
{
    struct Slot
    {
        std::uint32_t generation = 1;
        bool live = false;
        bool failed = false;
        bool ticking = false;
        const PrsmTypeDesc* type = nullptr;
        void* object = nullptr;
        CeRoot root{};
        CeHandle handle{};
        Entity* owner = nullptr;
        std::uint64_t ownerHandle = 0;
    };

    // ── 도구 ──
    HMODULE jitLibrary = nullptr;
    HMODULE aotLibrary = nullptr;   // Player: 패키지의 모듈 DLL(세대 하나, 교체 없음)
    PrsmJitLoadModuleFn jitLoad = nullptr;
    PrsmJitModuleGetFn jitModuleGet = nullptr;
    PrsmJitUnloadModuleFn jitUnload = nullptr;
    file::path toolRoot;
    file::path sourceRoot;
    file::path cacheRoot;

    // ── 현재 세대 ──
    PrsmJitModule* jitModule = nullptr;
    const PrsmModule* table = nullptr;
    std::unique_ptr<prsm::host::TaskScheduler> scheduler;
    std::unordered_map<std::string, const PrsmTypeDesc*> types;

    std::vector<Slot> slots;
    std::vector<std::uint32_t> freeSlots;
    std::unordered_map<const void*, std::uint32_t> slotOfObject;
    std::size_t liveCount = 0;

    std::string diagnostics;
    bool initialized = false;

    Slot* Find(PrsmInstanceId id)
    {
        if (!id.IsValid() || id.index >= slots.size()) return nullptr;
        Slot& slot = slots[id.index];
        return slot.live && slot.generation == id.generation ? &slot : nullptr;
    }
    const Slot* Find(PrsmInstanceId id) const { return const_cast<Impl*>(this)->Find(id); }

    // 실패한 컴포넌트를 격리한다: 이후 훅·틱 없음, 남은 태스크는 사용자 정리 없이 내린다.
    void Fail(Slot& slot, std::string_view what)
    {
        if (slot.failed) return;
        slot.failed = true;
        slot.ticking = false;
        if (scheduler) scheduler->FailOwner(slot.object);
        PrsmLog(spdlog::level::err, std::string(slot.type ? Text(slot.type->name) : "?") + " 격리됨 — " + std::string(what));
    }

    // 엔진 → 스크립트 진입 하나. GC 는 이 범위 안에서 돌지 않는다.
    bool Invoke(Slot& slot, PrsmOperation operation, float dt, void** frame = nullptr)
    {
        if (slot.failed || nullptr == slot.type->checked_invoke) return !slot.failed;
        PrsmCallStatus status = PRSM_CALL_FAILED;
        {
            prsm_engine::CallScope context(slot.owner, slot.ownerHandle);
            prsm::host::ScriptScope active(table);
            status = slot.type->checked_invoke(slot.object, operation, dt, frame);
        }
        if (PRSM_CALL_OK != status)
        {
            Fail(slot, "operation " + std::to_string(static_cast<int>(operation)) + " 실패");
            return false;
        }
        return true;
    }

    std::vector<PrsmJitSymbol> HostSymbols() const
    {
        std::vector<PrsmJitSymbol> symbols{
            { "ce_gc_alloc", reinterpret_cast<const void*>(&ce_gc_alloc) },
            { "ce_gc_alloc_bytes", reinterpret_cast<const void*>(&ce_gc_alloc_bytes) },
            { "ce_gc_store", reinterpret_cast<const void*>(&ce_gc_store) },
            { "ce_gc_regray", reinterpret_cast<const void*>(&ce_gc_regray) },
            { "ce_gc_root_create", reinterpret_cast<const void*>(&ce_gc_root_create) },
            { "ce_gc_root_destroy", reinterpret_cast<const void*>(&ce_gc_root_destroy) },
            { "ce_gc_trace_ref", reinterpret_cast<const void*>(&ce_gc_trace_ref) },
            { "ce_handle_is_alive", reinterpret_cast<const void*>(&ce_handle_is_alive) },
            { "ce_handle_script_instance", reinterpret_cast<const void*>(&ce_handle_script_instance) },
            { "ce_panic", reinterpret_cast<const void*>(&ce_panic) },
            { "ce_report_failure", reinterpret_cast<const void*>(&ce_report_failure) },
            { "ce_log", reinterpret_cast<const void*>(&ce_log) },
            { "prsm_host_current_task", reinterpret_cast<const void*>(&prsm_host_current_task) },
            { "prsm_host_wait_frames", reinterpret_cast<const void*>(&prsm_host_wait_frames) },
            { "prsm_host_task_start", reinterpret_cast<const void*>(&prsm_host_task_start) },
            { "prsm_host_task_status", reinterpret_cast<const void*>(&prsm_host_task_status) },
            { "prsm_host_group_create", reinterpret_cast<const void*>(&prsm_host_group_create) },
            { "prsm_host_group_add", reinterpret_cast<const void*>(&prsm_host_group_add) },
            { "prsm_host_group_seal", reinterpret_cast<const void*>(&prsm_host_group_seal) },
            { "prsm_host_group_ready", reinterpret_cast<const void*>(&prsm_host_group_ready) },
            { "prsm_host_group_winner", reinterpret_cast<const void*>(&prsm_host_group_winner) },
            { "prsm_host_branch_fail", reinterpret_cast<const void*>(&prsm_host_branch_fail) },
            { "prsm_host_group_error", reinterpret_cast<const void*>(&prsm_host_group_error) },
            { "prsm_host_reload_validate_object", reinterpret_cast<const void*>(&prsm_host_reload_validate_object) },
            { "prsm_host_reload_validate_restore", reinterpret_cast<const void*>(&prsm_host_reload_validate_restore) },
            { "ce_prsm_validate_binding", reinterpret_cast<const void*>(&ce_prsm_validate_binding) },
        };
        // 엔진 바인딩은 등록소가 안다 — 이 호스트는 어느 엔진 클래스가 열려 있는지 모른다.
        for (const prsm_engine::BindingSymbol& binding : prsm_engine::BindingRegistry::Get().Symbols())
        {
            symbols.push_back({ binding.name, binding.address });
        }
        return symbols;
    }

    bool LoadToolchain()
    {
        toolRoot = PathFinder::RelativeToExecutable("..\\PrSM").lexically_normal();
        const file::path jitPath = toolRoot / L"prsm_jit.dll";
        if (!file::exists(jitPath) || !file::exists(toolRoot / L"prsmc.exe"))
        {
            diagnostics = "PrSM toolchain missing in " + toolRoot.string();
            return false;
        }
        // prsm_jit.dll 은 같은 폴더의 LLVM-C.dll 에 의존한다.
        jitLibrary = LoadLibraryExW(jitPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (nullptr == jitLibrary)
        {
            diagnostics = "cannot load " + jitPath.string() + " (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        const auto version = reinterpret_cast<PrsmJitApiVersionFn>(GetProcAddress(jitLibrary, "prsm_jit_api_version"));
        jitLoad = reinterpret_cast<PrsmJitLoadModuleFn>(GetProcAddress(jitLibrary, "prsm_jit_load_module"));
        jitModuleGet = reinterpret_cast<PrsmJitModuleGetFn>(GetProcAddress(jitLibrary, "prsm_jit_module_get"));
        jitUnload = reinterpret_cast<PrsmJitUnloadModuleFn>(GetProcAddress(jitLibrary, "prsm_jit_unload_module"));
        if (!version || PRSM_JIT_API_VERSION != version() || !jitLoad || !jitModuleGet || !jitUnload)
        {
            diagnostics = "prsm_jit.dll does not implement PrSM JIT API version " + std::to_string(PRSM_JIT_API_VERSION);
            FreeLibrary(jitLibrary);
            jitLibrary = nullptr;
            return false;
        }
        return true;
    }

    // 컴파일 → JIT → 등록. 실패하면 아무 상태도 바꾸지 않는다.
    bool BuildGeneration()
    {
        diagnostics.clear();
        if (!file::exists(sourceRoot) || !EnsureProjectManifest(sourceRoot, diagnostics))
        {
            diagnostics += "no PrSM sources under " + sourceRoot.string();
            return false;
        }
        file::create_directories(cacheRoot);
        const file::path ir = cacheRoot / L"module.ll";
        file::remove(ir);
        // 배치된 바인딩 카탈로그(네이티브 모듈마다 하나)를 전부 넘긴다. 이름 순으로 정해 결정적이다.
        std::vector<file::path> catalogs;
        for (const auto& entry : file::directory_iterator(toolRoot))
        {
            const std::wstring name = entry.path().filename().wstring();
            if (entry.is_regular_file() && name.ends_with(L".bindings.json"))
            {
                catalogs.push_back(entry.path());
            }
        }
        std::sort(catalogs.begin(), catalogs.end());
        std::wstring command = Quote(toolRoot / L"prsmc.exe") + L" emit-module-ir --project "
            + Quote(sourceRoot / L"project.json");
        for (const file::path& catalog : catalogs)
        {
            command += L" --bindings " + Quote(catalog);
        }
        command += L" --cache-dir " + Quote(cacheRoot / L"build") + L" --color never -o " + Quote(ir);
        std::string output;
        const bool compiled = RunCompiler(command, cacheRoot / L"prsmc.log", output);
        diagnostics += output;
        if (!compiled || !file::exists(ir))
        {
            return false;
        }

        const std::string text = ReadFile(ir);
        const std::vector<PrsmJitSymbol> symbols = HostSymbols();
        char error[2048]{};
        PrsmJitModule* module = nullptr;
        const std::int32_t status = jitLoad(text.data(), text.size(), symbols.data(),
            static_cast<std::uint32_t>(symbols.size()), &module, error, sizeof(error));
        if (PRSM_JIT_OK != status || nullptr == module)
        {
            diagnostics += "JIT rejected the module (status " + std::to_string(status) + "): " + error;
            return false;
        }
        const PrsmModuleGetFn get = jitModuleGet(module);
        const PrsmModule* candidate = get ? get() : nullptr;
        if (!ValidTable(candidate))
        {
            diagnostics += "generated module has an invalid table";
            jitUnload(module);
            return false;
        }
        jitModule = module;
        Activate(*candidate);
        return true;
    }

    // JIT 세대와 AOT 모듈이 같은 검사를 지난다(harness loader 의 표 검사와 같은 조건).
    static bool ValidTable(const PrsmModule* candidate)
    {
        if (nullptr == candidate || PRSM_ABI_VERSION != candidate->abi_version || candidate->type_count > 65536
            || (candidate->type_count && nullptr == candidate->types)
            || (!!candidate->task_resume != !!candidate->task_cancel))
        {
            return false;
        }
        for (std::uint32_t i = 0; i < candidate->type_count; ++i)
        {
            const PrsmTypeDesc* type = candidate->types[i];
            if (nullptr == type || 0 == type->size || 0 == type->align || 0 != (type->align & (type->align - 1))
                || type->align > 4096 || (type->ref_bitmap_words && nullptr == type->ref_bitmap))
            {
                return false;
            }
        }
        return true;
    }

    // Player: 패키징이 링크한 모듈 DLL. 런타임 DLL 의 export 를 import 로 부르므로 같은
    // 프로세스의 그 DLL 에 묶인다. LLVM 은 필요 없다(D-048).
    bool LoadAotModule(const file::path& modulePath)
    {
        diagnostics.clear();
        aotLibrary = LoadLibraryExW(modulePath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (nullptr == aotLibrary)
        {
            diagnostics = "cannot load " + modulePath.string() + " (error " + std::to_string(GetLastError()) + ")";
            return false;
        }
        const auto get = reinterpret_cast<PrsmModuleGetFn>(GetProcAddress(aotLibrary, PRSM_MODULE_GET_SYMBOL));
        const PrsmModule* candidate = get ? get() : nullptr;
        if (!ValidTable(candidate))
        {
            diagnostics = modulePath.string() + " has no valid " + PRSM_MODULE_GET_SYMBOL + " table (ABI "
                + std::to_string(PRSM_ABI_VERSION) + " required)";
            FreeLibrary(aotLibrary);
            aotLibrary = nullptr;
            return false;
        }
        Activate(*candidate);
        return true;
    }

    void Activate(const PrsmModule& candidate)
    {
        prsm::host::GcRuntime::Instance().RegisterModule(candidate);
        table = &candidate;
        scheduler = std::make_unique<prsm::host::TaskScheduler>(*table, [this](void* owner) {
            const auto found = slotOfObject.find(owner);
            if (found != slotOfObject.end())
            {
                Fail(slots[found->second], "코루틴 실패");
            }
        });
        types.clear();
        for (std::uint32_t i = 0; i < table->type_count; ++i)
        {
            const PrsmTypeDesc* type = table->types[i];
            if (nullptr != type && 0 != type->base_name.len)
            {
                types.emplace(Text(type->name), type);
            }
        }
    }

    // 세대를 내린다. GC 가 그 모듈 객체를 전부 회수한 뒤에만 코드를 놓는다.
    void ReleaseGeneration()
    {
        if (nullptr == table) return;
        if (scheduler)
        {
            scheduler->CancelAll();
            for (int i = 0; i < 1024 && scheduler->LiveTaskCount(); ++i)
            {
                prsm_engine::CallScope polling(nullptr, 0);
                scheduler->Poll(0.0);
            }
            scheduler.reset();
        }
        auto& gc = prsm::host::GcRuntime::Instance();
        gc.CollectFull();
        if (gc.DrainModule(*table))
        {
            if (nullptr != jitModule) jitUnload(jitModule);
            if (nullptr != aotLibrary) FreeLibrary(aotLibrary);
        }
        else
        {
            // 아직 모듈 객체를 잡은 참조가 있다 — 코드를 남겨 두는 편이 안전하다(D-047).
            PrsmLog(spdlog::level::warn, "모듈 객체가 남아 스크립트 코드를 회수하지 않았습니다");
        }
        jitModule = nullptr;
        aotLibrary = nullptr;
        table = nullptr;
        types.clear();
    }
};

PrsmScriptHost& PrsmScriptHost::Get()
{
    static PrsmScriptHost instance;
    return instance;
}

PrsmScriptHost::PrsmScriptHost() : m_impl(std::make_unique<Impl>()) {}
PrsmScriptHost::~PrsmScriptHost() = default;

bool PrsmScriptHost::Initialize()
{
    Impl& impl = *m_impl;
    if (impl.initialized) return IsReady();
    impl.initialized = true;

    // 게임 스레드를 묶고, PrSM GC 런타임을 이 스레드에서 처음 만든다(소유 스레드 규약).
    prsm_engine::BindGameThread();
    (void)prsm::host::GcRuntime::Instance();

    impl.sourceRoot = PathFinder::Relative("Scripts\\PrSM").lexically_normal();
    impl.cacheRoot = PathFinder::CachePath("PrSM").lexically_normal();
    if (!impl.LoadToolchain())
    {
        PrsmLog(spdlog::level::warn, impl.diagnostics + " — PrSM 스크립트 없이 시작합니다");
        return false;
    }
    if (!impl.BuildGeneration())
    {
        PrsmLog(spdlog::level::warn, "컴파일·적재 실패:\n" + impl.diagnostics);
        return false;
    }
    PrsmLog(spdlog::level::info, "스크립트 계층 준비 완료 — 컴포넌트 타입 " + std::to_string(impl.types.size()) + "개");
    return true;
}

bool PrsmScriptHost::InitializeAot(const std::wstring& modulePath)
{
    Impl& impl = *m_impl;
    if (impl.initialized) return IsReady();
    impl.initialized = true;

    prsm_engine::BindGameThread();
    (void)prsm::host::GcRuntime::Instance();

    const file::path path = file::path(modulePath).lexically_normal();
    if (!file::exists(path))
    {
        // PrSM 스크립트가 없는 프로젝트의 정상 경로다.
        PrsmLog(spdlog::level::info, "AOT 모듈이 없어 PrSM 스크립트 없이 시작합니다: " + path.string());
        return false;
    }
    if (!impl.LoadAotModule(path))
    {
        PrsmLog(spdlog::level::err, "AOT 모듈 적재 실패: " + impl.diagnostics);
        return false;
    }
    PrsmLog(spdlog::level::info, "AOT 스크립트 계층 준비 완료 — 컴포넌트 타입 " + std::to_string(impl.types.size()) + "개");
    // 패키징 검증(PlayerVerification)과 회귀 게이트가 표준 출력에서 찾는 표지.
    std::printf("[PRSM_AOT] loaded types=%zu\n", impl.types.size());
    std::fflush(stdout);
    return true;
}

bool PrsmScriptHost::Rebuild()
{
    Impl& impl = *m_impl;
    if (nullptr == impl.jitLibrary) return false;
    if (0 != impl.liveCount)
    {
        PrsmLog(spdlog::level::warn, "살아 있는 PrSM 인스턴스가 있어 재컴파일을 미룹니다(재생을 멈춘 뒤 다시 시도)");
        return false;
    }
    impl.ReleaseGeneration();
    const bool built = impl.BuildGeneration();
    PrsmLog(built ? spdlog::level::info : spdlog::level::err,
        built ? "재컴파일 완료" : "재컴파일 실패:\n" + impl.diagnostics);
    return built;
}

void PrsmScriptHost::Shutdown()
{
    Impl& impl = *m_impl;
    for (std::uint32_t i = 0; i < impl.slots.size(); ++i)
    {
        if (impl.slots[i].live) Destroy({ i, impl.slots[i].generation });
    }
    impl.ReleaseGeneration();
    if (nullptr != impl.jitLibrary && nullptr == impl.jitModule)
    {
        FreeLibrary(impl.jitLibrary);
        impl.jitLibrary = nullptr;
    }
}

bool PrsmScriptHost::IsReady() const { return nullptr != m_impl->table; }
const std::string& PrsmScriptHost::LastDiagnostics() const { return m_impl->diagnostics; }

std::vector<std::string> PrsmScriptHost::ComponentTypeNames() const
{
    std::vector<std::string> names;
    for (const auto& [name, type] : m_impl->types) names.push_back(name);
    std::sort(names.begin(), names.end());
    return names;
}

bool PrsmScriptHost::HasComponentType(std::string_view name) const
{
    return m_impl->types.contains(std::string(name));
}

PrsmInstanceId PrsmScriptHost::Create(Entity& owner, std::string_view typeName, const std::vector<std::string>& fieldData)
{
    Impl& impl = *m_impl;
    if (!IsReady()) return {};
    const auto found = impl.types.find(std::string(typeName));
    if (found == impl.types.end())
    {
        PrsmLog(spdlog::level::err, "등록되지 않은 컴포넌트 타입: " + std::string(typeName));
        return {};
    }
    const PrsmTypeDesc* type = found->second;

    void* object = nullptr;
    {
        prsm::host::ScriptScope active(impl.table);
        object = ce_gc_alloc(type);
    }
    if (nullptr == object)
    {
        PrsmLog(spdlog::level::err, "GC 할당 한도에 걸려 " + std::string(typeName) + " 을 만들지 못했습니다");
        return {};
    }

    std::uint32_t index;
    if (!impl.freeSlots.empty())
    {
        index = impl.freeSlots.back();
        impl.freeSlots.pop_back();
    }
    else
    {
        index = static_cast<std::uint32_t>(impl.slots.size());
        impl.slots.emplace_back();
    }
    Impl::Slot& slot = impl.slots[index];
    slot.live = true;
    slot.failed = false;
    slot.ticking = false;
    slot.type = type;
    slot.object = object;
    slot.root = ce_gc_root_create(object);
    slot.handle = prsm::host::HandleTable::Instance().Create(object);
    slot.owner = &owner;
    const ScriptObjectHandle ownerHandle = ScriptObjectRegistry::Get().Register(&owner);
    slot.ownerHandle = prsm_engine::EncodeEntityHandle(ownerHandle.index, ownerHandle.generation);
    impl.slotOfObject[object] = index;
    ++impl.liveCount;

    // 기본값 → 저장값 복원 → (OnInitialized 는 호출자가) 순서(D-041).
    if (!impl.Invoke(slot, PRSM_OP_CONSTRUCT, 0.0f))
    {
        return { index, slot.generation };
    }
    const auto saved = ParseFields(fieldData);
    for (std::uint32_t i = 0; i < type->field_count; ++i)
    {
        const PrsmFieldDesc& field = type->fields[i];
        if (0 == (field.flags & PRSM_FIELD_SERIALIZE)) continue;
        const auto value = saved.find(Text(field.name));
        if (value == saved.end()) continue;   // 새 필드 — 기본값 유지
        const std::uint32_t width = ScalarWidth(field.type_id);
        if (0 == width) continue;
        if (value->second.type != field.type_id)
        {
            // 부동소수가 낀 타입 변경은 새 기본값을 쓰고, 그 밖의 스칼라 변경은 거부한다(D-035).
            if (!IsFloatType(value->second.type) && !IsFloatType(field.type_id))
            {
                PrsmLog(spdlog::level::warn, Text(type->name) + "." + Text(field.name) + " 의 저장 타입이 달라 복원하지 않았습니다");
            }
            continue;
        }
        if (!IsCanonical(field.type_id, value->second.bits))
        {
            PrsmLog(spdlog::level::warn, Text(type->name) + "." + Text(field.name) + " 의 저장 값이 타입 범위 밖이라 버렸습니다");
            continue;
        }
        std::memcpy(static_cast<std::byte*>(object) + field.offset, &value->second.bits, width);
    }
    return { index, slot.generation };
}

bool PrsmScriptHost::Dispatch(PrsmInstanceId id, PrsmLifecycle phase)
{
    Impl& impl = *m_impl;
    Impl::Slot* slot = impl.Find(id);
    if (nullptr == slot || slot->failed) return false;

    switch (phase)
    {
    // 활성 훅은 상태 전이다 — 이미 그 상태면 다시 부르지 않는다.
    case PrsmLifecycle::Enable:
        if (slot->ticking) return true;
        if (!impl.Invoke(*slot, PRSM_OP_ENABLE, 0.0f)) return false;
        slot->ticking = true;
        return true;

    case PrsmLifecycle::Disable:
        if (!slot->ticking) return true;
        // 틱을 먼저 멈추고 OnDisable 을 부른다(D-041 의 일시정지 → OnDisable 순서).
        slot->ticking = false;
        return impl.Invoke(*slot, PRSM_OP_DISABLE, 0.0f);

    case PrsmLifecycle::BeginSimulation:
    {
        if (!impl.Invoke(*slot, PRSM_OP_BEGIN_SIMULATION, 0.0f)) return false;
        if (0 == (slot->type->hook_mask & PRSM_HOOK_ON_SIMULATE)) return true;
        void* frame = nullptr;
        if (!impl.Invoke(*slot, PRSM_OP_SIMULATE_START, 0.0f, &frame)) return false;
        if (nullptr != frame) impl.scheduler->Start(frame, slot->object);
        return true;
    }

    case PrsmLifecycle::EndSimulation:
        // 소유자의 코루틴을 먼저 취소·정리하고 종료 훅을 부른다(D-041).
        if (!impl.scheduler->DrainOwner(slot->object))
        {
            PrsmLog(spdlog::level::warn, Text(slot->type->name) + " 의 취소 정리가 다음 프레임으로 넘어갑니다");
        }
        return impl.Invoke(*slot, PRSM_OP_END_SIMULATION, 0.0f);

    default:
        return impl.Invoke(*slot, ToOperation(phase), 0.0f);
    }
}

void PrsmScriptHost::Destroy(PrsmInstanceId id)
{
    Impl& impl = *m_impl;
    Impl::Slot* slot = impl.Find(id);
    if (nullptr == slot) return;

    if (impl.scheduler && !impl.scheduler->DrainOwner(slot->object))
    {
        // 협조 취소가 끝나지 않은 채 소유자가 사라진다 — 사용자 정리 없이 내린다.
        impl.scheduler->FailOwner(slot->object);
    }
    prsm::host::HandleTable::Instance().Destroy(slot->handle);
    ce_gc_root_destroy(slot->root);
    impl.slotOfObject.erase(slot->object);
    *slot = Impl::Slot{ slot->generation + 1 == 0 ? 1u : slot->generation + 1 };
    impl.freeSlots.push_back(id.index);
    --impl.liveCount;
}

bool PrsmScriptHost::IsLive(PrsmInstanceId id) const { return nullptr != m_impl->Find(id); }

bool PrsmScriptHost::IsFailed(PrsmInstanceId id) const
{
    const Impl::Slot* slot = m_impl->Find(id);
    return nullptr == slot || slot->failed;
}

std::vector<std::string> PrsmScriptHost::CaptureFields(PrsmInstanceId id) const
{
    std::vector<std::string> captured;
    const Impl::Slot* slot = m_impl->Find(id);
    if (nullptr == slot) return captured;
    const PrsmTypeDesc& type = *slot->type;
    for (std::uint32_t i = 0; i < type.field_count; ++i)
    {
        const PrsmFieldDesc& field = type.fields[i];
        const std::uint32_t width = ScalarWidth(field.type_id);
        if (0 == (field.flags & PRSM_FIELD_SERIALIZE) || 0 == width) continue;
        std::uint64_t bits = 0;
        std::memcpy(&bits, static_cast<const std::byte*>(slot->object) + field.offset, width);
        char hex[17]{};
        const auto end = std::to_chars(hex, hex + 16, bits, 16).ptr;
        captured.push_back(std::to_string(field.type_id) + "|" + std::string(hex, end) + "|" + Text(field.name));
    }
    return captured;
}

void PrsmScriptHost::TickPrePhysics(float deltaSeconds)
{
    Impl& impl = *m_impl;
    if (!IsReady() || 0 == impl.liveCount) return;
    ce::profile_scope scope{ ce::marker<"PrSM.PrePhysics">() };
    for (Impl::Slot& slot : impl.slots)
    {
        if (slot.live && slot.ticking && (slot.type->hook_mask & PRSM_HOOK_PRE_PHYSICS))
        {
            impl.Invoke(slot, PRSM_OP_PRE_PHYSICS, deltaSeconds);
        }
    }
}

void PrsmScriptHost::TickPostPhysics(float deltaSeconds)
{
    Impl& impl = *m_impl;
    if (!IsReady()) return;
    ce::profile_scope scope{ ce::marker<"PrSM.PostPhysics">() };
    for (Impl::Slot& slot : impl.slots)
    {
        if (slot.live && slot.ticking && (slot.type->hook_mask & PRSM_HOOK_POST_PHYSICS))
        {
            impl.Invoke(slot, PRSM_OP_POST_PHYSICS, deltaSeconds);
        }
    }
    if (impl.scheduler && impl.scheduler->LiveTaskCount())
    {
        // 소유자 문맥은 실행 중인 태스크가 정한다(CurrentTaskOwnerHandle).
        prsm_engine::CallScope polling(nullptr, 0);
        impl.scheduler->Poll(deltaSeconds);
    }
    // 증분 GC 한 단계. 스크립트가 스택에 없는 안전 지점이다.
    prsm::host::GcRuntime::Instance().CollectStep();
}

std::size_t PrsmScriptHost::LiveInstanceCount() const { return m_impl->liveCount; }

std::uint64_t prsm_engine::CurrentTaskOwnerHandle() noexcept
{
    const prsm::host::TaskBridgeScope* task = prsm::host::TaskBridgeScope::Current();
    if (nullptr == task) return 0;
    auto& impl = *PrsmScriptHost::Get().m_impl;
    try
    {
        const auto found = impl.slotOfObject.find(task->scheduler.Owner(task->task));
        return found == impl.slotOfObject.end() ? 0 : impl.slots[found->second].ownerHandle;
    }
    catch (...)
    {
        return 0;
    }
}
