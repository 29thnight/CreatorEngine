#pragma once
#include "../Utility_Framework/StandardLibrary.h"

namespace Runtime
{
    // Hosts own their binding session; CoreCLR and hostfxr remain process-wide.
    // This layer only loads unmanaged entry points. It knows no scene, assets,
    // presentation device, script registration policy, or editor settings.
    class ManagedAssembly
    {
    public:
        bool Open(const std::filesystem::path& assembly, const std::filesystem::path& privateRuntime,
                  std::string& error);
        bool Bind(const wchar_t* type, const wchar_t* method, void** target, std::string& error) const;

    private:
        std::filesystem::path assembly_;
        void* loader_ = nullptr;
        std::thread::id owner_ = std::this_thread::get_id();
    };
}
