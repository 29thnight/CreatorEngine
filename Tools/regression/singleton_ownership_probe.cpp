#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Core.Distribution.h"
#else
import Creator.Core.Distribution;
#endif
#include "../../Engine/RenderEngine/Assets/ClusterStreaming.h"
#include "../../Engine/RenderEngine/RHI/RHIRecordingDiagnostics.h"
#include "../../Editor/EngineGUIWindow/InspectorIcon.h"

const void* page_io_from_other_unit();
const void* policy_from_other_unit();
const void* identity_from_other_unit();
const void* icon_table_from_other_unit();
const char* lookup_icon_from_other_unit(std::size_t key);

int main()
{
    unsigned checks{};
    const auto require = [&](bool condition)
    {
        ++checks;
        if (!condition)
        {
            throw std::runtime_error("singleton ownership contract failed");
        }
    };
    try
    {
        require(page_io_from_other_unit() == &assets::cluster::PageIo::Shared());
        require(policy_from_other_unit() == &RHIRecordingPolicy::Get());
        require(identity_from_other_unit() == &ce::distribution::current_identity());
        require(icon_table_from_other_unit() == &editor::inspector::inspector_icon_table());
        auto& icons = editor::inspector::inspector_icon_table();
        const auto key = (std::numeric_limits<std::size_t>::max)();
        const auto old = icons.find(key);
        const bool existed = old != icons.end();
        const char* previous = existed ? old->second : nullptr;
        static constexpr char glyph[] = "OwnershipProbe";
        editor::inspector::set_inspector_icon(key, glyph);
        require(lookup_icon_from_other_unit(key) == glyph);
        editor::inspector::set_inspector_icon(key, "");
        require(lookup_icon_from_other_unit(key) == glyph);
        if (existed)
        {
            icons[key] = previous;
        }
        else
        {
            icons.erase(key);
        }
        require(ce::distribution::current_identity().channel == "preview" || ce::distribution::current_identity().channel == "stable");
        require(RHIRecordingPolicy::ReadBounded("CE_SINGLETON_PROBE_UNSET_501BA2D0", 1, 1) == 1);
        std::cout << "SINGLETON_OWNERSHIP_OK checks=" << checks << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
