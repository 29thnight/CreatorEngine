#include "MaterialGraphScenePacket.h"

#include <algorithm>
#include <limits>

namespace material_graph
{
namespace
{
bool Fail(std::string& error, std::string message)
{
    error = std::move(message);
    return false;
}

bool InstallShaders(const VerifiedProduct& product, RHIShaderBinary backend, RHIGraphicsPipelineDesc& description,
                    std::string& error)
{
    const std::string binary = backend == RHIShaderBinary::Dxil ? "dxil" : "spirv";
    unsigned vertices = 0, pixels = 0;
    for (const auto& target : product.targets)
    {
        if (target.binary != backend)
        {
            continue;
        }
        const bool vertex = target.profile.starts_with("vs_");
        const bool pixel = target.profile.starts_with("ps_");
        if (!vertex && !pixel)
        {
            continue;
        }
        const auto shader = std::ranges::find_if(product.shaders, [&](const auto& artifact) {
            return artifact.backend == binary && artifact.entryPoint == target.entry;
        });
        if (shader == product.shaders.end() || shader->bytecode.empty())
        {
            return Fail(error, "Scene material is missing its compiled surface entry.");
        }
        if (vertex)
        {
            ++vertices;
            description.vsBytecode = shader->bytecode.data();
            description.vsSize = shader->bytecode.size();
        }
        else
        {
            ++pixels;
            description.psBytecode = shader->bytecode.data();
            description.psSize = shader->bytecode.size();
        }
    }
    return vertices == 1 && pixels == 1
               ? true
               : Fail(error, "Scene material needs exactly one compiled VS/PS pair for the device backend.");
}
} // namespace

bool BuildSceneSurfaceEvaluation(std::shared_ptr<const SurfaceBatch> gpu, SceneSurfaceEvaluation& result,
                                 std::string& error)
{
    if (!gpu || !gpu->Material() || !gpu->Count())
    {
        return Fail(error, "Scene GPU evaluation needs an owning material/surface batch.");
    }
    SceneSurfaceEvaluation candidate;
    candidate.instance = gpu->Material();
    candidate.sceneEpoch = gpu->View().sceneEpoch;
    candidate.viewRevision = gpu->View().viewRevision;
    candidate.geometryRevision = gpu->View().geometryRevision;
    candidate.gpu = std::move(gpu);
    result = std::move(candidate);
    error.clear();
    return true;
}

bool ClassifySceneCoverage(const EnhancedMaterialCoverage& coverage, SceneCoverage& result, std::string& error)
{
    error.clear();
    if (!coverage.IsValid() || !(coverage.flags & EnhancedMaterialCoverage::Enabled) || coverage.baseAlpha < 0 ||
        coverage.baseAlpha > 1)
    {
        return Fail(error, "Scene material needs a valid enabled coverage policy.");
    }
    result = coverage.flags & EnhancedMaterialCoverage::Blended  ? SceneCoverage::Blended
             : coverage.flags & EnhancedMaterialCoverage::Masked ? SceneCoverage::Masked
                                                                 : SceneCoverage::Opaque;
    return true;
}

bool CreateScenePassLayout(IRenderRootSignatureCache& cache, const BindingLayout& material,
                           std::span<const RHIPipelineLayoutParam> hostParameters,
                           std::span<const RHIStaticSamplerDesc> hostSamplers, bool inputAssembler,
                           ScenePassLayout& result, std::string& error, std::uint32_t iblRegister)
{
    if (hostParameters.size() >= 64 || iblRegister >= TextureRegister)
    {
        return Fail(error, "Scene host leaves no root parameter for its IBL buffer.");
    }
    for (const auto& parameter : hostParameters)
    {
        if ((parameter.kind == RHILayoutParamKind::ShaderResourceBuffer && parameter.shaderRegister == iblRegister) ||
            (parameter.kind == RHILayoutParamKind::DescriptorTable &&
             parameter.table.type == RHIDescriptorType::ShaderResource && iblRegister >= parameter.table.baseRegister &&
             std::uint64_t(iblRegister) < std::uint64_t(parameter.table.baseRegister) + parameter.table.count))
        {
            return Fail(error, "Scene host overlaps the evaluated-point IBL buffer register.");
        }
    }
    ScenePassLayout candidate;
    candidate.iblSlot = static_cast<std::uint32_t>(hostParameters.size());
    std::vector<RHIPipelineLayoutParam> parameters(hostParameters.begin(), hostParameters.end());
    parameters.push_back(RHILayout::Srv(iblRegister, RHIShaderVisibility::Pixel));
    if (!CreatePassLayout(cache, material, parameters, hostSamplers, inputAssembler, candidate.material, error))
    {
        return false;
    }
    result = std::move(candidate);
    return true;
}

SceneMaterialSlot::~SceneMaterialSlot()
{
    ShutdownAfterIdle();
}

bool SceneMaterialSlot::Initialize(IRenderDeviceServices& device, std::string& error)
{
    error.clear();
    if (device_)
    {
        return device_ == &device ? true : Fail(error, "Scene material slot belongs to another device.");
    }
    if (device.GetCurrentUploadRecordingId() != 0)
    {
        return Fail(error, "Initialize the Scene material submission owner before opening a recording.");
    }
    device_ = &device;
    device.RegisterUploadTransactionListener(this);
    return true;
}

bool SceneMaterialSlot::Prepare(IRenderTextureCache& textures, IRenderPipelineCache& pipelines,
                                RenderBindingCache& bindings, IblBaker& baker, const SceneSurfaceEvaluation& evaluation,
                                const IblEnvironment& environment, const EnhancedMaterialCoverage& coverage,
                                const ScenePassLayout& layout, const RHIGraphicsPipelineDesc& pipeline,
                                RHIShaderBinary backend, const Capabilities& capabilities, const Budget& budget,
                                std::shared_ptr<const SceneMaterialPacket>& result, std::string& error)
{
    error.clear();
    const auto recordingId = device_ ? device_->GetCurrentUploadRecordingId() : 0;
    if (!recordingId || !evaluation.instance || !evaluation.instance->generation || !evaluation.sceneEpoch ||
        !evaluation.viewRevision || !evaluation.geometryRevision ||
        (evaluation.gpu ? !evaluation.points.empty() : evaluation.points.empty()) ||
        evaluation.points.size() > IblBaker::MaxPoints || serial_ == (std::numeric_limits<std::uint64_t>::max)() ||
        (backend != RHIShaderBinary::Dxil && backend != RHIShaderBinary::SpirV))
    {
        return Fail(error, "Scene material needs an owning evaluated instance/view/geometry in an open recording.");
    }
    if (evaluation.gpu &&
        (evaluation.gpu->Device() != device_ || !evaluation.gpu->Count() ||
         evaluation.gpu->Count() > IblBaker::MaxPoints || evaluation.gpu->Material() != evaluation.instance ||
         evaluation.gpu->View().sceneEpoch != evaluation.sceneEpoch ||
         evaluation.gpu->View().viewRevision != evaluation.viewRevision ||
         evaluation.gpu->View().geometryRevision != evaluation.geometryRevision ||
         (evaluation.gpu->RecordingId() != recordingId && !evaluation.gpu->IsValidated())))
    {
        return Fail(
            error,
            "Scene GPU material/view/geometry must match its live source in this recording or completed validation.");
    }
    const auto& product = evaluation.instance->generation->cooked.product;
    std::vector<std::uint8_t> validated;
    if (!WriteCookedProgram(product, budget, validated, error))
    {
        return false;
    }
    auto candidate = std::make_shared<SceneMaterialPacket>();
    std::vector<LX::LXMaterialDiagnostic> diagnostics;
    if (!SelectRoute(product.program, capabilities, budget, candidate->selection, diagnostics))
    {
        return Fail(error, diagnostics.empty() ? "Scene material route is unavailable." : diagnostics.front().message);
    }
    if (product.program.volume || candidate->selection.tier == Tier::Special ||
        candidate->selection.route != Route::Forward || candidate->selection.route != product.selection.route ||
        candidate->selection.tier != product.selection.tier || layout.material.material != product.layout ||
        layout.iblSlot >= 64 || layout.iblSlot + 1 != layout.material.hostParameterCount ||
        !pipeline.layout.IsValid() || pipeline.layout != layout.material.handle || pipeline.vsBytecode ||
        pipeline.psBytecode || pipeline.vsSize || pipeline.psSize || pipeline.independentBlend)
    {
        return Fail(error, "Scene packet supports matching Core/Layered Forward hosts with separate coverage.");
    }
    if (!ClassifySceneCoverage(coverage, candidate->queue, error))
    {
        return false;
    }
    const bool blended = candidate->queue == SceneCoverage::Blended;
    const bool doubleSided = (coverage.flags & EnhancedMaterialCoverage::DoubleSided) != 0;
    if (pipeline.blendEnable != blended ||
        pipeline.depthWriteMask != (blended ? RHIDepthWrite::Zero : RHIDepthWrite::All) ||
        pipeline.cullMode != (doubleSided ? RHICullMode::None : RHICullMode::Back) ||
        !std::ranges::all_of(evaluation.points, [&](const auto& point) {
            return point.viewTier[3] == (candidate->selection.tier == Tier::Layered ? 1.f : 0.f);
        }))
    {
        return Fail(error, "Scene PSO coverage or evaluated point tier disagrees with the owning material.");
    }
    auto description = pipeline;
    if (!InstallShaders(product, backend, description, error))
    {
        return false;
    }
    auto request = std::make_shared<RHIGraphicsPipelineRequest>();
    if (!request->Create(pipelines, description, error))
    {
        return false;
    }
    candidate->serial = ++serial_;
    candidate->coverage = coverage;
    candidate->evaluation = evaluation;
    candidate->environment = environment;
    candidate->pipeline = std::move(request);
    candidate->iblSlot = layout.iblSlot;

    // Retain before any operation which can record GPU work. In particular a
    // texture upload can submit a recording segment before returning a failure.
    recordings_[recordingId].owners.push_back(candidate);
    const bool prepared =
        bindings.Prepare(*device_, textures, evaluation.instance, layout.material, candidate->bindings, error);
    const auto current = device_->GetCurrentUploadRecordingId();
    if (current != 0 && current != recordingId)
    {
        recordings_[current].owners.push_back(candidate);
    }
    if (!prepared)
    {
        return false;
    }
    if (current != recordingId || candidate->bindings->recordingId != current)
    {
        return Fail(error, "Scene material recording changed during texture preparation; retry in the new recording.");
    }
    auto& recording = recordings_[current];
    const auto previous = recording.accepted ? recording.accepted : active_;
    if (previous && (evaluation.gpu ? previous->ibl->MatchesGpu(*device_, environment, *evaluation.gpu)
                                    : previous->ibl->Matches(*device_, environment, evaluation.points)))
    {
        candidate->ibl = previous->ibl;
    }
    else if (!(evaluation.gpu ? baker.RecordGpu(*device_, environment, evaluation.gpu, candidate->ibl, error)
                              : baker.Record(*device_, environment, evaluation.points, candidate->ibl, error)))
    {
        return false;
    }
    recording.accepted = candidate;
    result = std::move(candidate);
    return true;
}

bool SceneMaterialSlot::Bind(RHIEncoder& encoder, const SceneMaterialPacket& packet, std::string& error) const
{
    error.clear();
    if (!device_ || !packet.bindings || !packet.pipeline || !packet.pipeline->IsValid() || !packet.ibl ||
        packet.bindings->device != device_ || packet.bindings->recordingId != device_->GetCurrentUploadRecordingId() ||
        packet.bindings->descriptorVersion != device_->GetDescriptorVersionToken() ||
        packet.pipeline->GetDesc().layout != packet.bindings->layout.handle || packet.iblSlot >= 64 ||
        packet.iblSlot + 1 != packet.bindings->layout.hostParameterCount)
    {
        return Fail(error, "Scene material packet is incomplete or belongs to another recording.");
    }
    encoder.SetPipeline(RHIBindPoint::Graphics, packet.pipeline->GetHandle());
    if (!RenderBindingCache::Bind(*device_, encoder, RHIBindPoint::Graphics, *packet.bindings, error))
    {
        return false;
    }
    encoder.SetRootBuffer(RHIBindPoint::Graphics, packet.iblSlot, RHIBufferSlice::Whole(packet.ibl->Buffer()));
    return true;
}

void SceneMaterialSlot::OnUploadSubmitted(std::uint64_t recordingId, RHICompletionPoint completion)
{
    const auto found = recordings_.find(recordingId);
    if (found == recordings_.end())
    {
        return;
    }
    auto& recording = found->second;
    recording.completion = completion.value == 0 ? 0 : (std::max)(recording.completion, completion.value);
    recording.submissionNotified = true;
    recording.submitted = recording.accepted;
}

bool SceneMaterialSlot::PublishSubmitted(std::uint64_t recordingId, RHICompletionPoint completion, std::string& error)
{
    error.clear();
    const auto found = recordings_.find(recordingId);
    if (found == recordings_.end() || !completion.value || found->second.completion != completion.value ||
        !found->second.submitted || (found->second.publicationDecided && active_ != found->second.submitted) ||
        (active_ && found->second.submitted->serial < active_->serial))
    {
        return Fail(error, "Scene material publication needs the confirmed submission's exact recording and fence.");
    }
    if (found->second.submitted->evaluation.gpu &&
        (completed_ < completion.value || !found->second.submitted->evaluation.gpu->IsValidated()))
    {
        return Fail(error, "Scene GPU material publication awaits completed submission and accepted evaluated input.");
    }
    active_ = found->second.submitted;
    found->second.publicationDecided = true;
    return true;
}

void SceneMaterialSlot::RejectSubmitted(std::uint64_t recordingId)
{
    const auto found = recordings_.find(recordingId);
    if (found != recordings_.end() && !found->second.publicationDecided)
    {
        found->second.publicationDecided = true;
        OnUploadCompleted(completed_);
    }
}

void SceneMaterialSlot::OnUploadCompleted(std::uint64_t completed)
{
    completed_ = (std::max)(completed_, completed);
    const auto current = device_ ? device_->GetCurrentUploadRecordingId() : 0;
    for (auto item = recordings_.begin(); item != recordings_.end();)
    {
        auto& recording = item->second;
        if (item->first == current || recording.completion == 0 || recording.completion > completed_)
        {
            ++item;
            continue;
        }
        // A fast GPU can finish before the host checks its asynchronous CPU
        // submission ticket. Retain that unpublished candidate for the decision.
        if (!recording.publicationDecided && recording.submitted &&
            (!active_ || recording.submitted->serial > active_->serial))
        {
            recording.owners.clear();
            recording.accepted.reset();
            ++item;
            continue;
        }
        item = recordings_.erase(item);
    }
}

void SceneMaterialSlot::OnUploadAborted(std::uint64_t recordingId)
{
    const auto found = recordings_.find(recordingId);
    if (found == recordings_.end())
    {
        return;
    }
    // A recording can have an earlier partial submission. Abort only cancels
    // the unsubmitted tail; those owners still wait for the known fence.
    if (!found->second.submissionNotified || (found->second.completion != 0 && found->second.completion <= completed_))
    {
        recordings_.erase(found);
    }
}

void SceneMaterialSlot::ShutdownAfterIdle()
{
    if (device_)
    {
        device_->UnregisterUploadTransactionListener(this);
    }
    active_.reset();
    recordings_.clear();
    device_ = nullptr;
    serial_ = 0;
    completed_ = 0;
}
} // namespace material_graph
