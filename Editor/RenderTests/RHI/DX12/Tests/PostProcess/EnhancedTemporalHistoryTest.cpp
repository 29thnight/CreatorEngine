#include "RHI/DX12/Tests/DX12SelfTest.h"
#include "RHI/DX12/DX12DeviceResources.h"
#include "Render/Scene/EnhancedTemporalViewState.h"
#include "Render/Scene/EnhancedSceneRenderer.h"
#include "Camera.h"
#include "../../../../../../Engine/Utility_Framework/StandardLibrary.h"

// Production Begin/Commit contract on a real DX12 device and SDK contexts.
// Commit is driven by this test; no SDK dispatch, pixels or live submission is
// claimed. Live dispatch acceptance belongs to the temporal stage driver.
bool DX12Test::RunTemporalHistoryTest(std::string& outLog)
{
    DX12DeviceResources resources;
    std::string error;
    if (!resources.Initialize(384, 216, error))
    {
        outLog += "DX12 initialization failed: " + error;
        return false;
    }
    unsigned checks = 0, failures = 0;
    const auto expect = [&](bool value, const char* label) {
        ++checks;
        if (!value) { ++failures; outLog += std::string("FAIL: ") + label + "\n"; }
    };
    const auto close = [](float a, float b) { return std::abs(a - b) < 0.000001f; };
    // Independent rational Halton samples, not a call to the producer.
    constexpr float jitterX[] = { 0.f, -.25f, .25f, -.375f };
    constexpr float jitterY[] = { -1.f/6.f, 1.f/6.f, -7.f/18.f, -1.f/18.f };
    for (const auto provider : { TemporalProvider::None, TemporalProvider::Fsr, TemporalProvider::XeSS })
    {
        outLog += "provider=" + std::to_string(static_cast<unsigned>(provider)) + "\n";
        {
            EnhancedTemporalViewState state;
            TemporalRuntimeSnapshot control;
            control.settings.requestedUpscaler = provider;
            control.settings.enabled = true;
            control.settings.runtimeDirectory = TemporalRuntimeControl::Get().Snapshot().settings.runtimeDirectory;
            Camera camera;
            auto sealed = camera.CaptureFrameSnapshot(16.f/9.f);
            TemporalExtent display{384,216};
            uint64_t real = 1, view = 11, scene = 7, history = 3;
            const auto begin = [&] {
                const auto frameId = real++;
                const auto result = state.Begin(resources, TemporalBackend::DX12, control,
                    sealed, frameId, view, scene, history, display, 1.f/60.f, float(frameId)/60.f, false, true);
                expect(result.IsSuccess(), "production Begin succeeds");
                return result.IsSuccess();
            };
            if (!begin()) continue;
            expect(state.upscaler.Provider() == provider, "requested SDK selected without fallback");
            const bool temporal = provider != TemporalProvider::None;
            expect(state.frame.displayExtent == display && state.frame.renderExtent.IsValid(), "extents valid and display retained");
            expect(temporal ? state.frame.renderExtent.width < display.width && state.frame.renderExtent.height < display.height
                : state.frame.renderExtent == display, "quality separates extents; native is exactly 1:1");
            const auto sample = [&](unsigned index) {
                expect(close(state.frame.jitterX, temporal ? jitterX[index] : 0.f) &&
                    close(state.frame.jitterY, temporal ? jitterY[index] : 0.f), "independent canonical jitter sample");
                bool matrixMatches = true;
                for (unsigned row=0; row<4; ++row)
                    for (unsigned col=0; col<4; ++col)
                    {
                        float expected = sealed.projection.m[row][col];
                        if (col == 0) expected += 2.f * state.frame.jitterX / float(state.frame.renderExtent.width) * sealed.projection.m[row][3];
                        if (col == 1) expected -= 2.f * state.frame.jitterY / float(state.frame.renderExtent.height) * sealed.projection.m[row][3];
                        matrixMatches &= close(state.rasterCamera.projection.m[row][col], expected);
                    }
                expect(matrixMatches, "raster jitter uses render extent and correct axis signs");
            };
            expect(state.frame.reset && state.frame.historyRevision == 1, "first history reset");
            expect(state.lastHistoryReset.realFrameId == 0, "uncommitted preparation cannot publish a reset observation");
            sample(0);
            begin(); // Abort initial preparation: no Commit.
            expect(state.frame.reset && state.frame.historyRevision == 1, "aborted initial reset remains pending");
            sample(0);
            state.Commit({});
            expect(state.lastHistoryReset.realFrameId == state.frame.realFrameId &&
                state.lastHistoryReset.historyRevision == 1, "committed reset identity latched");
            for (unsigned index=1; index<4; ++index)
            {
                begin();
                expect(!state.frame.reset && state.frame.historyRevision == 1, "continuous history stable");
                sample(index);
                const auto pending = state.frame;
                begin(); // Abort a normal recording.
                expect(state.frame.jitterX == pending.jitterX && state.frame.jitterY == pending.jitterY &&
                    state.frame.previousJitterX == pending.previousJitterX && state.frame.previousJitterY == pending.previousJitterY,
                    "aborted recording cannot consume jitter or previous jitter");
                state.Commit({});
                expect(state.lastHistoryReset.historyRevision == 1 &&
                    state.lastHistoryReset.realFrameId < state.frame.realFrameId,
                    "normal commits preserve reset observation");
            }
            camera.m_eyePosition.x += 1000.f;
            sealed = camera.CaptureFrameSnapshot(16.f/9.f);
            begin();
            expect(!state.frame.reset, "ordinary translation preserves history");
            state.Commit({});
            const auto resetCase = [&](const char* label) {
                const auto committed = state.committedHistoryGeneration;
                begin();
                expect(state.frame.reset && state.frame.historyRevision == committed+1, label);
                sample(0);
                expect(state.frame.previousJitterX == state.frame.jitterX && state.frame.previousJitterY == state.frame.jitterY,
                    "reset suppresses previous jitter delta");
                begin();
                expect(state.frame.reset && state.frame.historyRevision == committed+1, "aborted reset not lost or counted twice");
                expect(state.lastHistoryReset.historyRevision == committed, "aborted reset cannot replace committed observation");
                sample(0);
                state.Commit({});
                expect(state.lastHistoryReset.historyRevision == committed+1 &&
                    state.lastHistoryReset.cameraCutRevision == sealed.cameraCutRevision &&
                    state.lastHistoryReset.displayExtent == display, "reset latch retains committed camera and resized extent");
                begin();
                expect(!state.frame.reset && state.frame.historyRevision == committed+1, "committed reset occurs once");
                sample(1);
                state.Commit({});
            };
            camera.NotifyCameraCut(); sealed = camera.CaptureFrameSnapshot(16.f/9.f);
            resetCase("explicit camera cut resets");
            camera.m_fov = 45.f; sealed = camera.CaptureFrameSnapshot(16.f/9.f);
            resetCase("unjittered FOV change resets");
            ++control.historyResetGeneration;
            resetCase("explicit history request resets");
            display = {512,288};
            resetCase("display resize resets SDK and history");
            expect(state.frame.displayExtent == display, "resized display propagated");
            ++view; resetCase("logical view change resets");
            ++scene; resetCase("scene epoch change resets");
            ++history; resetCase("source history revision change resets");
            control.settings.quality = TemporalQuality::NativeAA;
            if (temporal) resetCase("quality change resets");
            else
            {
                begin();
                expect(!state.frame.reset, "inactive upscaler quality does not invalidate native history");
                state.Commit({});
            }
            expect(state.frame.renderExtent == display, "SDK native-AA is exactly 1:1");
            expect(state.upscaler.Shutdown().IsSuccess(), "SDK context shutdown retires owner");
            expect(state.spatialPost.Shutdown().IsSuccess(), "spatial context shutdown");
        }
    }
    // Independent view clocks share controls, never jitter cursors or extents.
    for (const auto provider : {TemporalProvider::Fsr, TemporalProvider::XeSS, TemporalProvider::Dlss})
    {
        for (const auto extent : {TemporalExtent{321, 197}, TemporalExtent{640, 360}, TemporalExtent{961, 541}})
        {
            for (const auto quality : {TemporalQuality::NativeAA, TemporalQuality::Quality, TemporalQuality::Balanced,
                     TemporalQuality::Performance, TemporalQuality::UltraPerformance})
            {
                EnhancedTemporalViewState views[3];
                TemporalRuntimeSnapshot control;
                control.settings = TemporalRuntimeControl::Get().Snapshot().settings;
                control.settings.requestedUpscaler = provider;
                control.settings.quality = quality;
                control.settings.enabled = true;
                Camera camera;
                auto sealed = camera.CaptureFrameSnapshot(float(extent.width) / extent.height);
                const auto prepare = [&](unsigned view, uint64_t frame, const FrameCameraSnapshot& input)
                {
                    const auto result = views[view].Begin(resources, TemporalBackend::DX12, control, input,
                        frame, 100 + view, 7, 1, extent, 1.f / 60.f, float(frame) / 60.f, view == 2, true);
                    expect(result.IsSuccess(), "TR2 multi-view configuration succeeds");
                    return result.IsSuccess();
                };
                for (unsigned step = 0; step < 2; ++step)
                {
                    for (unsigned view = 0; view < 3; ++view)
                    {
                        if (!prepare(view, 1 + view * 100 + step, sealed))
                        {
                            continue;
                        }
                        const auto& state = views[view];
                        expect(state.frame.displayExtent == extent && state.frame.renderExtent.IsValid() &&
                            state.frame.renderExtent.width <= extent.width && state.frame.renderExtent.height <= extent.height,
                            "TR2 per-view render/display bounds");
                        expect(state.upscaler.Provider() == (view == 2 ? TemporalProvider::None : provider),
                            "TR2 active reconstruction and native-only preview remain independent");
                        expect(close(state.frame.jitterX, view == 2 ? 0.f : jitterX[step]) &&
                            close(state.frame.jitterY, view == 2 ? 0.f : jitterY[step]), "TR2 interleaving preserves independent Halton phase");
                        expect((quality == TemporalQuality::NativeAA || view == 2)
                            ? state.frame.renderExtent == extent
                            : state.frame.renderExtent.width < extent.width && state.frame.renderExtent.height < extent.height,
                            "TR2 reconstruction quality uses separate input extent");
                        views[view].Commit({});
                    }
                }
                auto cut = sealed;
                ++cut.cameraCutRevision;
                prepare(0, 3, cut);
                expect(views[0].frame.reset && close(views[0].frame.jitterY, jitterY[0]), "TR2 one view resets its cursor");
                views[0].Commit({});
                prepare(1, 103, sealed);
                expect(!views[1].frame.reset && close(views[1].frame.jitterX, jitterX[2]) &&
                    close(views[1].frame.jitterY, jitterY[2]), "TR2 other view continues after independent reset");
                views[1].Commit({});
                for (auto& state : views)
                {
                    expect(state.upscaler.Shutdown().IsSuccess() && state.spatialPost.Shutdown().IsSuccess(),
                        "TR2 multi-view SDK retirement");
                }
            }
        }
    }
    // A delayed readback cannot insert duplicate measured poses between steps.
    {
        EnhancedTemporalFixtureStamp step;
        step.sessionId = 1; step.stepId = 2; step.predecessorStepId = 1;
        step.inputSha256 = step.predecessorInputSha256 = std::string(64, 'a');
        EnhancedTemporalFixtureSubmission previous;
        previous.stamp = step; previous.sourceVerified = true;
        previous.viewId = 11; previous.historyRevision = 3; previous.sceneEpoch = 7;
        const EnhancedLiveViewKey key{11, 3};
        for (unsigned retry = 0; retry < 8; ++retry)
            expect(ShouldSkipTemporalFixtureStep(previous, step, key, 7),
                "held measured pose cannot resubmit while GT waits for readback");
        auto next = step; ++next.stepId; ++next.predecessorStepId;
        expect(!ShouldSkipTemporalFixtureStep(previous, next, key, 7), "next measured pose admitted");
        auto baseline = step; baseline.stepId = 1; baseline.predecessorStepId = 0;
        baseline.predecessorInputSha256.clear();
        expect(ShouldSkipTemporalFixtureStep(previous, baseline, key, 7), "queued older baseline cannot replace predecessor");
        previous.stamp = baseline;
        expect(!ShouldSkipTemporalFixtureStep(previous, baseline, key, 7), "baseline can warm non-reset history");
        baseline.sdkQuality = true; baseline.sdkWarmupSubmissions = 35;
        previous.stamp = baseline; previous.baselineSubmissions = 34;
        expect(!ShouldSkipTemporalFixtureStep(previous, baseline, key, 7), "SDK warmup admits its last required submission");
        previous.baselineSubmissions = 35;
        for (unsigned poll = 0; poll < 8; ++poll)
            expect(ShouldSkipTemporalFixtureStep(previous, baseline, key, 7), "SDK baseline holds while GT polls or captures");
        auto measured = baseline; measured.stepId = 2; measured.predecessorStepId = 1;
        measured.predecessorInputSha256 = measured.inputSha256;
        expect(!ShouldSkipTemporalFixtureStep(previous, measured, key, 7), "exact warmup admits measured baseline");
        auto nativeBaseline = baseline; nativeBaseline.sdkQuality = false;
        expect(!ShouldSkipTemporalFixtureStep(previous, nativeBaseline, key, 7), "native baseline is not bounded by SDK warmup");
        previous.stamp = step;
        expect(!ShouldSkipTemporalFixtureStep(previous, {}, key, 7), "ordinary views remain unrestricted");
        auto foreign = step; ++foreign.sessionId;
        expect(!ShouldSkipTemporalFixtureStep(previous, foreign, key, 7), "new fixture session remains unrestricted");
        expect(!ShouldSkipTemporalFixtureStep(previous, step, {12, 3}, 7), "other view remains unrestricted");
        expect(!ShouldSkipTemporalFixtureStep(previous, step, {11, 4}, 7), "new view history remains unrestricted");
        expect(!ShouldSkipTemporalFixtureStep(previous, step, key, 8), "new scene remains unrestricted");
        previous.sourceVerified = false;
        expect(!ShouldSkipTemporalFixtureStep(previous, step, key, 7), "unsubmitted or rejected pose can retry");
    }
    {
        TemporalMeasurementProvenance p;
        p.realFrameId = 1; p.frameKind = TemporalMeasuredFrameKind::Real;
        p.renderExtent = {221,96}; p.displayExtent = {332,144};
        p.upscaler = TemporalProvider::Fsr; p.resolutionState = TemporalResolutionState::Reconstructed;
        expect(IsSdkQualityFixtureProvenance(p), "FSR real reconstructed fixture can prove a submitted pose");
        p.upscaler = TemporalProvider::XeSS;
        expect(IsSdkQualityFixtureProvenance(p), "XeSS real reconstructed fixture can prove a submitted pose");
        p.upscaler = TemporalProvider::Dlss;
        expect(IsSdkQualityFixtureProvenance(p), "DLSS real reconstructed fixture can prove a submitted pose");
        const auto valid = p;
        p.upscaler = TemporalProvider::None;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept native fallback"); p = valid;
        p.nativeGateActive = true;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept native exclusion"); p = valid;
        p.frameGenerator = TemporalProvider::Fsr;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept frame generation"); p = valid;
        p.spatialMode = SpatialScalingMode::NisSharpen;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept spatial post effects"); p = valid;
        p.deepDvcApplied = true;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept DeepDVC"); p = valid;
        p.testFaultActive = true;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept deliberate fault evidence"); p = valid;
        p.frameKind = TemporalMeasuredFrameKind::Generated; p.generatedOrdinal = 1;
        expect(!IsSdkQualityFixtureProvenance(p), "SDK fixture cannot accept generated frames");
    }
    std::string validation;
    const auto problems = resources.DrainDebugMessages(validation);
    expect(problems == 0, "DX12 debug validation clean");
    outLog += validation;
    resources.Shutdown();
    outLog += "checks=" + std::to_string(checks) + " failures=" + std::to_string(failures) +
        " validation=" + std::to_string(problems) + "\n";
    return failures == 0;
}
