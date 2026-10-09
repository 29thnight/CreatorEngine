#include "TemporalRuntimeControl.h"
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace
{
    TemporalProvider ProductProvider(std::string_view value)
    {
        if (value == "fsr") { return TemporalProvider::Fsr; }
        if (value == "dlss") { return TemporalProvider::Dlss; }
        if (value == "xess") { return TemporalProvider::XeSS; }
        return TemporalProvider::None;
    }

    const char* ProductProviderName(TemporalProvider provider)
    {
        switch (provider)
        {
        case TemporalProvider::Fsr: return "fsr";
        case TemporalProvider::Dlss: return "dlss";
        case TemporalProvider::XeSS: return "xess";
        default: return "none";
        }
    }

    void ApplyEnvironmentOverrides(TemporalRuntimeSettings& settings)
    {
        auto product = TemporalProductSettingsFromRuntime(settings, true);
        bool valid = true;
        const auto text = [](const char* key, std::string& value)
        {
            if (const auto* overrideValue = std::getenv(key))
            {
                value = overrideValue;
            }
        };
        text("CREATOR_TEMPORAL_UPSCALER", product.upscaler);
        text("CREATOR_TEMPORAL_FRAME_GENERATOR", product.frameGenerator);
        text("CREATOR_TEMPORAL_QUALITY", product.quality);
        text("CREATOR_TEMPORAL_REFLEX", product.latencyMode);
        text("CREATOR_NIS_MODE", product.spatialMode);
        const auto boolean = [](const char* key, bool& value)
        {
            if (const auto* overrideValue = std::getenv(key))
            {
                value = std::string_view{ overrideValue } == "1";
            }
        };
        boolean("CREATOR_TEMPORAL_ENABLED", product.enabled);
        boolean("CREATOR_DEEPDVC", product.digitalVibrance);
        const auto number = [&valid](const char* key, auto& value)
        {
            if (const auto* overrideValue = std::getenv(key))
            {
                const std::string_view source{ overrideValue };
                const auto parsed = std::from_chars(source.data(), source.data() + source.size(), value);
                if (source.empty() || parsed.ec != std::errc{} || parsed.ptr != source.data() + source.size())
                {
                    valid = false;
                }
            }
        };
        number("CREATOR_TEMPORAL_INTERPOLATED_FRAME_COUNT", product.interpolatedFrameCount);
        number("CREATOR_NIS_RENDER_SCALE", product.spatialRenderScale);
        number("CREATOR_NIS_SHARPNESS", product.spatialSharpness);
        number("CREATOR_DEEPDVC_INTENSITY", product.vibranceIntensity);
        number("CREATOR_DEEPDVC_SATURATION_BOOST", product.saturationBoost);
        // Invalid overrides fail to native defaults, never guessed SDK support.
        settings = valid && ValidateTemporalProductSettings(product)
            ? TemporalRuntimeSettingsFromProduct(product) : TemporalRuntimeSettings{};
#ifdef _WIN32
        if (const auto* directory = _wgetenv(L"CREATOR_TEMPORAL_RUNTIME_DIRECTORY"))
#else
        if (const auto* directory = std::getenv("CREATOR_TEMPORAL_RUNTIME_DIRECTORY"))
#endif
        {
            try
            {
                const std::filesystem::path path(directory);
                if (path.is_absolute())
                {
                    settings.runtimeDirectory = path.wstring();
                }
            }
            catch (const std::filesystem::filesystem_error&)
            {
                settings.runtimeDirectory.clear();
            }
        }
        if (const auto* project = std::getenv("CREATOR_TEMPORAL_DLSS_PROJECT_ID"))
        {
            settings.dlssProjectId = project;
        }
    }
}

