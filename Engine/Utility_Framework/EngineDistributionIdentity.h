#pragma once
#include "EngineRuntimePaths.h"
#include "EngineVersion.h"
#include "EngineMetadataFile.h"
#include <fstream>
#include <string>

struct EngineDistributionIdentity
{
    std::string channel{ "preview" };
    std::string buildId{ "unpublished" };
    std::string payloadDigest;
};

inline const EngineDistributionIdentity& CurrentEngineDistributionIdentity()
{
    // Initialized by the host before application startup, then reused by About/crash reporting.
    static const EngineDistributionIdentity identity = []
    {
        EngineDistributionIdentity result;
        try
        {
            auto root = ResolveEngineRuntimeDirectory().parent_path();
            for (int depth = 0; depth <= 2 && !root.empty(); ++depth, root = root.parent_path())
            {
                for (const auto* name : { L"engine.runtime.info", L"engine.info" })
                {
                    const auto path = root / name;
                    if (!std::filesystem::is_regular_file(path)) continue;
                    const auto value = ReadEngineMetadataFile(path);
                    if (value.at("version") != CreatorEngineVersion::Build ||
                        value.at("productName") != CreatorEngineVersion::ProductName ||
                        value.at("featureRelease") != CreatorEngineVersion::FeatureRelease ||
                        value.at("localDevelopment") != (CreatorEngineVersion::LocalDevelopment ? "true" : "false"))
                        return result;
                    const auto channel = value.at("channel");
                    if (channel != "preview" && channel != "stable") return result;
                    result.channel = channel;
                    result.buildId = value.at("buildId");
                    result.payloadDigest = value.at("payloadDigest");
                    return result;
                }
            }
        }
        catch (...) {}
        return result;
    }();
    return identity;
}
