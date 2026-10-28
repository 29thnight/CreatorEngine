// Included by DiagnosticsCommands.cpp inside ConsoleCmd. Every command runs
// under the ordinary scene write boundary and reacquires its current entity.
static CommandCore::CommandResult Cmd_foliage_cache_probe(const ConsoleCommandContext& ctx)
{
    using namespace CommandCore;
    Scene* scene = SceneManagers->GetActiveScene();
    if (!scene || ctx.parts.size() < 2) return InvalidArguments("foliage.cache.probe <create|status|measure|edit|remove|rebind|save> ...");
    const auto& action = ctx.parts[1];
    Entity* entity = nullptr;
    for (const auto& candidate : scene->m_Entities)
        if (candidate && candidate->m_name == "__FoliageCacheProbe") { entity = candidate.get(); break; }
    if (action == "create")
    {
        size_t count{};
        if (entity || ctx.parts.size() != 4 || !ParseNumber(ctx.parts[3], count) || count > 100000 || count == 0)
            return InvalidArguments("create <source-model-path> <count>, maximum 100000; use a fresh scene");
        const auto generation = DataSystems->LoadModelAssetGenerationByPath(ctx.parts[2]);
        if (!generation || generation->Meshes().empty()) return Fail("foliage.model", "Model generation unavailable");
        entity = scene->CreateEntity("__FoliageCacheProbe");
        if (!entity) return Fail("foliage.entity", "Cannot create entity");
        auto* component = entity->AddComponent<FoliageComponent>();
        FoliageType type;
        type.m_modelName = generation->Name();
        type.m_modelGuid = FileGuid(generation->Identity().modelId);
        type.m_allowLegacySource = true;
        component->AddFoliageType(type);
        const auto width = size_t(std::ceil(std::sqrt(double(count))));
        for (size_t i = 0; i < count; ++i)
        {
            FoliageInstance instance;
            instance.m_position = {float(i % width) * 2.0f, 0, 5.0f + float(i / width) * 2.0f};
            instance.m_scale = {0.25f, 0.25f, 0.25f};
            component->AddFoliageInstance(instance);
        }
    }
    if (!entity) return PreconditionFailed("foliage.entity", "Fixture entity absent in current scene");
    auto* component = entity->GetComponent<FoliageComponent>();
    if (!component) return Fail("foliage.component", "Fixture component absent");
    if (action == "measure")
    {
        if (ctx.parts.size() != 3 || (ctx.parts[2] != "cached" && ctx.parts[2] != "baseline"))
            return InvalidArguments("measure <cached|baseline>");
        component->ConfigureCullingProbe(ctx.parts[2] == "cached");
    }
    if (action == "edit")
    {
        const auto& instances = component->GetFoliageInstances();
        if (instances.empty()) return Fail("foliage.empty", "No instance to edit");
        if (!component->UpdateFoliageInstance(instances.front().m_temporalIdentity,
                {100000, 0, 5}, {15, 45, 90}, {-1, 2, 0.5f})) return Fail("foliage.edit", "Edit rejected");
    }
    if (action == "remove") component->RemoveFoliageInstance(0);
    if (action == "rebind") component->RebindFoliageType(0);
    if (action == "save")
    {
        if (ctx.parts.size() != 3) return InvalidArguments("save <asset-name>");
        component->SaveFoliageAsset(ce::paths::relative("Foliage"), std::wstring(ctx.parts[2].begin(), ctx.parts[2].end()));
    }
    component->PollAssetBindings();
    auto data = CommandData::Object();
    data.Set("playing", CommandData::Bool(SceneManagers->IsGameStart()));
    data.Set("binding", CommandData::Int(int(component->GetAssetBindingStatus(0))));
    data.Set("count", CommandData::Int(component->GetFoliageInstances().size()));
    data.Set("cacheBytes", CommandData::Int(component->CullingCacheBytes()));
    data.Set("transforms", CommandData::Int(component->CullingProbeTransforms()));
    auto culling = CommandData::Array();
    for (const double value : component->CullingProbeSamples()) culling.Append(CommandData::Double(value));
    data.Set("cullingMs", std::move(culling));
    auto frames = CommandData::Array();
    for (const double value : component->CullingProbeFrameSamples()) frames.Append(CommandData::Double(value));
    data.Set("gameFrameIntervalMs", std::move(frames));
    if (action == "samples") return Ok("Actual FoliageSystem timing samples", std::move(data));
    data.Set("initialized", CommandData::Bool(component->HasLifecycleState(Component::State_Initialized)));
    data.Set("componentEnabled", CommandData::Bool(component->IsEnabled()));
    data.Set("ownerEnabled", CommandData::Bool(entity->IsEnabled()));
    if (auto* renderScene = SceneManagers->GetRenderScene())
        data.Set("primitiveProxies", CommandData::Int(renderScene->GetResourceCounts().proxies));
    data.Set("savedAsset", CommandData::Bool(component->m_foliageAssetGuid != nullFileGuid));
    const auto& instances = component->GetFoliageInstances();
    const auto& types = component->GetFoliageTypes();
    const auto* camera = scene->Cameras().GetPrimaryCamera();
    const auto frustum = camera ? camera->TryGetRenderFrustum() : std::optional<math::bounding_frustum>{};
    data.Set("hasFrustum", CommandData::Bool(frustum.has_value()));
    size_t visible = 0, mismatches = 0;
    // Status verification is deliberately outside the timed production update.
    for (const auto& instance : instances)
    {
        if (instance.m_foliageTypeID >= types.size()) continue;
        const auto& type = types[instance.m_foliageTypeID];
        const auto* bounds = type.m_meshDescriptor ? &type.m_meshDescriptor->bounds
            : type.m_modelGeneration && type.m_modelMeshIndex < type.m_modelGeneration->Meshes().size()
                ? &type.m_modelGeneration->Meshes()[type.m_modelMeshIndex].bounds : nullptr;
        bool expected = bounds == nullptr;
        if (bounds && SceneManagers->IsGameStart())
        {
            const auto world = math::transform(*bounds, instance.m_worldMatrix);
            expected = frustum && !world.is_empty() && !math::intersects(*frustum, world);
        }
        if (expected != instance.m_isCulled) ++mismatches;
        if (!instance.m_isCulled) ++visible;
    }
    data.Set("visibilityMismatches", CommandData::Int(mismatches));
    data.Set("visible", CommandData::Int(visible));
    if (!instances.empty())
    {
        data.Set("firstIdentity", CommandData::Int(instances.front().m_temporalIdentity));
        data.Set("firstX", CommandData::Double(instances.front().m_position.x));
    }
    if (!types.empty())
    {
        const auto& type = types.front();
        data.Set("hasMeshDescriptor", CommandData::Bool(bool(type.m_meshDescriptor)));
        data.Set("hasGeneration", CommandData::Bool(bool(type.m_modelGeneration)));
        data.Set("hasGraphSource", CommandData::Bool(bool(type.m_graphMaterialSource)));
        RHIModelMeshView view;
        const bool metadata = type.m_meshDescriptor ? BuildRHIModelMeshView(*type.m_meshDescriptor, view)
            : type.m_modelGeneration && BuildRHIModelMeshView(*type.m_modelGeneration, type.m_modelMeshIndex, view);
        data.Set("meshMetadataReady", CommandData::Bool(metadata));
        const auto* bounds = type.m_meshDescriptor ? &type.m_meshDescriptor->bounds
            : type.m_modelGeneration && type.m_modelMeshIndex < type.m_modelGeneration->Meshes().size()
                ? &type.m_modelGeneration->Meshes()[type.m_modelMeshIndex].bounds : nullptr;
        if (bounds) data.Set("localExtentX", CommandData::Double(bounds->extents.x));
    }
    return Ok("Actual component snapshot; timing samples originate in FoliageSystem frames", std::move(data));
}
