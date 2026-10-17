// Stage implementation included by MaterialGraphSceneHost.cpp inside material_graph.
RGHandle SceneHost::GeometryStage::DeclareGeometry(const own::shared_owner<const FrameInput>& prepared,
                                                   EnhancedRenderGraph& graph) const
{
    ce::profile_scope profile{ce::marker<"MaterialDeclareGeometry">()};
    const auto frame = prepared;
    if (!frame || frame->draws.empty())
    {
        return {};
    }
    frame->CheckCurrent();
    if (frame->graph)
    {
        frame->CheckCurrent(&graph);
        return this->geometryTable->GraphTable(graph);
    }
    // Declare unique pose producers once; instance views retain their own worlds.
    std::set<const MeshSurfaceBatch*> declaredGeometry;
    std::vector<const MeshSurfaceBatch*> geometry;
    for (const auto& draw : frame->draws)
    {
        declaredGeometry.insert(draw.geometry.get());
        geometry.push_back(draw.geometry.get());
        if (draw.previousGeometry && declaredGeometry.insert(draw.previousGeometry.get()).second)
        {
            geometry.push_back(draw.previousGeometry.get());
        }
        for (const auto& texture : draw.bindings->resources.textures)
        {
            auto handle = graph.FindImportedTexture(texture.resource);
            if (!handle.IsValid())
            {
                handle =
                    graph.ImportTexture(texture.resource, RHIResourceState::PixelShaderResource, "METEOR.Scene.Image");
            }
        }
    }
    for (const auto& draw : frame->shadowDraws)
    {
        if (declaredGeometry.insert(draw.geometry.get()).second)
        {
            geometry.push_back(draw.geometry.get());
        }
    }
    std::string error;
    if (!MeshSurfaceBatch::DeclareBatch(graph, geometry, error))
    {
        throw std::runtime_error(error);
    }
    this->geometryTable->Declare(graph, this->geometryTable);
    frame->graph = &graph;
    frame->graphEpoch = graph.ResourceEpoch();
    return this->geometryTable->GraphTable(graph);
}
