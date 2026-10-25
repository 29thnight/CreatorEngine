// Source-only CPU control-contract fixture. Not compiled/executed in this change.
// These calls test ownership/receipts, not SDK availability or live fallback.
#include "../../Engine/RenderEngine/Render/Temporal/TemporalRuntimeControl.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../build/ModuleAdapters/Creator.Contracts.h"
#else
import Creator.Contracts;
#endif

int main()
{
    auto& control = TemporalRuntimeControl::Get();
    const auto original = control.Snapshot().requestedSettings;
#if CE_DEVELOPMENT && !CE_SHIPPING
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (control.RequestTestFault(TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 0) == 0));
        },
        "control.RequestTestFault(TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 0) == 0");
    const auto generation = control.RequestTestFault(TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 4);
    const auto armed = control.Snapshot();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((generation != 0 && armed.requestedSettings.testFault.revision != 0));
        },
        "generation != 0 && armed.requestedSettings.testFault.revision != 0");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Capability,
                                                                TemporalProvider::Fsr, 0, 4, 2)));
        },
        "!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Capability, TemporalProvider::Fsr, 0, "
        "4, 2)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
                                                                TemporalProvider::Fsr, 17, 5, 2)));
        },
        "!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 17, "
        "5, 2)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
                                                                TemporalProvider::Fsr, 0, 4, 2)));
        },
        "!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 0, 4, "
        "2)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
                                                               TemporalProvider::Fsr, 17, 4, 2)));
        },
        "control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 17, 4, "
        "2)");
    const auto consumed = control.Snapshot();
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((consumed.testFaultConsumedCount == armed.testFaultConsumedCount + 1));
        },
        "consumed.testFaultConsumedCount == armed.testFaultConsumedCount + 1");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((consumed.testFaultConsumedRevision == armed.settings.testFault.revision));
        },
        "consumed.testFaultConsumedRevision == armed.settings.testFault.revision");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((consumed.testFaultRealFrameId == 17 && consumed.testFaultViewId == 4 &&
                                      consumed.testFaultSceneEpoch == 2));
        },
        "consumed.testFaultRealFrameId == 17 && consumed.testFaultViewId == 4 && consumed.testFaultSceneEpoch == 2");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) > generation));
        },
        "control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) > generation");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch,
                                                                TemporalProvider::Fsr, 18, 4, 2)));
        },
        "!control.ConsumeTestFault(armed.settings.testFault, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 18, "
        "4, 2)");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>((control.Snapshot().requestedSettings.testFault.mode == TemporalTestFaultMode::None));
        },
        "control.Snapshot().requestedSettings.testFault.mode == TemporalTestFaultMode::None");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::Dlss, 4) == 0));
        },
        "control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::Dlss, 4) == 0");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::XeSS, 4) == 0));
        },
        "control.RequestTestFault(TemporalTestFaultMode::FrameGenerationEvaluate, TemporalProvider::XeSS, 4) == 0");
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (control.RequestTestFault(static_cast<TemporalTestFaultMode>(255), TemporalProvider::Fsr, 4) == 0));
        },
        "control.RequestTestFault(static_cast<TemporalTestFaultMode>(255), TemporalProvider::Fsr, 4) == 0");
    for (uint8_t value = static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationCapability);
        value <= static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationFinalConsumption); ++value)
    {
        const auto mode = static_cast<TemporalTestFaultMode>(value);
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((control.RequestTestFault(mode, TemporalProvider::Fsr, 4) != 0));
            },
            "control.RequestTestFault(mode, TemporalProvider::Fsr, 4) != 0");
        const auto requested = control.Snapshot();
        const bool frameStage = mode >= TemporalTestFaultMode::FrameGenerationPrepare;
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>(
                    (!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Dlss, 21, 4, 2)));
            },
            "!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Dlss, 21, 4, 2)");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>(
                    (!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 5, 2)));
            },
            "!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 5, 2)");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>(
                    (!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 4, 0)));
            },
            "!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 21, 4, 0)");
        if (frameStage)
        {
            ::ce::contracts::enforce_that(
                [&]() -> bool {
                    return static_cast<bool>(
                        (!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 0, 4, 2)));
                },
                "!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 0, 4, 2)");
        }
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr,
                                                                   frameStage ? 21 : 0, 4, 2)));
            },
            "control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, frameStage ? 21 : 0, 4, 2)");
        const auto observed = control.Snapshot();
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((observed.testFaultConsumedMode == mode));
            },
            "observed.testFaultConsumedMode == mode");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((observed.testFaultResult.status == TemporalStatus::SdkFailure));
            },
            "observed.testFaultResult.status == TemporalStatus::SdkFailure");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((observed.testFaultResult.nativeCode == -45000 - value));
            },
            "observed.testFaultResult.nativeCode == -45000 - value");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((observed.testFaultConsumedRevision == requested.settings.testFault.revision));
            },
            "observed.testFaultConsumedRevision == requested.settings.testFault.revision");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((observed.testFaultRealFrameId == (frameStage ? 21 : 0)));
            },
            "observed.testFaultRealFrameId == (frameStage ? 21 : 0)");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>((control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) != 0));
            },
            "control.RequestTestFault(TemporalTestFaultMode::None, TemporalProvider::None) != 0");
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>(
                    (!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 22, 4, 2)));
            },
            "!control.ConsumeTestFault(requested.settings.testFault, mode, TemporalProvider::Fsr, 22, 4, 2)");
    }
#else
    for (uint8_t value = 1; value <= static_cast<uint8_t>(TemporalTestFaultMode::FrameGenerationFinalConsumption); ++value)
    {
        ::ce::contracts::enforce_that(
            [&]() -> bool {
                return static_cast<bool>(
                    (control.RequestTestFault(static_cast<TemporalTestFaultMode>(value), TemporalProvider::Fsr, 4) == 0));
            },
            "control.RequestTestFault(static_cast<TemporalTestFaultMode>(value), TemporalProvider::Fsr, 4) == 0");
    }
    TemporalTestFaultSettings external;
    external.mode = TemporalTestFaultMode::Dispatch;
    external.provider = TemporalProvider::Fsr;
    external.revision = 1;
    external.viewId = 4;
    ::ce::contracts::enforce_that(
        [&]() -> bool {
            return static_cast<bool>(
                (!control.ConsumeTestFault(external, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 17, 4, 2)));
        },
        "!control.ConsumeTestFault(external, TemporalTestFaultMode::Dispatch, TemporalProvider::Fsr, 17, 4, 2)");
#endif
    control.Request(original);
}
