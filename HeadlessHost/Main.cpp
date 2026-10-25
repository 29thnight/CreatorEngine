#include "../Engine/Utility_Framework/Foundation/StandardLibrary.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../Tools/build/ModuleAdapters/Creator.Platform.Windows.Strings.h"
#else
import Creator.Platform.Windows.Strings;
#endif
#include "../Engine/SceneRuntime/Headless/HeadlessScene.h"
#include <windows.h>
#include <psapi.h>

namespace
{
    std::atomic_bool stopping = false;

    BOOL WINAPI ConsoleSignal(DWORD signal)
    {
        if (signal != CTRL_C_EVENT && signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT) return FALSE;
        stopping.store(true);
        return TRUE;
    }

    class Observation final : public Runtime::ISceneEntityObserver, public Runtime::IComponentWriteObserver
    {
    public:
        std::uint64_t created = 0, destroyed = 0, writes = 0, invalidWrites = 0, scriptWrites = 0, physicsWrites = 0;
        void OnEntityChanged(const Runtime::SceneEntityEvent& event) override
        {
            if (event.change == Runtime::EntityChange::Created) ++created;
            if (event.change == Runtime::EntityChange::Destroyed) ++destroyed;
        }
        void OnComponentWritten(const Runtime::ComponentWriteEvent& event) override
        {
            ++writes;
            if (!event.entity || !event.component || !event.owner.generation) ++invalidWrites;
            if (event.source == Meta::PropertyChangeSource::Script) ++scriptWrites;
            if (event.source == Meta::PropertyChangeSource::Physics) ++physicsWrites;
        }
    };

    std::vector<std::filesystem::path> LoadedModules()
    {
        std::vector<HMODULE> modules(256);
        DWORD bytes = 0;
        if (!EnumProcessModules(GetCurrentProcess(), modules.data(), static_cast<DWORD>(modules.size()*sizeof(HMODULE)), &bytes))
            throw std::runtime_error("Module inventory failed");
        if (bytes > modules.size()*sizeof(HMODULE)) throw std::runtime_error("Module inventory exceeded capacity");
        std::vector<std::filesystem::path> result;
        for (std::size_t i = 0; i < bytes / sizeof(HMODULE); ++i)
        {
            std::vector<wchar_t> path(32768);
            const auto size = GetModuleFileNameW(modules[i], path.data(), static_cast<DWORD>(path.size()));
            if (!size || size == path.size()) throw std::runtime_error("Module path inventory failed");
            result.emplace_back(std::wstring(path.data(), size));
        }
        return result;
    }

    std::string Utf8(std::wstring_view text)
    {
        std::string result;
        if (!ce::strings::try_wide_to_utf8(text, result))
        {
            throw std::invalid_argument("Invalid Unicode option");
        }
        return result;
    }

    template<class T> T Number(std::string_view text)
    {
        T value{};
        const auto result = std::from_chars(text.data(), text.data()+text.size(), value);
        if (result.ec != std::errc{} || result.ptr != text.data()+text.size()) throw std::invalid_argument("Invalid numeric option");
        return value;
    }
}

