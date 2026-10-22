#include "EnhancedLivePipelineDesc.h"
#include "../../../EngineDiagnostics/ProfileScope.h"

#include <stdio.h>
#include "../../../Utility_Framework/StandardLibrary.h"

namespace
{
    LivePassNode MakeTestNode(std::string name)
    {
        LivePassNode node{};
        node.name = std::move(name);
        node.declare = [](LiveBlackboard&, EnhancedRenderGraph&,
            const EnhancedFrameContext&, const LiveFrameBinding&) {};
        return node;
    }

    bool ContainsAll(const std::string& text,
        std::initializer_list<const char*> needles)
    {
        for (const char* needle : needles)
        {
            if (std::string::npos == text.find(needle)) return false;
        }
        return true;
    }
}

void LivePipelineDesc::AddNode(LivePassNode node)
{
    if (!m_currentContributor.empty())
    {
        node.m_contributorName = m_currentContributor;
        node.m_contributorStage = m_currentContributionStage;
    }
#if !CE_SHIPPING
    node.declarationMarker = ce::intern_runtime_marker("RenderDeclare." + node.name, ce::marker_kind::cpu_scope);
#endif
    m_nodes.push_back(std::move(node));
    ++m_revision;
    m_compiledRevision = 0;
}

bool LiveResources::IsBuiltin(std::string_view slot) noexcept
{
    static constexpr auto names = std::to_array<std::string_view>({
        LiveSlots::kGBufferDiffuse,
        LiveSlots::kGBufferMetalRough,
        LiveSlots::kGBufferNormal,
        LiveSlots::kGBufferEmissive,
        LiveSlots::kGBufferBitmask,
        LiveSlots::kGBufferDepth,
        LiveSlots::kDecalBaseDiffuse,
        LiveSlots::kDecalBaseOrm,
        LiveSlots::kDecalBaseNormal,
        LiveSlots::kVisibility,
        LiveSlots::kShadowStaticDepths,
        LiveSlots::kShadowFirstLists,
        LiveSlots::kShadowOccluderDepths,
        LiveSlots::kShadowFinalLists,
        LiveSlots::kMeteorGeometry,
        LiveSlots::kMeteorFirstList,
        LiveSlots::kMeteorOccluderDepth,
        LiveSlots::kMeteorLayerCommands,
        LiveSlots::kMeteorMaterialTiles,
        LiveSlots::kMeteorMaterialArguments,
        LiveSlots::kMeteorBlendedCommands,
        LiveSlots::kMeteorBlendedList,
        LiveSlots::kMeteorFinalList,
        LiveSlots::kTemporalMotion,
        LiveSlots::kTemporalReactive,
        LiveSlots::kTemporalTransparency,
        LiveSlots::kTemporalResponsive,
        LiveSlots::kTemporalDepth,
        LiveSlots::kHudlessLdr,
        LiveSlots::kUiLayer,
        LiveSlots::kShadowMap,
        LiveSlots::kAmbientOcclusion,
        LiveSlots::kLitColor,
        LiveSlots::kDisplayLdr,
    });
    return std::find(names.begin(), names.end(), slot) != names.end();
}

void RenderFeatureContributors::Contribute(RenderFeatureStage stage, LivePipelineDesc& pipeline,
                                           const RenderFeatureContext& context) const
{
    if (!pipeline.m_currentContributor.empty())
    {
        throw std::logic_error("Nested render feature contribution is not supported.");
    }
    for (const Entry& entry : m_entries)
    {
        if (entry.stage != stage)
        {
            continue;
        }
        pipeline.m_currentContributor = entry.name;
        pipeline.m_currentContributionStage = stage;
        try
        {
            entry.contributor->Contribute(pipeline, context);
        }
        catch (...)
        {
            pipeline.m_currentContributor = {};
            throw;
        }
        pipeline.m_currentContributor = {};
    }
}

bool LiveResourceContract::Accepts(const LiveResourceContract& producer) const noexcept
{
    const bool compatibleExtent =
        extent == LiveResourceExtent::Independent || extent == producer.extent ||
        (extent == LiveResourceExtent::RenderOrDisplay &&
         (producer.extent == LiveResourceExtent::Render || producer.extent == LiveResourceExtent::Display));
    return kind == producer.kind && cardinality == producer.cardinality &&
           (semantic == LiveResourceSemantic::Generic || semantic == producer.semantic) &&
           (format == RHIFormat::Unknown || format == producer.format) && compatibleExtent &&
           (optional || !producer.optional) && (!listSize || listSize == producer.listSize);
}

bool LivePipelineDesc::Validate(std::string& outError) const
{
    outError.clear();
    return m_compiledRevision == m_revision || CompileFeaturePlan(outError);
}

