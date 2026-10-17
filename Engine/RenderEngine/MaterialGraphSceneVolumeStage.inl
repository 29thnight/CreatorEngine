// Volume coefficients, composite and forward medium bindings.
RGHandle SceneHost::VolumeStage::Declare(const own::shared_owner<const FrameInput>& prepared,
                                         EnhancedRenderGraph& graph, RGHandle shaded, RGHandle lighting, RGHandle depth,
                                         RGHandle shadowMap) const
{
    const auto input = prepared;
    if (!input || !this->frame || this->output.IsValid())
    {
        return lighting;
    }
    input->CheckCurrent(&graph);
    if (!shaded.IsValid())
    {
        throw std::runtime_error("METEOR Scene Volume requires completed surface shading.");
    }
    const auto result = this->frame->DeclareComposite(graph, lighting, depth, shadowMap);
    this->output = result;
    return result;
}

template<class DrawType>
void SceneHost::VolumeStage::BindForward(RHIEncoder& encoder, const DrawType& draw,
                                         const EnhancedForwardLighting& forward) const
{
    const unsigned index = draw.program->forwardLightsRoot;
    encoder.SetRootBuffer(RHIBindPoint::Graphics, index, forward.lights.IsValid() ? forward.lights : draw.constants);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 1,
                          forward.counts.IsValid() ? RHIBufferSlice::Whole(forward.counts) : draw.constants);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 2,
                          forward.indices.IsValid() ? RHIBufferSlice::Whole(forward.indices) : draw.constants);
    const bool medium = draw.blended && bool(frame);
    encoder.SetConstantBuffer(RHIBindPoint::Graphics, index + 3, medium ? frame->TransportConstants() : constants);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 4, medium ? frame->Triangles() : buffer);
    encoder.SetRootBuffer(RHIBindPoint::Graphics, index + 5,
                          medium ? RHIBufferSlice::Whole(frame->Coefficients()) : buffer);
    const auto mediumTable = medium && forward.volumeTable.IsValid() ? forward.volumeTable : this->table;
    if (!mediumTable.IsValid())
    {
        throw std::runtime_error("Forward medium binding failed.");
    }
    encoder.SetBindings(RHIBindPoint::Graphics, index + 6, mediumTable);
}