int wmain(int argc, wchar_t** argv)
{
    Diagnostics::Document report;
    auto root = report.Root(); root.SetMap();
    root["schemaVersion"] << 1;
    root["processId"] << static_cast<std::uint32_t>(GetCurrentProcessId());
    root["runtime"] << "shared-scene-renderless";
    std::filesystem::path reportPath;
    std::unique_ptr<Runtime::Headless::Session> scene;
    auto observation = std::make_shared<Observation>();
    std::string inspectType, inspectMethod;
    int exit = 1;
    try
    {
        std::filesystem::path cooked, managed;
        std::uint64_t frames = 0, transferAt = 0;
        double frameSeconds = 0;
        for (int i = 1; i < argc; ++i)
        {
            const std::wstring_view name(argv[i]);
            if (i+1 == argc) throw std::invalid_argument("Missing option value");
            const std::wstring value(argv[++i]);
            if (name == L"--scene") cooked = std::filesystem::path(value);
            else if (name == L"--managed") managed = std::filesystem::path(value);
            else if (name == L"--report") reportPath = std::filesystem::path(value);
            else if (name == L"--transfer-at-frame") transferAt = Number<std::uint64_t>(Utf8(value));
            else if (name == L"--frames") frames = Number<std::uint64_t>(Utf8(value));
            else if (name == L"--frame-seconds") frameSeconds = Number<double>(Utf8(value));
            else if (name == L"--inspect-type") inspectType = Utf8(value);
            else if (name == L"--inspect-method") inspectMethod = Utf8(value);
            else throw std::invalid_argument("Unknown headless option");
        }
        if (cooked.empty() || managed.empty() || reportPath.empty() || frames > 10'000'000
            || !std::isfinite(frameSeconds) || frameSeconds < 0 || frameSeconds > 1)
            throw std::invalid_argument("Invalid headless host arguments");
        if (!SetConsoleCtrlHandler(ConsoleSignal, TRUE)) throw std::runtime_error("Console signal setup failed");

        scene = std::make_unique<Runtime::Headless::Session>();
        scene->Observations().Subscribe(std::static_pointer_cast<Runtime::ISceneEntityObserver>(observation));
        scene->Observations().Subscribe(std::static_pointer_cast<Runtime::IComponentWriteObserver>(observation));
        scene->Load(cooked);
        scene->Start(managed);
        auto previous = std::chrono::steady_clock::now();
        for (std::uint64_t frame = 0; !stopping.load() && (!frames || frame < frames); ++frame)
        {
            double elapsed = frameSeconds;
            if (!elapsed)
            {
                std::this_thread::sleep_until(previous + std::chrono::milliseconds(1));
                const auto now = std::chrono::steady_clock::now();
                elapsed = std::chrono::duration<double>(now-previous).count();
                previous = now;
            }
            scene->Frame(elapsed);
            if (transferAt && frame + 1 == transferAt) scene->TransferPersistentEntity("Mover");
        }
        // The same six lifecycle phases complete before physics and native
        // owners retire. Inspecting static author state afterward proves teardown.
        scene->Stop();
        if (scene->ManagedErrors()) throw std::runtime_error("Managed script errors were recorded");
        root["syntheticFrameDelta"] << (frameSeconds > 0);
        root["result"] << "HEADLESS_SCENE_OK";
        exit = 0;
    }
    catch (const std::exception& exception)
    {
        root["result"] << "HEADLESS_SCENE_FAILED";
        root["error"] << exception.what();
        std::fprintf(stderr, "HeadlessHost: %s\n", exception.what());
    }
    if (scene)
    {
        try { scene->Stop(); }
        catch (const std::exception& error) { root["cleanupError"] << error.what(); exit = 1; }
        if (!inspectType.empty())
        {
            try { root["scriptState"] << scene->Inspect(inspectType, inspectMethod); }
            catch (const std::exception& error) { root["inspectionError"] << error.what(); exit = 1; }
        }
        scene->WriteReport(root["scene"]);
    }
    root["observations"].SetMap();
    root["observations"]["created"] << observation->created;
    root["observations"]["destroyed"] << observation->destroyed;
    root["observations"]["writes"] << observation->writes;
    root["observations"]["invalidWrites"] << observation->invalidWrites;
    root["observations"]["scriptWrites"] << observation->scriptWrites;
    root["observations"]["physicsWrites"] << observation->physicsWrites;
    if (observation->invalidWrites) exit = 1;
    const auto resources = ce::physics::read_resource_statistics();
    root["physicsResources"].SetMap();
    root["physicsResources"]["enabled"] << resources.enabled;
    root["physicsResources"]["balanced"] << resources.balanced();
    root["modules"].SetSequence();
    try
    {
        for (const auto& module : LoadedModules())
        {
            const auto path = module.u8string();
            root["modules"].Append() << std::string(reinterpret_cast<const char*>(path.data()), path.size());
        }
    }
    catch (const std::exception& error) { root["inventoryError"] << error.what(); exit = 1; }
    if (exit) root["result"] << "HEADLESS_SCENE_FAILED";
    root["exitCode"] << exit;
    if (!reportPath.empty())
    {
        std::ofstream output(reportPath, std::ios::binary);
        const auto text = report.ToJson();
        if (!output.write(text.data(), text.size())) return 2;
    }
    return exit;
}