bool LivePipelineDesc::CompileFeaturePlan(std::string& outError) const
{
    // Assembly-only allocations. The resulting indices/dependencies and owned
    // contracts are immutable throughout frame execution and reused by every view.
    struct PublishedSlot
    {
        uint32_t slot{}, writer{};
        std::string publisher;
        LiveResourceContract contract;
        bool typed{};
        std::vector<uint32_t> readers;
    };
    std::unordered_map<std::string, PublishedSlot> published;
    std::unordered_map<std::string, uint32_t> nodeNames;
    m_planNodes.clear();
    m_planResources.clear();
    m_planConsumers.clear();
    m_planNodes.reserve(m_nodes.size());
    m_compiledRevision = 0;
    outError.clear();

    for (uint32_t nodeIndex = 0; nodeIndex < m_nodes.size(); ++nodeIndex)
    {
        const LivePassNode& node = m_nodes[nodeIndex];
        if (node.name.empty())
        {
            outError = "이름 없는 노드가 있다";
            return false;
        }
        if (!nodeNames.emplace(node.name, nodeIndex).second)
        {
            outError = "Duplicate feature node '" + node.name + "'.";
            return false;
        }
        if (!node.declare)
        {
            outError = "노드 '" + node.name + "'에 declare가 없다";
            return false;
        }
        if (!node.m_contributorName.empty())
        {
            const std::string prefix = "Contributor '" + node.m_contributorName + "' at " +
                                       RenderFeatureStageName(node.m_contributorStage) + " node '" + node.name + "'";
            const std::string_view writable = node.m_contributorStage == RenderFeatureStage::BeforePresent
                                                  ? LiveSlots::kDisplayLdr
                                                  : LiveSlots::kLitColor;
            const auto validateWrites = [&](const std::vector<std::string>& slots)
            {
                for (const std::string& slot : slots)
                {
                    if (LiveResources::IsBuiltin(slot) && slot != writable)
                    {
                        outError = prefix + " cannot replace read-only built-in slot '" + slot + "'.";
                        return false;
                    }
                }
                return true;
            };
            if (!validateWrites(node.writes) || !validateWrites(node.modifies))
            {
                return false;
            }
            if ((!node.reads.empty() && node.inputContracts.empty()) ||
                (!node.writes.empty() && node.outputContracts.empty()) ||
                (!node.modifies.empty() && node.updateContracts.empty()))
            {
                outError = prefix + " requires typed Inputs, Outputs and Updates contracts.";
                return false;
            }
        }
        m_planNodes.emplace_back();
        CompiledNode& compiled = m_planNodes.back();
        compiled.node = nodeIndex;
        const auto addDependency = [&](uint32_t predecessor)
        {
            if (predecessor != nodeIndex && std::find(compiled.dependencies.begin(), compiled.dependencies.end(),
                                                      predecessor) == compiled.dependencies.end())
            {
                compiled.dependencies.push_back(predecessor);
            }
        };
        for (const std::string& dependency : node.dependsOn)
        {
            const auto found = nodeNames.find(dependency);
            if (found == nodeNames.end() || found->second >= nodeIndex)
            {
                outError = "Feature '" + node.name + "' requires an earlier CPU dependency '" + dependency + "'.";
                return false;
            }
            addDependency(found->second);
        }
        const auto validateContractList =
            [&](const std::vector<std::string>& names, const std::vector<LiveResourceBinding>& contracts)
        {
            if (contracts.empty())
            {
                return true;
            }
            if (names.size() != contracts.size())
            {
                outError = "Feature '" + node.name + "' has incomplete typed resource contracts.";
                return false;
            }
            for (size_t index = 0; index < names.size(); ++index)
            {
                if (names[index] != contracts[index].name || names[index].empty())
                {
                    outError = "Feature '" + node.name + "' has inconsistent typed slot names.";
                    return false;
                }
                const LiveResourceContract& contract = contracts[index].contract;
                if ((contract.kind == RGResourceKind::Buffer &&
                     (contract.format != RHIFormat::Unknown || contract.extent != LiveResourceExtent::Independent)) ||
                    (contract.cardinality == LiveResourceCardinality::Single && contract.listSize))
                {
                    outError = "Feature '" + node.name + "' has an invalid contract for '" + names[index] + "'.";
                    return false;
                }
            }
            return true;
        };
        if (!validateContractList(node.reads, node.inputContracts) ||
            !validateContractList(node.writes, node.outputContracts) ||
            !validateContractList(node.modifies, node.updateContracts))
        {
            return false;
        }
        const auto consume = [&](const std::vector<std::string>& names,
                                 const std::vector<LiveResourceBinding>& contracts,
                                 std::vector<CompiledResourceUse>& uses, bool modifies)
        {
            for (size_t index = 0; index < names.size(); ++index)
            {
                const std::string& name = names[index];
                const auto found = published.find(name);
                if (found == published.end())
                {
                    outError = "노드 '" + node.name + "'이 아직 발행되지 않은 슬롯 '" + name +
                               (modifies ? "'을(를) 수정한다" : "'을(를) 읽는다");
                    return false;
                }
                PublishedSlot& source = found->second;
                const bool typed = !contracts.empty();
                const LiveResourceContract contract = typed ? contracts[index].contract : source.contract;
                if (typed && (!source.typed || !contract.Accepts(source.contract)))
                {
                    outError = "Feature '" + node.name + "' has an incompatible resource contract for '" + name +
                               "' from '" + source.publisher + "' (kind/cardinality/semantic/format/extent/optional).";
                    return false;
                }
                if (std::any_of(uses.begin(), uses.end(),
                                [&](const CompiledResourceUse& use) { return use.slot == source.slot; }))
                {
                    outError = "Feature '" + node.name + "' repeats resource '" + name + "'.";
                    return false;
                }
                uses.push_back({source.slot, contract, typed});
                addDependency(source.writer);
                if (modifies)
                {
                    for (uint32_t reader : source.readers)
                    {
                        addDependency(reader);
                    }
                    source.readers.clear();
                    source.writer = nodeIndex;
                    source.contract = contract;
                    source.typed = typed || source.typed;
                }
                else
                {
                    source.readers.push_back(nodeIndex);
                    m_planConsumers[source.slot].push_back({nodeIndex, false});
                }
            }
            return true;
        };
        if (!consume(node.reads, node.inputContracts, compiled.inputs, false) ||
            !consume(node.modifies, node.updateContracts, compiled.updates, true))
        {
            return false;
        }
        for (size_t index = 0; index < node.writes.size(); ++index)
        {
            const std::string& name = node.writes[index];
            const auto existing = published.find(name);
            if (existing != published.end())
            {
                outError = "슬롯 '" + name + "'을(를) 노드 '" + existing->second.publisher + "'와(과) '" + node.name +
                           "'이 둘 다 발행한다 — 뒤엣것은 수정이므로 modifies로 적어야 한다";
                return false;
            }
            const bool typed = !node.outputContracts.empty();
            const LiveResourceContract contract = typed ? node.outputContracts[index].contract : LiveResourceContract{};
            const auto fallback =
                std::find_if(node.disabledPassthroughs.begin(), node.disabledPassthroughs.end(),
                             [&](const LivePassNode::DisabledPassthrough& value) { return value.output.name == name; });
            if ((node.active || node.requiredViewFlags) && fallback == node.disabledPassthroughs.end() &&
                !(typed && contract.optional))
            {
                outError = "노드 '" + node.name + "'은 꺼질 수 있는데 슬롯 '" + name +
                           "'을(를) 발행한다 — modifies 또는 명시적 disabled passthrough가 필요하다";
                return false;
            }
            const uint32_t slotIndex = static_cast<uint32_t>(m_planResources.size());
            m_planResources.emplace_back(name, contract);
            m_planConsumers.emplace_back();
            compiled.outputs.push_back({slotIndex, contract, typed});
            published.emplace(name, PublishedSlot{slotIndex, nodeIndex, node.name, contract, typed, {}});
        }
        for (const LivePassNode::DisabledPassthrough& fallback : node.disabledPassthroughs)
        {
            const auto output = published.find(fallback.output.name);
            const auto input = published.find(fallback.input.name);
            if (output == published.end() || output->second.writer != nodeIndex ||
                std::find(node.writes.begin(), node.writes.end(), fallback.output.name) == node.writes.end() ||
                input == published.end() || input->second.writer >= nodeIndex || !output->second.typed ||
                !input->second.typed || !fallback.output.contract.Accepts(output->second.contract) ||
                !output->second.contract.Accepts(fallback.output.contract) ||
                !fallback.input.contract.Accepts(input->second.contract) ||
                !output->second.contract.Accepts(input->second.contract) ||
                std::any_of(compiled.disabledPassthroughs.begin(), compiled.disabledPassthroughs.end(),
                            [&](const CompiledPassthrough& value) { return value.output == output->second.slot; }))
            {
                outError = "Feature '" + node.name + "' has an incompatible disabled passthrough '" +
                           fallback.input.name + "' -> '" + fallback.output.name + "'.";
                return false;
            }
            compiled.disabledPassthroughs.push_back({output->second.slot, input->second.slot});
            addDependency(input->second.writer);
            input->second.readers.push_back(nodeIndex);
            m_planConsumers[input->second.slot].push_back({nodeIndex, true});
        }
        std::sort(compiled.dependencies.begin(), compiled.dependencies.end());
    }
    // A description can be replaced at the same address with the same authored
    // node count/revision. A unique binding ID prevents an existing blackboard
    // from reusing the slot order of that earlier description.
    static std::atomic<uint64_t> nextBindingId{1};
    m_planBindingId = nextBindingId.fetch_add(1, std::memory_order_relaxed);
    m_compiledRevision = m_revision;
    return true;
}

