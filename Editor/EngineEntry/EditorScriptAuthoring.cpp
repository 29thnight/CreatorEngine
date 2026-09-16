#include "EditorEngineDistribution.h"
#include "EditorScriptAuthoring.h"
#include "EditorObjectOperations.h"
#include "EditorAssetDatabase.h"
#include "EditorComponentCatalog.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"
#include "PathFinder.h"
#include <Windows.h>
#include <array>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

namespace EditorScriptAuthoring
{
    namespace
    {
        struct Work
        {
            std::mutex mutex;
            Status status;
            EntityHandle target;
            std::filesystem::path source, log;
            HANDLE process{}, job{};
            bool compiled{};
            ~Work() { if (job) CloseHandle(job); if (process) CloseHandle(process); }
        };
        Work& State() { static Work state; return state; }
        std::string Utf8(const std::filesystem::path& path)
        {
            const auto value = path.u8string();
            return {reinterpret_cast<const char*>(value.data()), value.size()};
        }
        Entity* Resolve(EntityHandle target)
        {
            for (auto* scene : SceneManagers->GetScenes()) if (scene && scene->GetSceneId() == target.sceneId)
            {
                auto* entity = scene->Resolve(target);
                return entity && !entity->IsDestroyMark() ? entity : nullptr;
            }
            return nullptr;
        }
        std::wstring Quote(std::wstring_view argument)
        {
            std::wstring quoted = L"\"";
            size_t slashes = 0;
            for (const auto ch : argument)
            {
                if (ch == L'\\') { ++slashes; continue; }
                quoted.append(slashes * (ch == L'"' ? 2 : 1) + (ch == L'"' ? 1 : 0), L'\\');
                quoted += ch;
                slashes = 0;
            }
            quoted.append(slashes * 2, L'\\');
            return quoted + L'"';
        }
        void CloseBuild(Work& work)
        {
            // This job owns only the compiler process tree started for this request.
            if (work.job) CloseHandle(std::exchange(work.job, nullptr));
            if (work.process) CloseHandle(std::exchange(work.process, nullptr));
        }
        CommandCore::CommandResult FailWork(Work& work, const std::string& message)
        {
            CloseBuild(work);
            work.status.busy = false;
            work.status.succeeded = false;
            work.status.message = message;
            return CommandCore::Fail("script.create_failed", message);
        }
        CommandCore::CommandResult ValidateTarget(EntityHandle target)
        {
            auto* entity = Resolve(target);
            if (!entity) return CommandCore::PreconditionFailed("object.stale", "The original entity no longer exists. The script file is kept.");
            if (EditorObjectOperations::IsEditLocked(entity, true))
                return CommandCore::PreconditionFailed("object.locked", "Unlock the entity before adding a script.");
            if (SceneManagers->IsGameStart())
                return CommandCore::PreconditionFailed("script.play_mode", "Stop Play before creating or retrying a script.");
            return CommandCore::Ok("Target is editable");
        }
        CommandCore::CommandResult StartBuild(Work& work)
        {
            const auto projectRoot = PathFinder::BaseProjectPath();
            std::filesystem::path program, workingDirectory;
            std::vector<std::wstring> arguments;
            std::filesystem::create_directories(work.log.parent_path());
            if (const auto checkout = ResolveEditorSourceCheckout(); !checkout.repository.empty())
            {
                std::array<wchar_t, MAX_PATH> dotnet{};
                if (!SearchPathW(nullptr, L"dotnet.exe", nullptr, static_cast<DWORD>(dotnet.size()), dotnet.data(), nullptr))
                    return FailWork(work, "Compiling scripts in a source checkout requires the .NET SDK (dotnet.exe on PATH).");
                program = dotnet.data();
                workingDirectory = checkout.repository;
                arguments = {L"build", (checkout.repository / L"GameScripts/GameScripts.csproj").wstring(), L"-c", checkout.configuration,
                    L"--nologo", L"-v", L"quiet", L"-p:CreatorScriptSourceRoot=" + (projectRoot / L"Assets/Script").wstring(),
                    L"-flp:logfile=" + work.log.wstring() + L";verbosity=minimal"};
            }
            else
            {
                const auto distribution = ResolveEditorEngineDistribution();
                if (distribution.root.empty()) return FailWork(work, "Publish/select an engine distribution before compiling scripts.");
                program = distribution.root / L"Bin" / (L"x64-" + distribution.configuration) / L"Tools/CreatorBuildTool/CreatorBuildTool.exe";
                if (!std::filesystem::is_regular_file(program))
                    return FailWork(work, "The selected engine compiler is incomplete.");
                workingDirectory = distribution.root;
                arguments = {L"compile-game", L"-EngineDistribution", distribution.root.wstring(), L"-Project", projectRoot.wstring(),
                    L"-Config", distribution.configuration, L"-Output", PathFinder::ManagedPath("Scripts").parent_path().wstring(),
                    L"-LogPath", work.log.wstring()};
            }
            arguments.insert(arguments.begin(), program.wstring());
            std::wstring command;
            for (const auto& argument : arguments) { if (!command.empty()) command += L' '; command += Quote(argument); }
            work.job = CreateJobObjectW(nullptr, nullptr);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!work.job || !SetInformationJobObject(work.job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
                return FailWork(work, "Cannot prepare the script compiler process.");
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            startup.dwFlags = STARTF_USESHOWWINDOW;
            startup.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION process{};
            if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE,
                CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, workingDirectory.c_str(), &startup, &process))
                return FailWork(work, "Cannot start the script compiler (Windows error " + std::to_string(GetLastError()) + ").");
            work.process = process.hProcess;
            if (!AssignProcessToJobObject(work.job, work.process))
            {
                TerminateProcess(work.process, 1);
                CloseHandle(process.hThread);
                return FailWork(work, "Cannot track the script compiler process.");
            }
            const auto resumed = ResumeThread(process.hThread);
            CloseHandle(process.hThread);
            if (resumed == static_cast<DWORD>(-1)) return FailWork(work, "Cannot run the script compiler.");
            work.compiled = false;
            work.status.busy = true;
            work.status.succeeded = false;
            work.status.message = "Compiling " + work.source.filename().string() + "...";
            return CommandCore::Ok(work.status.message);
        }

