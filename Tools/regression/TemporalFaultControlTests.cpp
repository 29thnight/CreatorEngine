// Source-only CPU control-contract fixture. Not compiled/executed in this change.
// These calls test ownership/receipts, not SDK availability or live fallback.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#include <cassert>

int main()
{
    auto& control = TemporalRuntimeControl::Get();
    const auto original = control.Snapshot().requestedSettings;
#if CE_DEVELOPMENT && !CE_SHIPPING
    assert(control.RequestTestFault(TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 0) == 0);
    const auto generation = control.RequestTestFault(TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 4);
    const auto armed = control.Snapshot();
    assert(generation != 0 && armed.requestedSettings.testFault.revision != 0);
    assert(!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Capability,
        TemporalProvider::Fsr, 0, 4, 2));
    assert(!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
        TemporalProvider::Fsr, 17, 5, 2));
    assert(!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
        TemporalProvider::Fsr, 0, 4, 2));
    assert(control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
        TemporalProvider::Fsr, 17, 4, 2));
    const auto consumed = control.Snapshot();
    assert(consumed.testFaultConsumedCount == armed.testFaultConsumedCount + 1);
    assert(consumed.testFaultConsumedRevision == armed.settings.testFault.revision);
    assert(consumed.testFaultRealFrameId == 17 && consumed.testFaultViewId == 4 && consumed.testFaultSceneEpoch == 2);
    assert(control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) > generation);
    assert(!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
        TemporalProvider::Fsr, 18, 4, 2));
    assert(control.Snapshot().requestedSettings.testFault.mode == TemporalTestFaultMode::None);
    assert(control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::Dlss, 4) == 0);
    assert(control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::XeSS, 4) == 0);
    assert(control.RequestTestFault(static_cast<TemporalTestFaultMode>(255), TemporalProvider::Fsr, 4) == 0);
    for (uint8_t value = static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationCapability);
        value <= static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationFinalConsumption); ++value)
    {
        const auto mode = static_cast<TemporalTestFaultMode>(value);
        assert(control.RequestTestFault(mode, TemporalProvider::Fsr, 4) != 0);
        const auto requested = control.Snapshot();
        const bool frameStage = mode >= TemporalTestFaultMode::FrameGenerationPrepare;
        assert(!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Dlss, 21, 4, 2));
        assert(!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 5, 2));
        assert(!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 4, 0));
        if (frameStage)
        {
            assert(!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 0, 4, 2));
        }
        assert(control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr,
            frameStage ? 21 : 0, 4, 2));
        const auto observed = control.Snapshot();
        assert(observed.testFaultConsumedMode == mode);
        assert(observed.testFaultResult.status == TemporalStatus::SdkFailure);
        assert(observed.testFaultResult.nativeCode == -45000 - value);
        assert(observed.testFaultConsumedRevision == requested.settings.testFault.revision);
        assert(observed.testFaultRealFrameId == (frameStage ? 21 : 0));
        assert(control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) != 0);
        assert(!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 22, 4, 2));
    }
#else
    for (uint8_t value = 1; value <= static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationFinalConsumption); ++value)
    {
        assert(control.RequestTestFault(static_cast<TemporalTestFaultMode>(value), TemporalProvider::Fsr, 4) == 0);
    }
    TemporalTestFaultSettings external;
    external.mode = TemporalTestFaultMode::Dispatch;
    external.provider = TemporalProvider::Fsr;
    external.revision = 1;
    external.viewId = 4;
    assert(!control.ConsumeTestFault(external, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 17, 4, 2));
#endif
    control.Request(original);
}