LivePipelineDesc::CompiledPlanStats LivePipelineDesc::GetCompiledPlanStats() const
{
    CompiledPlanStats result{m_compiledRevision, m_planNodes.size(), m_planResources.size(), 0};
    for (const CompiledNode& node : m_planNodes)
    {
        result.dependencies += node.dependencies.size();
    }
    return result;
}

std::span<const uint32_t> LivePipelineDesc::GetNodeDependencies(size_t index) const
{
    return index < m_planNodes.size() ? std::span<const uint32_t>{m_planNodes[index].dependencies}
                                      : std::span<const uint32_t>{};
}

bool LivePipelineDesc::InitializeAll(const EnhancedFrameContext& context,
    uint32_t viewCount, std::string& outError) const
{
    if (!Validate(outError))
    {
        return false;
    }
    for (const LivePassNode& node : m_nodes)
    {
        const uint32_t rounds = node.perView ? viewCount : 1u;
        for (uint32_t view = 0; view < rounds; ++view)
        {
            static const bool trace = [] {
                char* value = nullptr;
                std::size_t size = 0;
                if (_dupenv_s(&value, &size, "CE_RENDER_PROGRESS_TRACE") != 0) return false;
                const bool enabled = value && std::string_view(value) == "1";
                std::free(value);
                return enabled;
            }();
            if (trace)
            {
                std::printf("[render.pass.progress] phase=initialize.begin node=%s view=%u\n", node.name.c_str(), view);
                std::fflush(stdout);
            }


            if (node.initialize)
            {
                if (!node.initialize(context, outError, view))
                {
                    outError = "노드 '" + node.name + "' 초기화 실패: " + outError;
                    return false;
                }
                continue;
            }

            // 패스가 없는 노드(live_present)는 초기화할 것이 없다.
            if (!node.instance) continue;

            EnhancedRenderPass* pass = node.instance(view);
            if (nullptr == pass) continue;

            if (!pass->Initialize(context, outError))
            {
                outError = "노드 '" + node.name + "' 초기화 실패: " + outError;
                return false;
            }
        }
    }
    return true;
}

