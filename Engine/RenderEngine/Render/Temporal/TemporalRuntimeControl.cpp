#include "TemporalRuntimeControl.h"
#include <cstdlib>
#include <filesystem>
#include <string_view>

TemporalRuntimeControl::TemporalRuntimeControl()
{
    const auto provider = [](const char* name) {
        const auto* value = std::getenv(name);
        const std::string_view text = value ? value : "";
        if (text == "fsr") return TemporalProvider::Fsr;
        if (text == "dlss") return TemporalProvider::Dlss;
        if (text == "xess") return TemporalProvider::XeSS;
        return TemporalProvider::None;
    };
    m_snapshot.settings.requestedUpscaler = provider("CREATOR_TEMPORAL_UPSCALER");
    m_snapshot.settings.requestedFrameGenerator = provider("CREATOR_TEMPORAL_FRAME_GENERATOR");
    if (const auto* mode = std::getenv("CREATOR_TEMPORAL_REFLEX"))
    {
        const std::string_view value{ mode };
        if (value == "on") { m_snapshot.settings.reflexMode = TemporalLatencyMode::On; }
        else if (value == "on-boost") { m_snapshot.settings.reflexMode = TemporalLatencyMode::OnPlusBoost; }
    }
    if (const auto* mode = std::getenv("CREATOR_NIS_MODE"))
    {
        const std::string_view value{ mode };
        if (value == "scale") { m_snapshot.settings.spatialPost.nisMode = SpatialScalingMode::NisScale; }
        else if (value == "sharpen") { m_snapshot.settings.spatialPost.nisMode = SpatialScalingMode::NisSharpen; }
    }
    if (const auto* enabled = std::getenv("CREATOR_DEEPDVC"))
    {
        m_snapshot.settings.spatialPost.deepDvcEnabled = std::string_view{ enabled } == "1";
    }
#ifdef _WIN32
    if (const auto* directory = _wgetenv(L"CREATOR_TEMPORAL_RUNTIME_DIRECTORY"))
#else
    if (const auto* directory = std::getenv("CREATOR_TEMPORAL_RUNTIME_DIRECTORY"))
#endif
    {
        try
        {
            const std::filesystem::path path(directory);
            if (path.is_absolute()) m_snapshot.settings.runtimeDirectory = path.wstring();
        }
        catch (const std::filesystem::filesystem_error&)
        {
            m_snapshot.settings.runtimeDirectory.clear();
        }
    }
    if (const auto* project = std::getenv("CREATOR_TEMPORAL_DLSS_PROJECT_ID"))
        m_snapshot.settings.dlssProjectId = project;
    if (m_snapshot.settings.requestedUpscaler != TemporalProvider::None ||
        m_snapshot.settings.requestedFrameGenerator != TemporalProvider::None ||
        m_snapshot.settings.reflexMode != TemporalLatencyMode::Off ||
        m_snapshot.settings.spatialPost.nisMode != SpatialScalingMode::Off ||
        m_snapshot.settings.spatialPost.deepDvcEnabled)
        m_snapshot.requestedGeneration = 1;
}
TemporalRuntimeControl& TemporalRuntimeControl::Get()
{
    // Capture scopes may outlive other static owners during process teardown.
    static auto* control = new TemporalRuntimeControl;
    return *control;
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
    return ++m_snapshot.requestedGeneration;
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
