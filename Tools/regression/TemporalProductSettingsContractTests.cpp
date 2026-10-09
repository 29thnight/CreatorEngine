// Authored, UNEXECUTED CPU/source fixture. Link TemporalRuntimeControl.cpp and
// the existing Authoring document implementation. No SDK/hardware acceptance.
#include "../../Engine/Utility_Framework/TemporalProductSettingsIO.h"
#include "../../Engine/Utility_Framework/AuthoringParsedDocument.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TemporalProductSettingsContractFixture
{
    unsigned failures = 0;

    void Expect(bool value, const char* message)
    {
        if (!value)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            ++failures;
        }
    }

    bool Read(std::string_view text, TemporalProductSettings& settings)
    {
        try
        {
            std::string error;
            const auto document = Authoring::ParsedDocument::ParseText(std::string(text), error);
            return document && TemporalProductSettingsIO::Read(document.Root(), settings);
        }
        catch (...)
        {
            return false;
        }
    }

    void SetEnvironment(const char* key, const char* value)
    {
#ifdef _WIN32
        _putenv_s(key, value ? value : "");
#else
        if (value)
        {
            setenv(key, value, 1);
        }
        else
        {
            unsetenv(key);
        }
#endif
    }

    class EnvironmentScope
    {
    public:
        explicit EnvironmentScope(const char* key) : m_key(key)
        {
            if (const auto* value = std::getenv(key))
            {
                m_previous = value;
            }
            SetEnvironment(key, nullptr);
        }
        ~EnvironmentScope()
        {
            SetEnvironment(m_key.c_str(), m_previous ? m_previous->c_str() : nullptr);
        }
        EnvironmentScope(const EnvironmentScope&) = delete;
        EnvironmentScope& operator=(const EnvironmentScope&) = delete;
    private:
        std::string m_key;
        std::optional<std::string> m_previous;
    };
}