bool LivePipelineDesc::PrepareAll(const EnhancedFrameContext& context,
    uint32_t viewIndex, std::string& outError) const
{
    for (const LivePassNode& node : m_nodes)
    {
        if ((context.viewFlags & node.requiredViewFlags) != node.requiredViewFlags)
        {
            continue;
        }
        if (node.prepare)
        {
            if (!node.prepare(context, outError, viewIndex))
            {
                outError = "노드 '" + node.name + "' 프레임 준비 실패: " + outError;
                return false;
            }
            continue;
        }

        if (!node.instance) continue;

        EnhancedRenderPass* pass = node.instance(viewIndex);
        if (nullptr == pass) continue;

        if (!pass->PrepareFrame(context, outError))
        {
            outError = "노드 '" + node.name + "' 프레임 준비 실패: " + outError;
            return false;
        }
    }
    return true;
}

void LivePipelineDesc::ShutdownAll(uint32_t viewCount) const
{
    // 역순. 뒤에 선 것이 앞엣것의 자원을 참조할 수 있으므로 푸는 순서는
    // 세운 순서의 반대다 — 이것을 사람이 두 곳에 나눠 적던 자리가 접점 4였다.
    for (auto it = m_nodes.rbegin(); it != m_nodes.rend(); ++it)
    {
        const LivePassNode& node = *it;

        if (node.shutdown)
        {
            node.shutdown();
            continue;
        }

        if (!node.instance) continue;

        const uint32_t rounds = node.perView ? viewCount : 1u;
        for (uint32_t view = 0; view < rounds; ++view)
        {
            if (EnhancedRenderPass* pass = node.instance(view)) pass->Shutdown();
        }
    }
}

