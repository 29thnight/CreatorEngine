#include "SpatialPostEffectsHost.h"
#include "TemporalRuntimeControl.h"
#include "../Graph/EnhancedRenderPass.h"
#include "../../RHI/DX12/DX12DeviceResources.h"
#include "../../RHI/DX12/DX12Encoder.h"
#include "../../RHI/Temporal/DlssTemporalAdapter.h"
#include <algorithm>
#include <atomic>
#include <stdexcept>

namespace
{
    // Separate viewport namespace: local NIS/DVC evaluation tags must never
    // replace temporal reconstruction or presentation-owned frame tags.
    std::atomic<uint32_t> g_nextSpatialViewport{ 0x80000000u };

    TemporalResult DrainSpatialResources(DX12DeviceResources& resources)
    {
        std::string error;
        const bool drained = resources.DrainForLifecycle(RHILifecycleCommand::PipelineRebuild, error) &&
            resources.GetLastLifecycleResult().command != RHILifecycleCommand::UnrecoverableDeviceError &&
            resources.GetLastLifecycleResult().IsClean() && resources.GetDevice() &&
            SUCCEEDED(resources.GetDevice()->GetDeviceRemovedReason());
        return { drained ? TemporalStatus::Success : TemporalStatus::SdkFailure };
    }
}

struct SpatialPostEffectsHost::State
{
    DX12DeviceResources* resources{ nullptr };
    std::shared_ptr<DlssTemporalAdapter> session;
    SpatialPostSettings settings;
    SpatialPostSnapshot snapshot;
    uint64_t generation{ UINT64_MAX };
    uint32_t viewportId{ g_nextSpatialViewport.fetch_add(1) };
    TemporalExtent render, display;
    RHITextureHandle nisInput, output;
    bool nisFailed{ false }, deepDvcFailed{ false }, sdkResources{ false };
};

SpatialPostEffectsHost::SpatialPostEffectsHost() : m_state(std::make_unique<State>()) {}
SpatialPostEffectsHost::~SpatialPostEffectsHost()
{
    if (!Shutdown().IsSuccess())
    {
        // Preserve the native session and handles rather than unload live SDK
        // code or free resources after an unproven GPU retirement.
        (void)m_state.release();
    }
}

TemporalResult SpatialPostEffectsHost::Shutdown()
{
    auto& state = *m_state;
    if (state.resources && (state.sdkResources || state.nisInput.IsValid() || state.output.IsValid()))
    {
        const auto drained = DrainSpatialResources(*state.resources);
        if (!drained.IsSuccess())
        {
            return drained;
        }
        if (state.sdkResources && state.session)
        {
            const auto freed = state.session->FreeSpatialPostAfterGpuIdle(state.viewportId);
            if (!freed.IsSuccess())
            {
                return freed;
            }
        }
        for (const auto texture : { state.nisInput, state.output })
        {
            if (texture.IsValid())
            {
                state.resources->ReleaseTexture(texture);
            }
        }
    }
    state.nisInput = state.output = {};
    state.sdkResources = false;
    state.session.reset();
    state.resources = nullptr;
    return { TemporalStatus::Success };
}

