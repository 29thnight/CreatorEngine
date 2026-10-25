// CPU contracts, executed by verify-temporal-foundation.ps1 in Debug/Release.
// Does not claim SDK/hardware/pixel acceptance.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#include <stdio.h>
#include "../../Engine/Utility_Framework/Foundation/StandardLibrary.h"

namespace
{
    unsigned failures = 0;
    void Expect(bool value, const char* message)
    {
        if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
    }
    TemporalFrame Frame()
    {
        TemporalFrame frame;
        frame.realFrameId = 7;
        frame.renderExtent = { 1280, 720 };
        frame.displayExtent = { 1920, 1080 };
        frame.motionVectorScaleX = frame.motionVectorScaleY = 1;
        frame.frameTimeMilliseconds = 16;
        frame.cameraNear = .1f;
        frame.cameraFar = 1000;
        frame.cameraVerticalFov = 1;
        return frame;
    }
}
int main()
{
    auto frame = Frame();
    Expect(ValidateTemporalFrame(frame).IsSuccess(), "complete real frame accepted");
    frame.realFrameId = 0;
    Expect(!ValidateTemporalFrame(frame).IsSuccess(), "missing real identity rejected");
    frame = Frame(); frame.displayExtent = {};
    Expect(!ValidateTemporalFrame(frame).IsSuccess(), "missing display extent rejected");
    frame = Frame(); frame.jitterX = std::numeric_limits<float>::quiet_NaN();
    Expect(!ValidateTemporalFrame(frame).IsSuccess(), "nonfinite jitter rejected");
    frame = Frame(); frame.previousJitterY = 1;
    Expect(!ValidateTemporalFrame(frame).IsSuccess(), "invalid previous jitter rejected");
    frame = Frame(); frame.camera.orthographicProjection = true; frame.cameraVerticalFov = 0; frame.cameraNear = -1;
    Expect(ValidateTemporalFrame(frame).IsSuccess(), "native orthographic metadata does not invent perspective FOV");
    TemporalFrameGenerationInputs orthographic;
    orthographic.frame = frame;
    Expect(ValidateTemporalFrameGenerationInputs(orthographic).status == TemporalStatus::ProjectionUnsupported,
        "unimplemented SDK projection fails closed");
    frame = Frame(); frame.renderExtent.width = 3840;
    Expect(!ValidateTemporalFrame(frame).IsSuccess(), "oversized render extent rejected");
    for (uint64_t index = 0; index != 2048; ++index)
    {
        const auto first = SampleTemporalJitter(index, 91);
        const auto replay = SampleTemporalJitter(index, 91);
        const auto wrapped = SampleTemporalJitter(index + 1024, 91);
        Expect(first.x == replay.x && first.y == replay.y && first.x == wrapped.x && first.y == wrapped.y,
            "production jitter sequence reproducible and bounded-period");
        Expect(std::abs(first.x) <= .5f && std::abs(first.y) <= .5f, "jitter is centered render-pixel offset");
    }
    const auto reset = SampleTemporalJitter(0);
    Expect(reset.x == 0 && std::abs(reset.y + 1.f / 6.f) < .000001f, "reset starts canonical Halton sample");
    std::array<TemporalCapabilities,3> caps{};
    caps[0].provider = TemporalProvider::Fsr;
    caps[0].upscaling = caps[0].frameGeneration = { TemporalStatus::Success };
    caps[1].provider = TemporalProvider::Dlss;
    caps[1].upscaling = { TemporalStatus::Success };
    caps[1].frameGeneration = { TemporalStatus::FeatureUnsupported };
    caps[2].provider = TemporalProvider::XeSS;
    caps[2].upscaling = { TemporalStatus::RuntimeUnavailable, 126 };
    caps[2].frameGeneration = { TemporalStatus::Success };
    auto selection = SelectTemporalProviders(TemporalProvider::Dlss, TemporalProvider::XeSS, TemporalBackend::DX12, caps);
    Expect(selection.upscaler == TemporalProvider::Dlss && selection.frameGenerator == TemporalProvider::XeSS,
        "independent provider axes retained");
    selection = SelectTemporalProviders(TemporalProvider::XeSS, TemporalProvider::Dlss, TemporalBackend::DX12, caps);
    Expect(selection.upscaler == TemporalProvider::Fsr && selection.frameGenerator == TemporalProvider::Fsr &&
        selection.requestedUpscaler.nativeCode == 126, "queried FSR fallback preserves original SDK failure");
    selection = SelectTemporalProviders(TemporalProvider::Dlss, TemporalProvider::XeSS, TemporalBackend::Vulkan, caps);
    Expect(selection.upscaler == TemporalProvider::None && selection.frameGenerator == TemporalProvider::None,
        "different backend capability cannot activate requested device");
    caps[0].upscaling = caps[0].frameGeneration = { TemporalStatus::SdkNotBuilt };
    selection = SelectTemporalProviders(TemporalProvider::XeSS, TemporalProvider::Dlss, TemporalBackend::DX12, caps);
    Expect(selection.upscaler == TemporalProvider::None && selection.frameGenerator == TemporalProvider::None,
        "unsupported fallback is native, not fabricated FSR support");
    TemporalFrameGenerationConfig config;
    // Independent query records from TU and FG must merge by axis. A stale,
    // conflicting result must never silently win based on snapshot order.
    std::array<TemporalCapabilities, 3> split{};
    split[0].provider = split[1].provider = TemporalProvider::Dlss;
    split[0].upscaling = { TemporalStatus::Success };
    split[1].frameGeneration = { TemporalStatus::FeatureUnsupported, 17 };
    split[2].provider = TemporalProvider::Fsr;
    split[2].upscaling = split[2].frameGeneration = { TemporalStatus::Success };
    selection = SelectTemporalProviders(TemporalProvider::Dlss, TemporalProvider::Dlss, TemporalBackend::DX12, split);
    Expect(selection.upscaler == TemporalProvider::Dlss && selection.frameGenerator == TemporalProvider::Fsr &&
        selection.requestedFrameGenerator.nativeCode == 17, "split capability axes retain independent SDK result");
    split[1].upscaling = { TemporalStatus::RuntimeUnavailable, 126 };
    for (unsigned order = 0; order != 2; ++order)
    {
        selection = SelectTemporalProviders(TemporalProvider::Dlss, TemporalProvider::None, TemporalBackend::DX12, split);
        Expect(selection.upscaler == TemporalProvider::Fsr && selection.frameGenerator == TemporalProvider::None &&
            selection.requestedUpscaler.status == TemporalStatus::InvalidInput,
            "conflicting query snapshots fail closed without enabling disabled FG");
        const auto temporary = split[0]; split[0] = split[1]; split[1] = temporary;
    }
    selection = SelectTemporalProviders(TemporalProvider::None, TemporalProvider::None, TemporalBackend::DX12, split);
    Expect(selection.upscaler == TemporalProvider::None && selection.frameGenerator == TemporalProvider::None,
        "explicit native request cannot be overridden by available or contradictory SDKs");
    config.target = TemporalPresentationTarget::EditorViewport;
    Expect(ValidateTemporalFrameGenerationConfig(config).status == TemporalStatus::EditorViewportForbidden,
        "Editor FG rejected before resource/native setup");
    auto& runtime = TemporalRuntimeControl::Get();
    auto settings = runtime.Snapshot().requestedSettings;
    settings.requestedUpscaler = TemporalProvider::Dlss;
    settings.requestedFrameGenerator = TemporalProvider::Fsr;
    const auto receipt = runtime.Request(settings);
    const auto before = runtime.Snapshot();
    Expect(before.requestedGeneration == receipt && before.observedGeneration < receipt,
        "request receipt does not fabricate live observation");
    {
        TemporalNativeCaptureExclusion outer;
        TemporalNativeCaptureExclusion inner;
        auto captured = runtime.Snapshot();
        Expect(!captured.settings.enabled && captured.requestedSettings == settings,
            "capture overrides effective execution without deleting user intent");
        settings.requestedUpscaler = TemporalProvider::XeSS;
        runtime.Request(settings);
    }
    Expect(runtime.Snapshot().settings == settings, "capture release preserves concurrent new user intent");
    settings.enabled = false;
    settings.requestedUpscaler = TemporalProvider::None;
    settings.requestedFrameGenerator = TemporalProvider::None;
    settings.reflexMode = TemporalLatencyMode::OnPlusBoost;
    runtime.Request(settings);
    const auto latencyOnly = runtime.Snapshot();
    Expect(latencyOnly.settings.reflexMode == TemporalLatencyMode::OnPlusBoost &&
        latencyOnly.settings.requestedUpscaler == TemporalProvider::None &&
        latencyOnly.settings.requestedFrameGenerator == TemporalProvider::None,
        "independent Reflex intent survives disabled reconstruction and interpolation");
    Expect(!latencyOnly.reflex.configured && latencyOnly.reflex.sleepRealFrameId == 0 &&
        latencyOnly.reflex.markerRealFrameId == 0 && latencyOnly.reflex.presentedRealFrameId == 0,
        "a latency request invents no SDK configuration or timing observation");
    {
        TemporalNativeCaptureExclusion capture;
        Expect(runtime.Snapshot().settings.reflexMode == TemporalLatencyMode::OnPlusBoost,
            "pixel-only native capture does not overwrite independent latency intent");
    }
    settings.reflexMode = TemporalLatencyMode::Off;
    runtime.Request(settings);
    Expect(runtime.Snapshot().requestedSettings.reflexMode == TemporalLatencyMode::Off,
        "Off remains an explicit latency request instead of enabling frame generation");
    return failures ? 1 : 0;
}