void LivePipelineDesc::ValidatePublishedResources(const CompiledNode& node, const LiveBlackboard& blackboard,
                                                  const EnhancedRenderGraph& graph,
                                                  const EnhancedFrameContext& context) const
{
    const auto validate = [&](const CompiledResourceUse& use)
    {
        if (!use.typed)
        {
            return;
        }
        const LiveResourceContract& contract = use.contract;
        const std::span<const RGHandle> handles = blackboard.BoundHandles(use.slot);
        const auto fail = [&](std::string_view reason)
        {
            throw std::runtime_error("Feature '" + m_nodes[node.node].name + "' published invalid slot '" +
                                     m_planResources[use.slot].name + "': " + std::string{reason});
        };
        if (handles.empty())
        {
            if (!contract.optional)
            {
                fail("required resource is absent");
            }
            return;
        }
        if ((contract.cardinality == LiveResourceCardinality::Single && handles.size() != 1) ||
            (contract.listSize && handles.size() != contract.listSize))
        {
            fail("resource cardinality differs from its contract");
        }
        for (const RGHandle handle : handles)
        {
            if (!handle.IsValid())
            {
                if (!contract.optional)
                {
                    fail("required handle is invalid");
                }
                continue;
            }
            RGResourceDescription description{};
            if (!graph.DescribeResource(handle, description) || description.kind != contract.kind)
            {
                fail("handle kind, epoch or version is invalid");
            }
            if (contract.kind == RGResourceKind::Buffer)
            {
                continue;
            }
            if (contract.format != RHIFormat::Unknown && description.format != contract.format)
            {
                fail("texture format differs from its contract");
            }
            const uint32_t displayWidth = context.displayWidth ? context.displayWidth : context.width;
            const uint32_t displayHeight = context.displayHeight ? context.displayHeight : context.height;
            const bool renderExtent = description.width == context.width && description.height == context.height;
            const bool displayExtent = description.width == displayWidth && description.height == displayHeight;
            switch (contract.extent)
            {
            case LiveResourceExtent::Render:
                if (!renderExtent)
                {
                    fail("render extent differs from its contract");
                }
                break;
            case LiveResourceExtent::HalfRender:
                if (description.width != context.width / 2 + context.width % 2 ||
                    description.height != context.height / 2 + context.height % 2)
                {
                    fail("half-render extent differs from its contract");
                }
                break;
            case LiveResourceExtent::Display:
                if (!displayExtent)
                {
                    fail("display extent differs from its contract");
                }
                break;
            case LiveResourceExtent::RenderOrDisplay:
                if (!renderExtent && !displayExtent)
                {
                    fail("texture is neither render nor display extent");
                }
                break;
            case LiveResourceExtent::Independent:
                break;
            }
        }
    };
    for (const CompiledResourceUse& output : node.outputs)
    {
        validate(output);
    }
    for (const CompiledResourceUse& update : node.updates)
    {
        validate(update);
    }
}

void LivePipelineDesc::DeclareAll(LiveBlackboard& blackboard, EnhancedRenderGraph& graph,
    const EnhancedFrameContext& context, const LiveFrameBinding& binding,
    const std::function<void(const LivePassNode&, const LiveBlackboard&)>& afterDeclare) const
{
    using Observer = std::function<void(const LivePassNode&, const LiveBlackboard&)>;
    const auto invoke = [](const void* observer, const LivePassNode& node, const LiveBlackboard& values)
    { (*static_cast<const Observer*>(observer))(node, values); };
    DeclareAllImpl(blackboard, graph, context, binding, &afterDeclare, afterDeclare ? +invoke : nullptr);
}

void LivePipelineDesc::DeclareAllImpl(LiveBlackboard& blackboard, EnhancedRenderGraph& graph,
                                      const EnhancedFrameContext& context, const LiveFrameBinding& binding,
                                      const void* observer,
                                      void (*afterDeclare)(const void*, const LivePassNode&,
                                                           const LiveBlackboard&)) const
{
    ce::profile_scope declarationProfile{ce::marker<"RenderGraphDeclare">()};
    ce::profile_instant(RenderContainerReuseEnabled() ? ce::marker<"RenderContainerReuse.On">()
                                                      : ce::marker<"RenderContainerReuse.Off">());
    if (m_compiledRevision != m_revision)
    {
        std::string error;
        if (!CompileFeaturePlan(error))
        {
            throw std::runtime_error(error);
        }
    }
    blackboard.BindPlan(this, m_planBindingId, m_planResources);
    const bool declarationPackets = !graph.IsProgramBindingOpen();
    if (declarationPackets)
    {
        graph.BeginDeclarationPackets();
    }
    try
    {
        for (const CompiledNode& compiled : m_planNodes)
        {
            const LivePassNode& node = m_nodes[compiled.node];
            if (!node.IsActive(binding))
            {
                for (const CompiledPassthrough& fallback : compiled.disabledPassthroughs)
                {
                    blackboard.CopyBoundSlot(fallback.output, fallback.input);
                }
                ValidatePublishedResources(compiled, blackboard, graph, context);
                continue;
            }
            if (declarationPackets)
            {
                graph.NextDeclarationPacket();
            }
            ce::profile_scope profile{node.declarationMarker};
            if (!node.m_contributorName.empty())
            {
                blackboard.BeginFeatureWrites(node.name);
                for (const CompiledResourceUse& output : compiled.outputs)
                {
                    blackboard.AllowFeatureWrite(output.slot);
                }
                for (const CompiledResourceUse& update : compiled.updates)
                {
                    blackboard.AllowFeatureWrite(update.slot);
                }
            }
            {
                ce::profile_scope body{ce::marker<"RenderGraphDeclareNodeBody">()};
                node.declare(blackboard, graph, context, binding);
            }
            blackboard.EndFeatureWrites();
            ValidatePublishedResources(compiled, blackboard, graph, context);
            if (afterDeclare)
            {
                ce::profile_scope callback{ce::marker<"RenderGraphDeclareAfterNode">()};
                afterDeclare(observer, node, blackboard);
            }
        }
        if (declarationPackets)
        {
            graph.FinishDeclarationPackets();
        }
    }
    catch (...)
    {
        blackboard.EndFeatureWrites();
        if (declarationPackets)
        {
            graph.CancelDeclarationPackets();
        }
        throw;
    }
}

