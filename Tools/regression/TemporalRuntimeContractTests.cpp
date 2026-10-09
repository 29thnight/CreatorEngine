// Authored, UNEXECUTED CPU contract fixture. Does not claim SDK/hardware/pixel
// acceptance. Link the real TemporalReconstruction.cpp/TemporalRuntimeControl.cpp.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h"
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>

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
    return failures ? 1 : 0;
}