TemporalResult SpatialPostEffectsHost::Configure(IRHIDeviceResources& resources, TemporalBackend backend,
    const TemporalRuntimeSettings& settings, uint64_t generation, TemporalExtent display,
    bool temporalResolved, bool toneMappedSdr, bool nativeOnly)
{
    auto& state = *m_state;
    if (!display.IsValid() || !resources.IsInitialized())
    {
        return { TemporalStatus::InvalidInput };
    }
    const bool retryFaulted = state.generation != generation && (state.nisFailed || state.deepDvcFailed);
    if (state.generation != generation)
    {
        state.nisFailed = state.deepDvcFailed = false;
    }
    SpatialPostSnapshot observed;
    observed.sdrEligible = toneMappedSdr;
    observed.nisCapability = observed.deepDvcCapability = {
        backend == TemporalBackend::DX12 ? TemporalStatus::SdkNotBuilt : TemporalStatus::BackendUnsupported };
    auto* dx = dynamic_cast<DX12DeviceResources*>(&resources);
    auto session = dx ? dx->GetTemporalDlssSession() : nullptr;
#if CREATOR_ENABLE_DLSS_STREAMLINE
    if (backend == TemporalBackend::DX12)
    {
        observed.nisCapability = observed.deepDvcCapability = { TemporalStatus::IntegrationRequired };
        if (dx && !session && !dx->GetTemporalBootstrapResult().IsSuccess() &&
            dx->GetTemporalBootstrapResult().status != TemporalStatus::NotQueried)
        {
            observed.nisCapability = observed.deepDvcCapability = dx->GetTemporalBootstrapResult();
        }
    }
#endif
    if (session)
    {
        const auto capability = session->QuerySpatialPostCapabilities();
        observed.nisCapability = capability.nisCapability;
        observed.deepDvcCapability = capability.deepDvcCapability;
        if (!session->MatchesRuntimeConfiguration(settings.runtimeDirectory, settings.dlssProjectId))
        {
            observed.nisCapability = observed.deepDvcCapability = { TemporalStatus::IntegrationRequired };
        }
    }
    const bool requested = !nativeOnly;
    const bool valid = ValidateSpatialPostSettings(settings.spatialPost);
    const auto eligibility = !valid ? TemporalResult{ TemporalStatus::InvalidInput } :
        !toneMappedSdr ? TemporalResult{ TemporalStatus::FeatureUnsupported } : TemporalResult{ TemporalStatus::Success };
    observed.nisResult = eligibility.IsSuccess() ? observed.nisCapability : eligibility;
    observed.deepDvcResult = eligibility.IsSuccess() ? observed.deepDvcCapability : eligibility;
    if (state.nisFailed)
    {
        observed.nisResult = state.snapshot.nisResult;
    }
    if (state.deepDvcFailed)
    {
        observed.deepDvcResult = state.snapshot.deepDvcResult;
    }
    if (requested && eligibility.IsSuccess())
    {
        if (settings.spatialPost.nisMode != SpatialScalingMode::Off && observed.nisCapability.IsSuccess() && !state.nisFailed)
        {
            observed.selectedNisMode = settings.spatialPost.nisMode;
            if (temporalResolved || settings.spatialPost.nisRenderScale == 1.f)
            {
                // Temporal output is already final resolution. NVScaler already
                // sharpens; one NVSharpen is the only permitted extra operation.
                observed.selectedNisMode = SpatialScalingMode::NisSharpen;
            }
        }
        observed.deepDvcSelected = settings.spatialPost.deepDvcEnabled &&
            observed.deepDvcCapability.IsSuccess() && !state.deepDvcFailed;
    }
    TemporalExtent render = display;
    if (observed.selectedNisMode == SpatialScalingMode::NisScale)
    {
        render.width = std::max(1u, static_cast<uint32_t>(std::ceil(display.width * settings.spatialPost.nisRenderScale)));
        render.height = std::max(1u, static_cast<uint32_t>(std::ceil(display.height * settings.spatialPost.nisRenderScale)));
    }
    const bool changed = retryFaulted || state.resources != dx || state.session != session || state.render != render || state.display != display ||
        state.snapshot.selectedNisMode != observed.selectedNisMode || state.snapshot.deepDvcSelected != observed.deepDvcSelected;
    if (changed)
    {
        const auto retired = Shutdown();
        if (!retired.IsSuccess())
        {
            return retired;
        }
        state.resources = dx;
        state.session = session;
        if (observed.selectedNisMode != SpatialScalingMode::Off || observed.deepDvcSelected)
        {
            if (!dx || !session)
            {
                return { TemporalStatus::NotInitialized };
            }
            RHITextureDesc description;
            description.width = display.width;
            description.height = display.height;
            description.format = RHIFormat::RGBA8Unorm;
            description.allowUnorderedAccess = true;
            description.allowRenderTarget = true; // Existing UI composition follows this output.
            description.initialState = RHIResourceState::ShaderResource;
            description.debugName = L"SpatialPost.PersistentOutput";
            std::string error;
            if (!dx->CreateTexture(description, state.output, error))
            {
                // Optional output allocation must not strand the live renderer
                // in a failing low-resolution configuration on every frame.
                if (observed.selectedNisMode != SpatialScalingMode::Off)
                {
                    state.nisFailed = true;
                    observed.nisResult = { TemporalStatus::SdkFailure };
                }
                if (observed.deepDvcSelected)
                {
                    state.deepDvcFailed = true;
                    observed.deepDvcResult = { TemporalStatus::SdkFailure };
                }
                observed.selectedNisMode = SpatialScalingMode::Off;
                observed.deepDvcSelected = false;
                render = display;
            }
            if (observed.selectedNisMode != SpatialScalingMode::Off)
            {
                description.width = render.width;
                description.height = render.height;
                description.allowRenderTarget = false;
                description.debugName = L"NIS.PersistentInput";
                if (!dx->CreateTexture(description, state.nisInput, error))
                {
                    state.nisFailed = true;
                    observed.nisResult = { TemporalStatus::SdkFailure };
                    observed.selectedNisMode = SpatialScalingMode::Off;
                    render = display;
                    if (!observed.deepDvcSelected)
                    {
                        dx->ReleaseTexture(state.output);
                        state.output = {};
                    }
                }
            }
        }
    }
    state.settings = settings.spatialPost;
    state.generation = generation;
    state.render = render;
    state.display = display;
    observed.inputExtent = render;
    observed.outputExtent = display;
    observed.generation = generation;
    observed.effectiveSettings = state.settings;
    observed.effectiveSettings.nisMode = observed.selectedNisMode;
    observed.effectiveSettings.deepDvcEnabled = observed.deepDvcSelected;
    state.snapshot = observed;
    return { TemporalStatus::Success };
}