bool LivePipelineDesc::HasConsumer(std::string_view slot, const EnhancedFrameContext& context,
                                   const LiveFrameBinding& binding) const
{
    if (m_compiledRevision != m_revision)
    {
        std::string error;
        if (!CompileFeaturePlan(error))
        {
            throw std::runtime_error(error);
        }
    }
    for (size_t index = 0; index < m_planResources.size(); ++index)
    {
        const LiveResourceBinding& resource = m_planResources[index];
        if (resource.name != slot)
        {
            continue;
        }
        for (const CompiledConsumer& consumer : m_planConsumers[index])
        {
            const LivePassNode& node = m_nodes[consumer.node];
            if (node.IsActive(binding) != consumer.whenDisabled &&
                (consumer.whenDisabled || !node.consumes || node.consumes(resource.name, context, binding)))
            {
                return true;
            }
        }
        break;
    }
    return false;
}

std::string LivePipelineDesc::Dump() const
{
    const auto joinSlots = [](const std::vector<std::string>& slots) -> std::string
    {
        std::string joined;
        for (size_t i = 0; i < slots.size(); ++i)
        {
            if (0 != i) joined += ", ";
            joined += slots[i];
        }
        return joined;
    };

    std::string text = "LivePipelineDesc — " + std::to_string(m_nodes.size()) + " nodes\n";
    for (size_t i = 0; i < m_nodes.size(); ++i)
    {
        const LivePassNode& node = m_nodes[i];

        text += "  " + std::to_string(i) + ". " + node.name;
        if (node.active) text += node.IsActive() ? "  [active]" : "  [inactive]";
        text += "\n";

        if (!node.reads.empty())    text += "       reads: " + joinSlots(node.reads) + "\n";
        if (!node.writes.empty())   text += "       writes: " + joinSlots(node.writes) + "\n";
        if (!node.modifies.empty()) text += "       modifies: " + joinSlots(node.modifies) + "\n";
    }
    return text;
}

