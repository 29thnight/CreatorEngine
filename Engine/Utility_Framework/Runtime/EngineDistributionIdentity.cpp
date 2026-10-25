#if defined(CE_CONTRACT_STANDALONE)
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Distribution.h"
#include "../../../Tools/build/ModuleAdapters/Creator.Core.Paths.h"
#else
module Creator.Core.Distribution;
import Creator.Core.Paths;
#endif

namespace ce::distribution
{
    const identity& current_identity()
    {
        // Initialized by the host before application startup, then reused by About/crash reporting.
        static const identity cached_identity = []
        {
            identity result;
            try
            {
                auto root = ce::paths::resolve_runtime_directory().parent_path();
                for (int depth = 0; depth <= 2 && !root.empty(); ++depth, root = root.parent_path())
                {
                    for (const auto* name : {L"engine.runtime.info", L"engine.info"})
                    {
                        const auto path = root / name;
                        if (!std::filesystem::is_regular_file(path))
                        {
                            continue;
                        }
                        const auto value = read_metadata_file(path);
                        if (value.at("version") != build || value.at("productName") != product_name ||
                            value.at("featureRelease") != feature_release ||
                            value.at("localDevelopment") != (local_development ? "true" : "false"))
                        {
                            return result;
                        }
                        const auto channel = value.at("channel");
                        if (channel != "preview" && channel != "stable")
                        {
                            return result;
                        }
                        result.channel = channel;
                        result.build_id = value.at("buildId");
                        result.payload_digest = value.at("payloadDigest");
                        return result;
                    }
                }
            }
            catch (...)
            {
            }
            return result;
        }();
        return cached_identity;
    }
} // namespace ce::distribution