        CommandCore::CommandResult ReloadNow()
        {
            using namespace CommandCore;
            auto& clr = ClrHost::Get();
            if (!clr.IsReady()) return PreconditionFailed("script.clr_not_ready", "Script runtime is not ready");
            std::vector<ScriptComponent*> scripts;
            // A reload replaces all managed instances, including multiple scripts per
            // entity and loaded scenes other than the currently selected scene.
            for (auto* scene : SceneManagers->GetScenes()) if (scene)
                for (const auto& entity : scene->m_Entities) if (entity && !entity->IsDestroyMark())
                    for (const auto& component : entity->m_components)
                        if (auto* script = dynamic_cast<ScriptComponent*>(component.get()); script && !script->IsDestroyMark())
                        {
                            script->PrepareForReload();
                            scripts.push_back(script);
                        }
            const auto outcome = clr.ReloadScripts();
            auto data = CommandData::Object();
            data.Set("total", CommandData::Int(scripts.size()));
            if (outcome == ClrHost::ReloadOutcome::PreviousKept)
            {
                data.Set("intact", CommandData::Int(scripts.size()));
                data.Set("previousAssemblyKept", CommandData::Bool(true));
                return Fail("script.reload_failed", "The new assembly was rejected. Existing scripts are unchanged.", std::move(data));
            }
            if (outcome == ClrHost::ReloadOutcome::Faulted)
            {
                data.Set("instancesTouched", CommandData::Bool(false));
                return Fail("script.reload_faulted", "Script reload failed. See Output Log.", std::move(data));
            }
            int restored = 0;
            auto failed = CommandData::Array();
            for (auto* script : scripts)
            {
                script->RestoreAfterReload();
                if (script->HasInstance()) ++restored;
                else failed.Append(CommandData::String(script->m_scriptType));
            }
            data.Set("restored", CommandData::Int(restored));
            data.Set("failed", std::move(failed));
            if (outcome == ClrHost::ReloadOutcome::PreviousLost)
            {
                data.Set("previousAssemblyKept", CommandData::Bool(false));
                return Fail("script.reload_failed", "The assembly could not be loaded. Retry after correcting the script errors.", std::move(data));
            }
            if (restored != scripts.size()) return Fail("script.reload_partial", "Some script instances could not be restored.", std::move(data));
            return Ok("Scripts reloaded", std::move(data));
        }
    }

    CommandCore::CommandResult CreateAndAttach(EntityHandle target, const std::string& name)
    {
        auto& work = State();
        std::lock_guard lock(work.mutex);
        if (work.status.busy) return CommandCore::PreconditionFailed("script.busy", "A script is already compiling.");
        auto check = ValidateTarget(target);
        if (!check.IsSuccess()) return check;
        if (name.empty() || name.size() > 100 || !(std::isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')
            || !std::ranges::all_of(name, [](unsigned char ch) { return ch < 128 && (std::isalnum(ch) || ch == '_'); }))
            return CommandCore::InvalidArguments("Use a C# class name containing letters, numbers and underscores, starting with a letter or underscore.");
        constexpr std::string_view keywords = "|abstract|as|base|bool|break|byte|case|catch|char|checked|class|const|continue|decimal|default|delegate|do|double|else|enum|event|explicit|extern|false|finally|fixed|float|for|foreach|goto|if|implicit|in|int|interface|internal|is|lock|long|namespace|new|null|object|operator|out|override|params|private|protected|public|readonly|ref|return|sbyte|sealed|short|sizeof|stackalloc|static|string|struct|switch|this|throw|true|try|typeof|uint|ulong|unchecked|unsafe|ushort|using|virtual|void|volatile|while|";
        const auto lower = editor::components::SearchKey(name);
        if (keywords.find("|" + name + "|") != std::string_view::npos || lower == "con" || lower == "prn" || lower == "aux" || lower == "nul"
            || (lower.size() == 4 && (lower.starts_with("com") || lower.starts_with("lpt")) && lower[3] >= '1' && lower[3] <= '9'))
            return CommandCore::InvalidArguments("This name is reserved. Choose another class name.");
        // ScriptGenerator registers short class names; use the same persisted key.
        const auto& typeName = name;
        const auto names = ClrHost::Get().GetComponentTypeNames();
        if (std::ranges::find(names, typeName) != names.end())
            return CommandCore::InvalidArguments("This script class already exists. Add it from Scripts or choose another name.");
        try
        {
            const auto source = PathFinder::BaseProjectPath() / L"Assets/Script" / (name + ".cs");
            std::filesystem::create_directories(source.parent_path());
            HANDLE fileHandle = CreateFileW(source.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (fileHandle == INVALID_HANDLE_VALUE) return CommandCore::InvalidArguments("Cannot create this file. It may already exist or the folder may be read-only.");
            const std::string text = "namespace CreatorEngine.Scripts;\n\npublic sealed partial class " + name + " : global::CreatorEngine.Component\n{\n    public override void OnBeginSimulation()\n    {\n    }\n}\n";
            DWORD written = 0;
            const bool saved = WriteFile(fileHandle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
            CloseHandle(fileHandle);
            if (!saved) return CommandCore::Fail("script.write_failed", "Could not write the complete script file.");
            work.target = target;
            work.source = source;
            work.log = PathFinder::RuntimeDataPath("Logs/ScriptCompilation.log");
            work.status = {false, false, {}, Utf8(source), Utf8(work.log), typeName};
            EditorAssetDatabase::Get().CreateMeta(source);
            return StartBuild(work);
        }
        catch (const std::exception& error) { return FailWork(work, error.what()); }
    }

    CommandCore::CommandResult Retry()
    {
        auto& work = State();
        std::lock_guard lock(work.mutex);
        if (work.status.busy) return CommandCore::PreconditionFailed("script.busy", "A script is already compiling.");
        auto check = ValidateTarget(work.target);
        if (!check.IsSuccess()) { work.status.message = check.message; return check; }
        try
        {
            if (!std::filesystem::is_regular_file(work.source)) return FailWork(work, "The script source no longer exists.");
            return StartBuild(work);
        }
        catch (const std::exception& error) { return FailWork(work, error.what()); }
    }
    Status GetStatus() { auto& work = State(); std::lock_guard lock(work.mutex); return work.status; }
    CommandCore::CommandResult Reload()
    {
        if (GetStatus().busy) return CommandCore::PreconditionFailed("script.busy", "Wait for script compilation to finish.");
        return ReloadNow();
    }
    void Cancel()
    {
        auto& work = State();
        std::lock_guard lock(work.mutex);
        if (work.status.busy) FailWork(work, "Compilation cancelled. The script file is kept.");
    }
    void Tick()
    {
        auto& work = State();
        std::lock_guard lock(work.mutex);
        if (!work.status.busy) return;
        if (!work.compiled)
        {
            if (!work.process || WaitForSingleObject(work.process, 0) == WAIT_TIMEOUT) return;
            DWORD exitCode = 1;
            if (!GetExitCodeProcess(work.process, &exitCode) || exitCode != 0)
            {
                FailWork(work, "Script compilation failed. Open the build log, correct the script, then retry.");
                return;
            }
            CloseBuild(work);
            work.compiled = true;
        }
        if (SceneManagers->IsGameStart())
        {
            work.status.message = "Compilation completed. Stop Play to add the script.";
            return;
        }
        auto result = ValidateTarget(work.target);
        if (result.IsSuccess()) result = ReloadNow();
        if (result.IsSuccess()) result = EditorObjectOperations::AddManagedScript(work.target, work.status.type);
        work.status.busy = false;
        work.status.succeeded = result.IsSuccess();
        work.status.message = result.IsSuccess() ? work.source.filename().string() + " compiled and added to the original entity." : result.message;
    }
    void Shutdown()
    {
        auto& work = State();
        std::lock_guard lock(work.mutex);
        CloseBuild(work);
        work.status = {};
    }
}
