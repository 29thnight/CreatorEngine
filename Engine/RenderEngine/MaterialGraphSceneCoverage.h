#pragma once
#include "Render/Graph/EnhancedRenderPass.h"
#include <string>
namespace material_graph
{
    enum class SceneCoverage : std::uint8_t
    {
        Opaque,
        Masked,
        Blended
    };

    inline bool ClassifySceneCoverage(const EnhancedMaterialCoverage& coverage, SceneCoverage& result, std::string& error)
    {
        error.clear();
        if (!coverage.IsValid() || !(coverage.flags & EnhancedMaterialCoverage::Enabled) || coverage.baseAlpha < 0 ||
            coverage.baseAlpha > 1)
        {
            error = "Scene material needs a valid enabled coverage policy.";
            return false;
        }
        result = coverage.flags & EnhancedMaterialCoverage::Blended  ? SceneCoverage::Blended
                 : coverage.flags & EnhancedMaterialCoverage::Masked ? SceneCoverage::Masked
                                                                     : SceneCoverage::Opaque;
        return true;
    }

} // namespace material_graph
