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
    static thread_local std::vector<const MeshSurfaceBatch*> geometry;
    static thread_local ce::render_pointer_set<MeshSurfaceBatch> declaredGeometry;
    declaredGeometry.reset(frame->draws.size() * 2 + frame->shadowDraws.size());
    if (!geometry.empty())
    {
        geometry.clear();
    }
    struct ClearGeometry
    {
        std::vector<const MeshSurfaceBatch*>& values;
        ce::render_pointer_set<MeshSurfaceBatch>& seen;
        ~ClearGeometry()
        {
            values.clear();
            seen.clear();
        }
    } clearGeometry{geometry, declaredGeometry};
    const auto addGeometry = [&](const MeshSurfaceBatch* batch)
    {
        if (declaredGeometry.insert(batch))
        {
            geometry.push_back(batch);
        }
    };
    for (const auto& draw : std::span{frame->draws})
    {
        addGeometry(draw.geometry.get());
        if (draw.previousGeometry)
        {
            addGeometry(draw.previousGeometry.get());
        }
        for (const auto& texture : std::span{draw.bindings->resources.textures})
        {
            auto handle = graph.FindImportedTexture(texture.resource);
            if (!handle.IsValid())
            {
                handle =
                    graph.ImportTexture(texture.resource, RHIResourceState::PixelShaderResource, "METEOR.Scene.Image");
            }
        }
    }
    for (const auto& draw : std::span{frame->shadowDraws})
    {
        addGeometry(draw.geometry.get());
    }
    std::string error;
    if (!MeshSurfaceBatch::DeclareBatch(graph, geometry, error))
    {
        throw std::runtime_error(error);
    }
    this->geometryTable->Declare(graph, this->geometryTable);
    if (frame->sceneData)
    {
        frame->sceneData->Declare(graph);
    }
    frame->graph = graph.DeclarationGraph();
    frame->graphEpoch = graph.DeclarationEpoch();
    return this->geometryTable->GraphTable(graph);
}
