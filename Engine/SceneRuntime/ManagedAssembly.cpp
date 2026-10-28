#include "../Containers/ContainerAPI.h"
#include "ManagedAssembly.h"
import "Creator.WinAPI.h";
#include <nethost.h>
#include <hostfxr.h>
#include <coreclr_delegates.h>

#pragma comment(lib, "nethost.lib")

namespace Runtime
{
    bool ManagedAssembly::Open(const std::filesystem::path& assembly,
                              const std::filesystem::path& privateRuntime, std::string& error)
    {
        error.clear();
        if (std::this_thread::get_id() != owner_)
        {
            error = "Managed host initialization requires its owner thread";
            return false;
        }
        auto config = assembly;
        config.replace_extension(L"runtimeconfig.json");
        if (!std::filesystem::is_regular_file(assembly) || !std::filesystem::is_regular_file(config))
        {
            error = "Managed assembly or runtime configuration is missing";
            return false;
        }

        const auto root = privateRuntime.wstring();
        get_hostfxr_parameters parameters{sizeof(get_hostfxr_parameters), nullptr, root.c_str()};
        const bool privateHost = std::filesystem::is_directory(privateRuntime / L"host" / L"fxr");
        ce::dynamic_array<wchar_t> path(32768);
        auto size = path.size();
        if (get_hostfxr_path(path.data(), &size, privateHost ? &parameters : nullptr) != 0)
        {
            error = "hostfxr could not be located";
            return false;
        }
        // Intentionally retained for process lifetime: unloading hostfxr cannot
        // unload CoreCLR and would invalidate a later host binding session.
        const auto module = LoadLibraryW(path.data());
        if (!module) { error = "hostfxr could not be loaded"; return false; }

        const auto initialize = reinterpret_cast<hostfxr_initialize_for_runtime_config_fn>(
            GetProcAddress(module, "hostfxr_initialize_for_runtime_config"));
        const auto getDelegate = reinterpret_cast<hostfxr_get_runtime_delegate_fn>(
            GetProcAddress(module, "hostfxr_get_runtime_delegate"));
        const auto close = reinterpret_cast<hostfxr_close_fn>(GetProcAddress(module, "hostfxr_close"));
        if (!initialize || !getDelegate || !close) { error = "Incomplete hostfxr API"; return false; }

        hostfxr_handle context = nullptr;
        const int status = initialize(config.c_str(), nullptr, &context);
        if ((status != 0 && status != 1) || !context)
        {
            if (context) close(context);
            error = "CoreCLR runtime configuration was rejected: " + std::to_string(status);
            return false;
        }
        void* loader = nullptr;
        const int loaded = getDelegate(context, hdt_load_assembly_and_get_function_pointer, &loader);
        close(context);
        if (loaded != 0 || !loader) { error = "CoreCLR assembly loader unavailable"; return false; }

        assembly_ = assembly;
        loader_ = loader;
        return true;
    }

    bool ManagedAssembly::Bind(const wchar_t* type, const wchar_t* method, void** target, std::string& error) const
    {
        error.clear();
        if (!target) { error = "Missing managed entry output"; return false; }
        *target = nullptr;
        if (!loader_ || std::this_thread::get_id() != owner_)
        {
            error = "Managed entry binding requires an initialized owner session";
            return false;
        }
        const auto load = reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loader_);
        const int status = load(assembly_.c_str(), type, method, UNMANAGEDCALLERSONLY_METHOD, nullptr, target);
        if (status != 0 || !*target)
        {
            error = "Managed entry could not be bound: " + std::to_string(status);
            return false;
        }
        return true;
    }
}