int main()
{
    using namespace TemporalProductSettingsContractFixture;
    TemporalProductSettings legacy;
    legacy.fallbackAa = false;
    Expect(TemporalProductSettingsIO::Read({}, legacy) && !legacy.fallbackAa,
        "missing block preserves migrated legacy AA-off preference");
    Expect(Read("upscaler: fsr\n", legacy) && !legacy.fallbackAa && legacy.upscaler == "fsr",
        "partial v1 map preserves legacy AA fallback and defaults missing fields");

    TemporalProductSettings saved;
    saved.upscaler = "dlss";
    saved.quality = "native-aa";
    saved.frameGenerator = "xess";
    saved.interpolatedFrameCount = 3;
    saved.latencyMode = "on-boost";
    saved.fallbackAa = false;
    saved.spatialMode = "sharpen";
    saved.spatialRenderScale = .65f;
    saved.spatialSharpness = .7f;
    saved.digitalVibrance = true;
    saved.vibranceIntensity = .8f;
    saved.saturationBoost = .4f;
    Authoring::WriteDocument document;
    TemporalProductSettingsIO::Write(document.Root(), saved);
    TemporalProductSettings reloaded;
    Expect(Read(document.Dump(), reloaded) && reloaded == saved,
        "all portable scalar fields round-trip through the production codec");
    const auto live = TemporalRuntimeSettingsFromProduct(saved);
    Expect(TemporalProductSettingsFromRuntime(live, saved.fallbackAa) == saved,
        "runtime translation retains all portable settings");
    auto local = live;
    local.runtimeDirectory = L"C:\\machine-local\\private-runtime";
    local.dlssProjectId = "machine-local-bootstrap-identity";
    Authoring::WriteDocument portable;
    portable.Root().Child("runtimeDirectory").SetScalar("C:/obsolete-local-path");
    portable.Root().Child("unexpected").SetScalar("obsolete-field");
    TemporalProductSettingsIO::Write(portable.Root(), TemporalProductSettingsFromRuntime(local, false));
    Expect(portable.Dump().find("machine-local") == std::string::npos &&
        portable.Dump().find("runtimeDirectory") == std::string::npos &&
        portable.Dump().find("unexpected") == std::string::npos,
        "portable serialization cannot leak runtime paths or bootstrap identity");

    for (const auto* invalid : {
        "schemaVersion: 2\n", "interpolatedFrameCount: 0\n", "interpolatedFrameCount: -1\n",
        "interpolatedFrameCount: 4294967296\n", "quality: guessed-ultra\n", "latencyMode: boost-only\n",
        "spatialRenderScale: 0.49\n", "spatialSharpness: .nan\n", "digitalVibrance: maybe\n",
        "runtimeDirectory: C:/local\n", "dlssProjectId: hidden\n", "unexpected: true\n",
        "upscaler: fsr\nupscaler: dlss\n", "quality: [quality]\n" })
    {
        TemporalProductSettings result;
        Expect(!Read(invalid, result), "invalid, duplicate, nonportable or future-version field rejected");
    }

    std::vector<TemporalCapabilities> capabilities(1);
    capabilities[0].provider = TemporalProvider::Dlss;
    capabilities[0].backend = TemporalBackend::DX12;
    capabilities[0].frameGeneration = { TemporalStatus::Success };
    Expect(TemporalSupportedInterpolatedFrameCount(TemporalProvider::Dlss, TemporalBackend::DX12, capabilities) == 0,
        "successful status without a maximum is not MFG support");
    capabilities[0].maxInterpolatedFrames = 3;
    Expect(TemporalSupportedInterpolatedFrameCount(TemporalProvider::Dlss, TemporalBackend::DX12, capabilities) == 3,
        "queried MFG maximum is exposed exactly");
    Expect(TemporalSupportedInterpolatedFrameCount(TemporalProvider::Dlss, TemporalBackend::Vulkan, capabilities) == 0,
        "another backend cannot establish local support");
    capabilities.push_back(capabilities.front());
    capabilities.back().maxInterpolatedFrames = 2;
    Expect(TemporalSupportedInterpolatedFrameCount(TemporalProvider::Dlss, TemporalBackend::DX12, capabilities) == 0,
        "conflicting observed maximum fails closed");

    // Clean the optional environment so this source fixture is deterministic.
    EnvironmentScope upscaler("CREATOR_TEMPORAL_UPSCALER");
    EnvironmentScope generator("CREATOR_TEMPORAL_FRAME_GENERATOR");
    EnvironmentScope quality("CREATOR_TEMPORAL_QUALITY");
    EnvironmentScope enabled("CREATOR_TEMPORAL_ENABLED");
    EnvironmentScope count("CREATOR_TEMPORAL_INTERPOLATED_FRAME_COUNT");
    EnvironmentScope latency("CREATOR_TEMPORAL_REFLEX");
    EnvironmentScope spatial("CREATOR_NIS_MODE");
    EnvironmentScope scale("CREATOR_NIS_RENDER_SCALE");
    EnvironmentScope sharpness("CREATOR_NIS_SHARPNESS");
    EnvironmentScope vibrance("CREATOR_DEEPDVC");
    EnvironmentScope intensity("CREATOR_DEEPDVC_INTENSITY");
    EnvironmentScope saturation("CREATOR_DEEPDVC_SATURATION_BOOST");
    EnvironmentScope runtimeDirectory("CREATOR_TEMPORAL_RUNTIME_DIRECTORY");
    EnvironmentScope identity("CREATOR_TEMPORAL_DLSS_PROJECT_ID");
    SetEnvironment("CREATOR_TEMPORAL_UPSCALER", "none");
    SetEnvironment("CREATOR_TEMPORAL_INTERPOLATED_FRAME_COUNT", "2");
    SetEnvironment("CREATOR_TEMPORAL_REFLEX", "off");
    auto& control = TemporalRuntimeControl::Get();
    control.InitializeProjectDefaults(saved);
    const auto startup = control.Snapshot();
    Expect(startup.requestedSettings.requestedUpscaler == TemporalProvider::None &&
        startup.requestedSettings.interpolatedFrameCount == 2 &&
        startup.requestedSettings.reflexMode == TemporalLatencyMode::Off,
        "explicit environment overrides saved startup intent, including off/none");
    Expect(startup.requestedSettings.requestedFrameGenerator == TemporalProvider::XeSS &&
        startup.requestedSettings.quality == TemporalQuality::NativeAA,
        "absent environment keys preserve saved startup intent");
    Expect(!startup.rendererObserved && !startup.playerObserved && !startup.playerLatencyHostRegistered &&
        startup.configuredInterpolatedFrameCount == 0 && startup.activeInterpolatedFrameCount == 0,
        "startup preference creates no support or active-frame evidence");
    auto command = startup.requestedSettings;
    command.interpolatedFrameCount = 1;
    command.reflexMode = TemporalLatencyMode::On;
    control.Request(command);
    control.InitializeProjectDefaults(saved);
    Expect(control.Snapshot().requestedSettings == command,
        "later live request wins and repeated startup initialization cannot reset it");
    Expect(saved.interpolatedFrameCount == 3 && saved.latencyMode == "on-boost",
        "session overrides cannot mutate the saved product defaults");
    return failures == 0 ? 0 : 1;
}