bool LivePipelineDesc::RunSelfTest(std::string& outLog)
{
    outLog.clear();
    constexpr uint32_t kSelfTestCases = 15;
    uint32_t passed = 0;
    uint32_t total = 0;

    const auto record = [&](bool ok, const char* label, const std::string& detail)
    {
        ++total;
        if (ok) ++passed;
        outLog += "[" + std::to_string(total) + "/" + std::to_string(kSelfTestCases) + "] " + label + " — "
            + (ok ? "통과" : "실패");
        if (!detail.empty()) outLog += " (" + detail + ")";
        outLog += "\n";
    };

    const auto expectFailure = [&](LivePipelineDesc desc, const char* label,
        std::initializer_list<const char*> expected)
    {
        std::string error;
        const bool rejected = !desc.Validate(error);
        const bool matched = rejected && ContainsAll(error, expected);
        record(matched, label, rejected ? error : "잘못된 기술을 허용함");
    };

    // 정상 계약과 Dump 계약은 같은 작은 기술을 공유한다. source가 처음
    // 발행하고, optional은 꺼져도 기존 값을 보존하도록 modifies만 가지며,
    // consumer가 그 결과를 읽어 새 슬롯을 발행한다.
    LivePipelineDesc valid;
    LivePassNode source = MakeTestNode("source");
    source.writes = { "scene" };
    valid.AddNode(std::move(source));

    LivePassNode optional = MakeTestNode("optional");
    optional.modifies = { "scene" };
    optional.active = [] { return false; };
    valid.AddNode(std::move(optional));

    LivePassNode consumer = MakeTestNode("consumer");
    consumer.reads = { "scene" };
    consumer.writes = { "display" };
    valid.AddNode(std::move(consumer));

    std::string validationError;
    record(valid.Validate(validationError), "정상 read/write/optional-modify 기술",
        validationError);

    {
        LivePipelineDesc desc;
        desc.AddNode(MakeTestNode(""));
        expectFailure(std::move(desc), "이름 없는 노드 거부", { "이름 없는" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("optional-writer");
        node.active = [] { return true; };
        node.writes = { "new-slot" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "비활성 가능 노드의 새 슬롯 발행 거부",
            { "optional-writer", "new-slot", "modifies" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("reader");
        node.reads = { "missing" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "미발행 슬롯 read 거부",
            { "reader", "missing", "읽는다" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node = MakeTestNode("modifier");
        node.modifies = { "missing" };
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "미발행 슬롯 modify 거부",
            { "modifier", "missing", "수정한다" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode first = MakeTestNode("first");
        first.writes = { "color" };
        desc.AddNode(std::move(first));
        LivePassNode second = MakeTestNode("second");
        second.writes = { "color" };
        desc.AddNode(std::move(second));
        expectFailure(std::move(desc), "중복 슬롯 발행 거부",
            { "color", "first", "second", "modifies" });
    }
    {
        LivePipelineDesc desc;
        LivePassNode node{};
        node.name = "no-declare";
        desc.AddNode(std::move(node));
        expectFailure(std::move(desc), "declare 없는 노드 거부",
            { "no-declare", "declare" });
    }

    const std::string dump = valid.Dump();
    record(ContainsAll(dump,
        { "3 nodes", "0. source", "1. optional  [inactive]", "2. consumer",
          "writes: scene", "modifies: scene", "reads: scene", "writes: display" }),
        "덤프 순서·활성 상태·슬롯 연결", "");

    // 조회 문자열의 수명과 저장 키의 수명을 분리해도 핸들 값과 목록 소유권은 같아야 한다.
    const RGHandle first{17, 3, RGResourceKind::Buffer, 41};
    const RGHandle second{23, 9, RGResourceKind::Texture, 43};
    const auto sameHandle = [](RGHandle left, RGHandle right)
    {
        return left.index == right.index && left.version == right.version &&
            left.kind == right.kind && left.epoch == right.epoch;
    };
    const auto sameSlot = [&](const LiveBlackboard& board, std::string_view slot, RGHandle expected)
    {
        const auto handles = board.GetList(slot);
        return sameHandle(board.Get(slot), expected) && handles.size() == 1 &&
            sameHandle(handles.front(), expected);
    };
    LiveBlackboard blackboard;
    {
        const std::string key = "METEOR.Test.OwnedSlotNameAfterSourceDestruction";
        std::string query;
        {
            std::string source = key;
            blackboard.Set(source, first);
            query = source;
            source.assign(source.size(), 'x');
        }
        record(sameSlot(blackboard, key, first) && sameSlot(blackboard, query, first) &&
                sameSlot(blackboard, "METEOR.Test.OwnedSlotNameAfterSourceDestruction", first) &&
                !blackboard.Get("missing-slot").IsValid() && blackboard.GetList("missing-slot").empty(),
            "블랙보드 키 소유·다른 주소의 같은 이름·누락 조회", "");
    }
    {
        const std::string key = "METEOR.Test.BoundedView";
        const std::string padded = "prefix/" + key + "/suffix";
        const std::string_view query{padded.data() + 7, key.size()};
        blackboard.Set(key, first);
        record(sameSlot(blackboard, query, first) && !blackboard.Get(padded).IsValid() &&
                blackboard.GetList(padded).empty() && !blackboard.Get(query.substr(0, query.size() - 1)).IsValid() &&
                blackboard.GetList(query.substr(0, query.size() - 1)).empty(),
            "블랙보드 종료 문자 없는 부분 문자열의 정확한 범위", "");
    }
    {
        const std::string key{"METEOR\0Slot", 11};
        blackboard.Set(key, first);
        blackboard.Set("METEOR", second);
        const std::string differentAddress = key;
        record(sameSlot(blackboard, differentAddress, first) && sameSlot(blackboard, "METEOR", second),
            "블랙보드 중간 NUL을 포함한 이름과 접두 이름 구분", "");
    }
    {
        const std::string_view empty;
        const bool missing = !blackboard.Get(empty).IsValid() && blackboard.GetList(empty).empty();
        blackboard.Set("", first);
        record(missing && sameSlot(blackboard, empty, first) && sameSlot(blackboard, "", first),
            "블랙보드 비어 있는 view의 누락·발행 조회", "");
    }
    {
        const std::string padded = "prefix/METEOR.Test.List/suffix";
        const std::string_view query{padded.data() + 7, 16};
        blackboard.SetList("METEOR.Test.List", {first, second});
        auto copied = blackboard.GetList(query);
        const bool copiedValues = copied.size() == 2 && sameHandle(copied[0], first) && sameHandle(copied[1], second);
        copied.clear();
        const auto preserved = blackboard.GetList(query);
        bool rejected = false;
        try
        {
            (void)blackboard.Get(query);
        }
        catch (const std::runtime_error&)
        {
            rejected = true;
        }
        blackboard.SetList("empty-list", {});
        record(copiedValues && preserved.size() == 2 && sameHandle(preserved[0], first) &&
                sameHandle(preserved[1], second) && rejected && blackboard.GetList("empty-list").empty() &&
                !blackboard.Get("empty-list").IsValid(),
            "블랙보드 목록 값 복사·다중 값의 단일 조회 거부·빈 목록", "");
    }
    {
        blackboard.Reset();
        bool cleared = true;
        const std::array<std::string_view, 7> keys{
            "METEOR.Test.OwnedSlotNameAfterSourceDestruction", "METEOR.Test.BoundedView",
            std::string_view{"METEOR\0Slot", 11}, "METEOR", "", "METEOR.Test.List", "empty-list"};
        for (const auto key : keys)
        {
            if (blackboard.Get(key).IsValid() || !blackboard.GetList(key).empty())
            {
                cleared = false;
            }
        }
        blackboard.Set("METEOR.Test.OwnedSlotNameAfterSourceDestruction", second);
        record(cleared && sameSlot(blackboard, "METEOR.Test.OwnedSlotNameAfterSourceDestruction", second),
            "블랙보드 Reset 뒤 이전 슬롯 제거·새 핸들 발행", "");
    }
    {
        const std::string scalarKey = "METEOR.Test.OwnedScalarAfterQueryDestruction";
        const std::string listKey = "METEOR.Test.OwnedListAfterQueryDestruction";
        std::vector<RGHandle> copiedList;
        bool preserved = true;
        {
            LiveBlackboard board;
            for (uint32_t index = 0; index < 300; ++index)
            {
                board.Set("METEOR.Test.Warmup." + std::to_string(index), first);
            }
            // 큰 표의 버킷을 유지한 채 소수 슬롯만 발행해도 조회 문자열의 수명을 빌려서는 안 된다.
            board.Reset();
            for (uint32_t cycle = 0; cycle < 3; ++cycle)
            {
                const RGHandle current = cycle % 2 == 0 ? first : second;
                const RGHandle other = cycle % 2 == 0 ? second : first;
                board.Set(scalarKey, current);
                board.SetList(listKey, {current, other});
                {
                    std::string scalarQuery = scalarKey;
                    std::string listQuery = listKey;
                    preserved = sameSlot(board, scalarQuery, current) &&
                        sameHandle(board.Get(std::string{scalarKey}), current) && preserved;
                    copiedList = board.GetList(listQuery);
                    const auto temporaryList = board.GetList(std::string{listKey});
                    preserved = temporaryList.size() == 2 && sameHandle(temporaryList[0], current) &&
                        sameHandle(temporaryList[1], other) && preserved;
                    // 같은 버퍼의 내용 변경과 용량을 넘는 재할당을 거친 뒤 조회 원본을 파괴한다.
                    scalarQuery[0] = 'x';
                    listQuery[0] = 'y';
                    scalarQuery.assign(scalarQuery.capacity() + 1, 'x');
                    listQuery.assign(listQuery.capacity() + 1, 'y');
                }
                const auto storedList = board.GetList(listKey);
                preserved = sameSlot(board, scalarKey, current) && storedList.size() == 2 &&
                    sameHandle(storedList[0], current) && sameHandle(storedList[1], other) && preserved;
                board.Reset();
                preserved = !board.Get(scalarKey).IsValid() && board.GetList(scalarKey).empty() &&
                    board.GetList(listKey).empty() && copiedList.size() == 2 &&
                    sameHandle(copiedList[0], current) && sameHandle(copiedList[1], other) && preserved;
            }
        }
        record(preserved && copiedList.size() == 2 && sameHandle(copiedList[0], first) &&
                sameHandle(copiedList[1], second),
            "블랙보드 조회 원본 변경·재할당·파괴 뒤 Reset 반복·보드 파괴 뒤 목록 보존", "");
    }

    const bool complete = passed == total && total == kSelfTestCases;
    outLog += "LivePipelineDesc selftest " + std::to_string(passed) + "/"
        + std::to_string(total) + (complete ? " 통과\n" : " 실패\n");
    return complete;
}