TemporalRuntimeSettings TemporalRuntimeSettingsFromProduct(const TemporalProductSettings& settings)
{
    TemporalRuntimeSettings result;
    if (!ValidateTemporalProductSettings(settings))
    {
        return result;
    }
    result.enabled = settings.enabled;
    result.requestedUpscaler = ProductProvider(settings.upscaler);
    result.requestedFrameGenerator = ProductProvider(settings.frameGenerator);
    result.interpolatedFrameCount = settings.interpolatedFrameCount;
    if (settings.quality == "native-aa") { result.quality = TemporalQuality::NativeAA; }
    else if (settings.quality == "balanced") { result.quality = TemporalQuality::Balanced; }
    else if (settings.quality == "performance") { result.quality = TemporalQuality::Performance; }
    else if (settings.quality == "ultra-performance") { result.quality = TemporalQuality::UltraPerformance; }
    result.reflexMode = settings.latencyMode == "on-boost" ? TemporalLatencyMode::OnPlusBoost :
        settings.latencyMode == "on" ? TemporalLatencyMode::On : TemporalLatencyMode::Off;
    result.spatialPost.nisMode = settings.spatialMode == "scale" ? SpatialScalingMode::NisScale :
        settings.spatialMode == "sharpen" ? SpatialScalingMode::NisSharpen : SpatialScalingMode::Off;
    result.spatialPost.nisRenderScale = settings.spatialRenderScale;
    result.spatialPost.nisSharpness = settings.spatialSharpness;
    result.spatialPost.deepDvcEnabled = settings.digitalVibrance;
    result.spatialPost.deepDvcIntensity = settings.vibranceIntensity;
    result.spatialPost.deepDvcSaturationBoost = settings.saturationBoost;
    return result;
}

TemporalProductSettings TemporalProductSettingsFromRuntime(const TemporalRuntimeSettings& settings, bool fallbackAa)
{
    TemporalProductSettings result;
    result.enabled = settings.enabled;
    result.upscaler = ProductProviderName(settings.requestedUpscaler);
    result.frameGenerator = ProductProviderName(settings.requestedFrameGenerator);
    result.interpolatedFrameCount = settings.interpolatedFrameCount;
    switch (settings.quality)
    {
    case TemporalQuality::NativeAA: result.quality = "native-aa"; break;
    case TemporalQuality::Balanced: result.quality = "balanced"; break;
    case TemporalQuality::Performance: result.quality = "performance"; break;
    case TemporalQuality::UltraPerformance: result.quality = "ultra-performance"; break;
    default: result.quality = "quality"; break;
    }
    result.latencyMode = settings.reflexMode == TemporalLatencyMode::OnPlusBoost ? "on-boost" :
        settings.reflexMode == TemporalLatencyMode::On ? "on" : "off";
    result.fallbackAa = fallbackAa;
    result.spatialMode = SpatialScalingModeName(settings.spatialPost.nisMode);
    result.spatialRenderScale = settings.spatialPost.nisRenderScale;
    result.spatialSharpness = settings.spatialPost.nisSharpness;
    result.digitalVibrance = settings.spatialPost.deepDvcEnabled;
    result.vibranceIntensity = settings.spatialPost.deepDvcIntensity;
    result.saturationBoost = settings.spatialPost.deepDvcSaturationBoost;
    return result;
}

uint32_t TemporalSupportedInterpolatedFrameCount(TemporalProvider provider, TemporalBackend backend,
    const std::vector<TemporalCapabilities>& capabilities)
{
    uint32_t maximum = 0;
    bool observed = false;
    for (const auto& capability : capabilities)
    {
        if (capability.provider != provider || capability.backend != backend ||
            capability.frameGeneration.status == TemporalStatus::NotQueried)
        {
            continue;
        }
        if (!capability.frameGeneration.IsSuccess() || capability.maxInterpolatedFrames == 0 ||
            (observed && maximum != capability.maxInterpolatedFrames))
        {
            return 0;
        }
        maximum = capability.maxInterpolatedFrames;
        observed = true;
    }
    return maximum;
}

TemporalRuntimeControl::TemporalRuntimeControl()
{
    ApplyEnvironmentOverrides(m_snapshot.settings);
    if (m_snapshot.settings != TemporalRuntimeSettings{})
    {
        m_snapshot.requestedGeneration = 1;
    }
}

TemporalRuntimeControl& TemporalRuntimeControl::Get()
{
    // Capture scopes may outlive other static owners during process teardown.
    static auto* control = new TemporalRuntimeControl;
    return *control;
}

void TemporalRuntimeControl::InitializeProjectDefaults(const TemporalProductSettings& settings)
{
    std::lock_guard lock(m_mutex);
    if (m_projectDefaultsInitialized)
    {
        return;
    }
    m_snapshot.settings = TemporalRuntimeSettingsFromProduct(settings);
    ApplyEnvironmentOverrides(m_snapshot.settings);
    m_projectDefaultsInitialized = true;
    ++m_snapshot.requestedGeneration;
}

void TemporalRuntimeControl::RegisterPlayerLatencyHost()
{
    std::lock_guard lock(m_mutex);
    m_snapshot.playerLatencyHostRegistered = true;
}