TemporalExtent SpatialPostEffectsHost::RenderExtent(TemporalExtent temporalExtent) const
{
    return Scales() ? m_state->render : temporalExtent;
}
bool SpatialPostEffectsHost::Scales() const
{
    return m_state->snapshot.selectedNisMode == SpatialScalingMode::NisScale;
}
SpatialPostSnapshot SpatialPostEffectsHost::Snapshot() const { return m_state->snapshot; }

RGHandle SpatialPostEffectsHost::Declare(EnhancedRenderGraph& graph, const EnhancedFrameContext& context, RGHandle input)
{
    auto& state = *m_state;
    state.snapshot.realFrameId = context.temporalFrame.realFrameId;
    state.snapshot.observed = true;
    if (state.snapshot.selectedNisMode == SpatialScalingMode::Off && !state.snapshot.deepDvcSelected)
    {
        return input;
    }
    const bool versioned = graph.GetSchedulingMode() == RGSchedulingMode::ExplicitVersioned;
    const bool explicitAccess = graph.GetSchedulingMode() != RGSchedulingMode::DeclarationOrder;
    const auto read = explicitAccess ? RGAccessMode::Read : RGAccessMode::LegacyState;
    const auto write = explicitAccess ? RGAccessMode::Write : RGAccessMode::LegacyState;
    auto output = graph.ImportTexture(state.output, RHIResourceState::ShaderResource, "SpatialPost.PersistentOutput");
    auto copyTarget = output;
    if (state.snapshot.selectedNisMode != SpatialScalingMode::Off)
    {
        copyTarget = graph.ImportTexture(state.nisInput, RHIResourceState::ShaderResource, "NIS.PersistentInput");
    }
    if (versioned)
    {
        copyTarget = graph.Write(copyTarget);
    }
    graph.AddPass("SpatialPost.InputCopy", { { input, RHIResourceState::CopySource, read },
        { copyTarget, RHIResourceState::CopyDest, write } }, [input, copyTarget](const auto& execute)
    {
        execute.encoder->CopyTexture(execute.ResolveHandle(copyTarget), execute.ResolveHandle(input));
    });
    const uint64_t realFrameId = context.temporalFrame.realFrameId;
    // Recording callbacks can run concurrently even when graph GPU ordering is
    // strict. Establish lifetime ownership here; each callback only writes its
    // own result and fault fields.
    state.sdkResources = true;
    if (state.snapshot.selectedNisMode != SpatialScalingMode::Off)
    {
        if (versioned)
        {
            output = graph.Write(output);
        }
        graph.AddPass("SpatialPost.NIS", { { copyTarget, RHIResourceState::ShaderResource, read },
            { output, RHIResourceState::UnorderedAccess, write } }, [this, copyTarget, output, realFrameId](const auto& execute)
        {
            auto& current = *m_state;
            auto* encoder = dynamic_cast<DX12Encoder*>(execute.encoder);
            if (!encoder || !encoder->UsesResources(current.resources) || !encoder->GetCommandList())
            {
                throw std::runtime_error("NIS requires the bound DX12 graphics encoder.");
            }
            auto result = current.session->EnsureRealFrame(realFrameId);
            if (result.IsSuccess())
            {
                DlssSpatialResources native;
                native.commandList = encoder->GetCommandList();
                native.color = { current.resources->Resolve(execute.ResolveHandle(copyTarget)),
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE };
                native.output = { current.resources->Resolve(execute.ResolveHandle(output)), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
                result = current.session->DispatchNis(current.viewportId, realFrameId,
                    current.snapshot.selectedNisMode, current.settings.nisSharpness, current.render, current.display, native);
                encoder->ResetState(native.commandList);
            }
            current.snapshot.nisResult = result;
            if (!result.IsSuccess())
            {
                current.nisFailed = true;
                throw std::runtime_error("NIS recording failed; discard this frame and rebuild at native extent.");
            }
            current.snapshot.activeNisMode = current.snapshot.selectedNisMode;
        });
        graph.RequireImportedFinalState(copyTarget, RHIResourceState::ShaderResource);
    }
    else
    {
        output = copyTarget;
    }
    if (state.snapshot.deepDvcSelected)
    {
        if (versioned)
        {
            output = graph.Modify(output);
        }
        graph.AddPass("SpatialPost.DeepDVC.SDR", { { output, RHIResourceState::UnorderedAccess,
            explicitAccess ? RGAccessMode::ReadWrite : RGAccessMode::LegacyState } }, [this, output, realFrameId](const auto& execute)
        {
            auto& current = *m_state;
            auto* encoder = dynamic_cast<DX12Encoder*>(execute.encoder);
            if (!encoder || !encoder->UsesResources(current.resources) || !encoder->GetCommandList())
            {
                throw std::runtime_error("DeepDVC requires the bound DX12 graphics encoder.");
            }
            auto result = current.session->EnsureRealFrame(realFrameId);
            if (result.IsSuccess())
            {
                DlssSpatialResources native;
                native.commandList = encoder->GetCommandList();
                native.output = { current.resources->Resolve(execute.ResolveHandle(output)), D3D12_RESOURCE_STATE_UNORDERED_ACCESS };
                result = current.session->DispatchDeepDvc(current.viewportId, realFrameId, current.settings,
                    current.display, true, native);
                encoder->ResetState(native.commandList);
            }
            current.snapshot.deepDvcResult = result;
            if (!result.IsSuccess())
            {
                current.deepDvcFailed = true;
                throw std::runtime_error("DeepDVC recording failed; discard this frame before publishing color.");
            }
            current.snapshot.deepDvcApplied = true;
        });
    }
    // These SDK passes deliberately have no async-compute eligibility hint.
    // Persistent resources are used only on the ordered graphics queue. The
    // existing hudless/display copies isolate presentation and FG lifetime.
    // No per-frame GPU wait/free is introduced; only reconfiguration retires.
    graph.RequireImportedFinalState(output, RHIResourceState::ShaderResource);
    return output;
}