TemporalRuntimeSnapshot TemporalRuntimeControl::Snapshot() const
{
    std::lock_guard lock(m_mutex);
    auto result = m_snapshot;
    result.requestedSettings = m_snapshot.settings;
    result.nativeCaptureExclusionActive = m_nativeCaptureExclusions != 0;
    if (result.nativeCaptureExclusionActive)
    {
        result.settings.enabled = false;
        result.settings.requestedUpscaler = TemporalProvider::None;
        result.settings.requestedFrameGenerator = TemporalProvider::None;
        result.settings.spatialPost.nisMode = SpatialScalingMode::Off;
        result.settings.spatialPost.deepDvcEnabled = false;
    }
    return result;
}

uint64_t TemporalRuntimeControl::Request(const TemporalRuntimeSettings& settings)
{
    std::lock_guard lock(m_mutex);
    m_snapshot.settings = settings;
#if !CE_DEVELOPMENT || CE_SHIPPING
    m_snapshot.settings.testFault = {};
#endif
    return ++m_snapshot.requestedGeneration;
}

uint64_t TemporalRuntimeControl::RequestTestFault(TemporalTestFaultMode mode, TemporalProvider provider, uint64_t viewId)
{
#if CE_DEVELOPMENT && !CE_SHIPPING
    if (mode > TemporalTestFaultMode::Dispatch || provider > TemporalProvider::XeSS ||
        (mode == TemporalTestFaultMode::None) != (provider == TemporalProvider::None) ||
        (mode != TemporalTestFaultMode::None && viewId == 0))
    {
        return 0;
    }
    std::lock_guard lock(m_mutex);
    auto& fault = m_snapshot.settings.testFault;
    fault.mode = mode;
    fault.provider = provider;
    fault.viewId = mode == TemporalTestFaultMode::None ? 0 : viewId;
    ++fault.revision;
    ++m_snapshot.historyResetGeneration;
    return ++m_snapshot.requestedGeneration;
#else
    (void)mode;
    (void)provider;
    (void)viewId;
    return 0;
#endif
}

bool TemporalRuntimeControl::ConsumeTestFault(const TemporalTestFaultSettings& fault,
    TemporalTestFaultMode mode, TemporalProvider provider, uint64_t realFrameId, uint64_t viewId, uint64_t sceneEpoch)
{
#if CE_DEVELOPMENT && !CE_SHIPPING
    // Off is the normal render hot path; no mutex or global lookup is needed.
    if (fault.revision == 0 || fault.mode != mode || mode == TemporalTestFaultMode::None || fault.provider != provider ||
        fault.viewId != viewId || viewId == 0 || sceneEpoch == 0 ||
        (mode == TemporalTestFaultMode::Dispatch && realFrameId == 0))
    {
        return false;
    }
    std::lock_guard lock(m_mutex);
    if (fault != m_snapshot.settings.testFault)
    {
        return false;
    }
    m_snapshot.testFaultConsumedRevision = fault.revision;
    ++m_snapshot.testFaultConsumedCount;
    // Capability rejection occurs before a real frame exists. Zero explicitly
    // means no frame identity for that configuration-time observation.
    m_snapshot.testFaultRealFrameId = realFrameId;
    m_snapshot.testFaultViewId = viewId;
    m_snapshot.testFaultSceneEpoch = sceneEpoch;
    return true;
#else
    (void)fault;
    (void)mode;
    (void)provider;
    (void)realFrameId;
    (void)viewId;
    (void)sceneEpoch;
    return false;
#endif
}

uint64_t TemporalRuntimeControl::RequestHistoryReset()
{
    std::lock_guard lock(m_mutex);
    ++m_snapshot.historyResetGeneration;
    return ++m_snapshot.requestedGeneration;
}

void TemporalRuntimeControl::AcquireNativeCaptureExclusion()
{
    std::lock_guard lock(m_mutex);
    if (m_nativeCaptureExclusions++ == 0)
    {
        ++m_snapshot.requestedGeneration;
        ++m_snapshot.historyResetGeneration;
    }
}

void TemporalRuntimeControl::ReleaseNativeCaptureExclusion()
{
    std::lock_guard lock(m_mutex);
    if (m_nativeCaptureExclusions != 0 && --m_nativeCaptureExclusions == 0)
    {
        ++m_snapshot.requestedGeneration;
        ++m_snapshot.historyResetGeneration;
    }
}
