// Included after the text viewer helpers in EnhancedRenderDebugWindow.cpp.
namespace EnhancedRenderDebugUi
{
    using GraphSnapshot = EnhancedRenderGraph::DiagnosticSnapshot;
    using GraphViewModel = editor::compiled_graph::ViewModel;
    constexpr ImU32 kGraphRead = IM_COL32(74, 151, 203, 255);
    constexpr ImU32 kGraphWrite = IM_COL32(208, 155, 66, 255);
    constexpr ImU32 kGraphModify = IM_COL32(119, 177, 123, 255);

    uint8_t GraphAccessBits(const EnhancedRenderGraph::DiagnosticUsage& usage)
    {
        switch (usage.access)
        {
        case RGAccessMode::Read:
            return 1;
        case RGAccessMode::Write:
            return 2;
        case RGAccessMode::ReadWrite:
        case RGAccessMode::AtomicMin64:
            return 3;
        default:
            return usage.inferredWrite ? 2 : 1;
        }
    }

    const char* GraphResourceName(const GraphSnapshot& snapshot, uint32_t resource)
    {
        return resource < snapshot.resources.size() ? snapshot.resources[resource].name.c_str() : "Ordering only";
    }

    void DrawGraphRange(const GraphSnapshot& snapshot, uint32_t resource,
                        const RHITextureSubresourceRange& texture, const RHIBufferSubresourceRange& buffer)
    {
        if (resource >= snapshot.resources.size())
        {
            return;
        }
        if (snapshot.resources[resource].buffer)
        {
            if (buffer.size == UINT64_MAX)
            {
                ImGui::TextDisabled("  Bytes: offset %llu, remaining", static_cast<unsigned long long>(buffer.offset));
            }
            else
            {
                ImGui::TextDisabled("  Bytes: offset %llu, size %llu", static_cast<unsigned long long>(buffer.offset),
                                    static_cast<unsigned long long>(buffer.size));
            }
        }
        else
        {
            if (texture.mipCount == UINT32_MAX)
            {
                ImGui::TextDisabled("  Mips: first %u, remaining", texture.firstMip);
            }
            else
            {
                ImGui::TextDisabled("  Mips: first %u, count %u", texture.firstMip, texture.mipCount);
            }
            if (texture.layerCount == UINT32_MAX)
            {
                ImGui::TextDisabled("  Layers: first %u, remaining", texture.firstLayer);
            }
            else
            {
                ImGui::TextDisabled("  Layers: first %u, count %u", texture.firstLayer, texture.layerCount);
            }
            ImGui::TextDisabled("  Aspect: %s", texture.aspect == RHITextureAspect::All ? "all" :
                                texture.aspect == RHITextureAspect::Color ? "color" :
                                texture.aspect == RHITextureAspect::Depth ? "depth" :
                                texture.aspect == RHITextureAspect::Stencil ? "stencil" : "depth+stencil");
        }
    }

    void DrawGraphBarriers(const GraphSnapshot& snapshot, const auto& barriers)
    {
        for (const auto& barrier : barriers)
        {
            ImGui::TextWrapped("%s %s: %s -> %s%s [%s]", barrier.afterPass ? "After" : "Before",
                               GraphResourceName(snapshot, barrier.resource), GraphStateName(barrier.before),
                               GraphStateName(barrier.after), barrier.aliasing ? " [alias]" : barrier.uav ? " [UAV]" : "",
                               barrier.phase == RHITransitionPhase::Begin ? "split begin" :
                               barrier.phase == RHITransitionPhase::End ? "split end" : "full");
            if (!barrier.uav && !barrier.aliasing)
            {
                DrawGraphRange(snapshot, barrier.resource, barrier.textureRange, barrier.bufferRange);
            }
        }
    }

    void DrawGraphUsage(const GraphSnapshot& snapshot, const auto& usages, uint32_t& selectedResource)
    {
        for (size_t index = 0; index < usages.size(); ++index)
        {
            const auto& usage = usages[index];
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(GraphResourceName(snapshot, usage.resource), selectedResource == usage.resource))
            {
                selectedResource = usage.resource;
            }
            ImGui::TextDisabled("  %s | r%u v%u | %s", GraphAccessName(usage.access), usage.resource,
                                unsigned(usage.version), GraphStateName(usage.state));
            DrawGraphRange(snapshot, usage.resource, usage.textureRange, usage.bufferRange);
            ImGui::PopID();
        }
    }

    void DrawGraphInspector(const GraphSnapshot& snapshot, const GraphViewModel& model, uint32_t selectedPass,
                            uint32_t& selectedResource)
    {
        ImGui::TextUnformatted("Inspector");
        if (selectedPass < snapshot.passes.size())
        {
            const auto& pass = snapshot.passes[selectedPass];
            ImGui::Separator();
            ImGui::TextWrapped("%s", pass.name.c_str());
            ImGui::Text("Authored #%u | compiled %d%s", pass.authoredIndex, pass.compiledIndex,
                        pass.culled ? " | culled" : "");
            const auto wave =
                selectedPass < snapshot.dependencyWaves.size() ? snapshot.dependencyWaves[selectedPass] : -1;
            ImGui::TextWrapped("Dependency wave %d | CPU record cost %u", wave, pass.recordCost);
            ImGui::Text("Side effect %s | max slices %u", pass.sideEffect ? "yes" : "no", pass.maxSlices);
            ImGui::Text("Compute compatible %s | async requested %s", pass.computeCompatible ? "yes" : "no",
                        pass.asyncCompute ? "yes" : "no");
            for (const auto& entry : snapshot.queueExecution.schedule.entries)
            {
                if (entry.pass == selectedPass)
                {
                    ImGui::Text("Queue: %s", entry.queue == RHIQueueKind::Compute ? "Compute" : "Graphics");
                }
            }
            ImGui::TextUnformatted("Resource accesses");
            ImGui::PushID("PassUsages");
            DrawGraphUsage(snapshot, pass.usages, selectedResource);
            ImGui::PopID();
            for (size_t index = 0; index < pass.phases.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                const auto& phase = pass.phases[index];
                if (ImGui::TreeNode("Phase", "%s (%u iterations)", phase.name.c_str(), pass.repeatCount))
                {
                    DrawGraphUsage(snapshot, phase.usages, selectedResource);
                    ImGui::Text("First barriers %zu | repeat barriers %zu", phase.firstBarriers.size(),
                                phase.repeatBarriers.size());
                    ImGui::TextUnformatted("First iteration");
                    DrawGraphBarriers(snapshot, phase.firstBarriers);
                    ImGui::TextUnformatted("Following iterations");
                    DrawGraphBarriers(snapshot, phase.repeatBarriers);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if (ImGui::TreeNode("Barriers"))
            {
                DrawGraphBarriers(snapshot, pass.barriers);
                ImGui::TreePop();
            }
        }
        else
        {
            ImGui::TextWrapped("Select a pass header, access cell or DAG node.");
        }
        if (selectedResource < snapshot.resources.size())
        {
            const auto& resource = snapshot.resources[selectedResource];
            ImGui::Separator();
            ImGui::TextWrapped("r%u %s", selectedResource, resource.name.c_str());
            ImGui::Text("%s | %s | %s", resource.buffer ? "Buffer" : "Texture",
                        resource.imported ? "Imported" : "Transient", resource.used ? "used" : "unused");
            ImGui::TextWrapped("Versions %u | allocation %llu bytes", resource.versionCount,
                               static_cast<unsigned long long>(resource.allocationBytes));
            if (resource.used)
            {
                ImGui::Text("Lifetime: compiled #%u .. #%u", resource.firstUse, resource.lastUse);
            }
            if (resource.aliasGroup != UINT32_MAX)
            {
                ImGui::Text("Alias group %u", resource.aliasGroup);
            }
            ImGui::TextWrapped("State: %s -> %s", GraphStateName(resource.initialState),
                               GraphStateName(resource.finalState));
            for (uint32_t cell = model.resourceOffsets[selectedResource];
                 cell < model.resourceOffsets[selectedResource + 1]; ++cell)
            {
                const auto& access = model.cells[cell];
                ImGui::BulletText("%s v%u..%u", snapshot.passes[access.pass].name.c_str(),
                                  unsigned(access.firstVersion), unsigned(access.lastVersion));
            }
        }
    }

    void DrawGraphMatrix(const GraphSnapshot& snapshot, const GraphViewModel& model,
                         const ce::dynamic_array<uint32_t>& columns, const ce::dynamic_array<uint32_t>& resources,
                         uint32_t& selectedPass, uint32_t& selectedResource, float height)
    {
        ImGui::TextUnformatted("R read | W write | RW modify/atomic | faint band: resource lifetime");
        const float scale = ImGui::GetFontSize() / 13.f;
        const float columnWidth = 112.f * scale, rowHeight = 28.f * scale;
        const float width = (std::max)(columnWidth, columnWidth * columns.size());
        if (!ImGui::BeginTable("PassResourceMatrix", 2,
                               ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersInner |
                                   ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit,
                               ImVec2(0, height), 220.f * scale + width))
        {
            return;
        }
        ImGui::TableSetupColumn("Resource", ImGuiTableColumnFlags_WidthFixed, 220.f * scale);
        ImGui::TableSetupColumn("Compiled pass order", ImGuiTableColumnFlags_WidthFixed, width);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableNextRow(0, 78.f * scale);
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted("Resource / compiled pass");
        ImGui::TableSetColumnIndex(1);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 header = ImGui::GetCursorScreenPos();
        for (size_t column = 0; column < columns.size(); ++column)
        {
            const auto passIndex = columns[column];
            const auto& pass = snapshot.passes[passIndex];
            const ImVec2 a(header.x + column * columnWidth, header.y), b(a.x + columnWidth - 2, a.y + 74.f * scale);
            if (!ImGui::IsRectVisible(a, b))
            {
                continue;
            }
            draw->AddRectFilled(a, b,
                                passIndex == selectedPass ? IM_COL32(55, 79, 103, 255) : IM_COL32(43, 46, 51, 255));
            draw->PushClipRect(a, b, true);
            draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 5, a.y + 4),
                          pass.culled ? IM_COL32(140, 140, 140, 255) : IM_COL32_WHITE, pass.name.c_str(), nullptr,
                          columnWidth - 10);
            draw->PopClipRect();
            if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
            {
                ImGui::SetTooltip("%s\nAuthored #%u / compiled %d%s", pass.name.c_str(), pass.authoredIndex,
                                  pass.compiledIndex, pass.culled ? " / culled" : "");
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    selectedPass = passIndex;
                }
            }
        }
        ImGui::Dummy(ImVec2(width, 74.f * scale));
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(resources.size()), rowHeight);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
            {
                const uint32_t resourceIndex = resources[row];
                const auto& resource = snapshot.resources[resourceIndex];
                ImGui::PushID(static_cast<int>(resourceIndex));
                ImGui::TableNextRow(0, rowHeight);
                ImGui::TableSetColumnIndex(0);
                if (ImGui::Selectable(resource.name.c_str(), selectedResource == resourceIndex, 0,
                                      ImVec2(0, rowHeight - 5)))
                {
                    selectedResource = resourceIndex;
                }
                ImGui::TableSetColumnIndex(1);
                const ImVec2 origin = ImGui::GetCursorScreenPos();
                for (size_t column = 0; column < columns.size(); ++column)
                {
                    const uint32_t passIndex = columns[column];
                    const auto& pass = snapshot.passes[passIndex];
                    const ImVec2 a(origin.x + column * columnWidth, origin.y),
                        b(a.x + columnWidth - 2, a.y + rowHeight - 5);
                    if (!ImGui::IsRectVisible(a, b))
                    {
                        continue;
                    }
                    if (resource.used && pass.compiledIndex >= 0 && uint32_t(pass.compiledIndex) >= resource.firstUse &&
                        uint32_t(pass.compiledIndex) <= resource.lastUse)
                    {
                        draw->AddRectFilled(a, b, IM_COL32(99, 130, 154, 38));
                    }
                    const auto* cell = model.Find(resourceIndex, passIndex);
                    if (cell)
                    {
                        ImU32 color = cell->access == 1 ? kGraphRead : cell->access == 2 ? kGraphWrite : kGraphModify;
                        draw->AddRectFilled(a, b, pass.culled ? IM_COL32(91, 91, 91, 180) : color, 2.f);
                        draw->AddText(ImVec2(a.x + 5, a.y + 2), IM_COL32(15, 18, 21, 255),
                                      cell->access == 1   ? "R"
                                      : cell->access == 2 ? "W"
                                                          : "RW");
                        if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
                        {
                            ImGui::SetTooltip("%s\n%s r%u | versions %u..%u\nIncludes repeated phase accesses",
                                              pass.name.c_str(), resource.name.c_str(), resourceIndex,
                                              unsigned(cell->firstVersion), unsigned(cell->lastVersion));
                            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                            {
                                selectedPass = passIndex;
                                selectedResource = resourceIndex;
                            }
                        }
                    }
                    if (passIndex == selectedPass || resourceIndex == selectedResource)
                    {
                        draw->AddRect(a, b, IM_COL32(219, 226, 235, 200), 0.f, 0, 1.f);
                    }
                }
                ImGui::Dummy(ImVec2(width, rowHeight - 5));
                ImGui::PopID();
            }
        }
        ImGui::EndTable();
    }

    void DrawGraphArrow(ImDrawList& draw, ImVec2 a, ImVec2 b, ImU32 color)
    {
        const float mid = (a.x + b.x) * .5f;
        draw.AddBezierCubic(a, ImVec2(mid, a.y), ImVec2(mid, b.y), b, color, 1.5f);
        draw.AddTriangleFilled(b, ImVec2(b.x - 7, b.y - 4), ImVec2(b.x - 7, b.y + 4), color);
    }

    void DrawGraphDag(const GraphSnapshot& snapshot, GraphViewModel& model, uint32_t& selectedPass,
                      uint32_t& selectedResource)
    {
        if (selectedPass >= snapshot.passes.size())
        {
            ImGui::TextUnformatted("Select a pass in Matrix first.");
            return;
        }
        model.Focus(selectedPass);
        ImGui::TextUnformatted(
            "Direct predecessors -> selected pass -> direct consumers (dependency order, not GPU time)");
        const float scale = ImGui::GetFontSize() / 13.f;
        const float nodeWidth = 180.f * scale, strideX = 250.f * scale, strideY = 68.f * scale;
        const size_t rows = (std::max)(size_t(1), (std::max)(model.parents.size(), model.children.size()));
        const float canvasHeight =
            (std::max)(100.f * scale, (std::min)(240.f * scale, ImGui::GetContentRegionAvail().y * .6f));
        if (ImGui::BeginChild("SelectedPassDag", ImVec2(0, canvasHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            auto* storage = ImGui::GetStateStorage();
            const auto selectionKey = ImGui::GetID("DagPassSelection");
            if (storage->GetInt(selectionKey, -1) != static_cast<int>(selectedPass))
            {
                storage->SetInt(selectionKey, static_cast<int>(selectedPass));
                ImGui::SetScrollY(0.f);
            }
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            auto* draw = ImGui::GetWindowDrawList();
            const auto position = [&](int lane, size_t row)
            { return ImVec2(origin.x + lane * strideX + 8, origin.y + row * strideY + 8); };
            auto selectedPosition = position(1, rows / 2);
            selectedPosition.y =
                (std::min)(selectedPosition.y, origin.y + (std::max)(8.f, (canvasHeight - 44.f * scale) * .5f));
            for (const auto& edge : model.edges)
            {
                const bool incoming = edge.consumer == selectedPass;
                const bool outgoing = edge.producer == selectedPass;
                if (!incoming && !outgoing)
                {
                    continue;
                }
                const auto& neighbors = incoming ? model.parents : model.children;
                const auto found =
                    std::find(neighbors.begin(), neighbors.end(), incoming ? edge.producer : edge.consumer);
                if (found == neighbors.end())
                {
                    continue;
                }
                const auto neighbor = position(incoming ? 0 : 2, size_t(found - neighbors.begin()));
                const ImVec2 a(incoming ? neighbor.x + nodeWidth : selectedPosition.x + nodeWidth,
                               (incoming ? neighbor.y : selectedPosition.y) + 22.f * scale);
                const ImVec2 b(incoming ? selectedPosition.x : neighbor.x,
                               (incoming ? selectedPosition.y : neighbor.y) + 22.f * scale);
                DrawGraphArrow(*draw, a, b, edge.resource == selectedResource ? kGraphWrite : kGraphRead);
            }
            uint32_t clicked = GraphViewModel::invalid;
            const auto node = [&](uint32_t passIndex, ImVec2 a)
            {
                const ImVec2 b(a.x + nodeWidth, a.y + 44.f * scale);
                draw->AddRectFilled(
                    a, b, passIndex == selectedPass ? IM_COL32(53, 81, 105, 255) : IM_COL32(47, 51, 57, 255), 3.f);
                draw->AddRect(a, b, kGraphRead, 3.f);
                draw->PushClipRect(a, b, true);
                draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x + 6, a.y + 5), IM_COL32_WHITE,
                              snapshot.passes[passIndex].name.c_str(), nullptr, nodeWidth - 12);
                draw->PopClipRect();
                if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
                {
                    ImGui::SetTooltip("%s\nClick to inspect this pass", snapshot.passes[passIndex].name.c_str());
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    {
                        clicked = passIndex;
                    }
                }
            };
            for (size_t i = 0; i < model.parents.size(); ++i)
            {
                node(model.parents[i], position(0, i));
            }
            node(selectedPass, selectedPosition);
            for (size_t i = 0; i < model.children.size(); ++i)
            {
                node(model.children[i], position(2, i));
            }
            ImGui::Dummy(ImVec2(3 * strideX, (rows + 1) * strideY));
            if (clicked != GraphViewModel::invalid)
            {
                selectedPass = clicked;
            }
        }
        ImGui::EndChild();
        ImGui::TextUnformatted("Edge reasons (select a resource to highlight its links)");
        ImGui::BeginChild("DagEdgeReasons", ImVec2(0, 0));
        for (size_t i = 0; i < model.edges.size(); ++i)
        {
            const auto& edge = model.edges[i];
            if (edge.producer != selectedPass && edge.consumer != selectedPass)
            {
                continue;
            }
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(GraphResourceName(snapshot, edge.resource), selectedResource == edge.resource))
            {
                selectedResource = edge.resource;
            }
            ImGui::TextWrapped("%s -> %s | v%u | %s", snapshot.passes[edge.producer].name.c_str(),
                               snapshot.passes[edge.consumer].name.c_str(), unsigned(edge.version),
                               edge.reason < 0
                                   ? "Reachability"
                                   : GraphEdgeReason(static_cast<GraphSnapshot::VersionEdge::Reason>(edge.reason)));
            ImGui::PopID();
        }
        ImGui::EndChild();
    }

    void DrawGraphQueues(const GraphSnapshot& snapshot, uint32_t& selectedPass)
    {
        const auto& queue = snapshot.queueExecution;
        const auto& execution = queue.execution;
        ImGui::Text("Requested mode %u | effective mode %u | fallback %s", queue.requestedExecutionMode,
                    queue.effectiveExecutionMode, RGQueueFallbackReasonName(queue.schedule.fallbackReason));
        ImGui::TextWrapped("Batch columns show submission order, not measured GPU duration. Submitted means accepted "
                           "by CPU submission; it is not GPU completion.");
        ImGui::Text("Batches accepted/planned %u/%u | compute %u/%u | waits %u/%u", execution.submittedBatches,
                    execution.plannedBatches, execution.computeBatches, execution.plannedComputeBatches,
                    execution.submittedWaits, execution.plannedWaits);
        ImGui::Text("CPU plan %.3f ms | record %.3f ms | submit %.3f ms", execution.scheduleMilliseconds,
                    execution.recordingMilliseconds, execution.submissionMilliseconds);
        if (execution.recoveryRequired)
        {
            ImGui::TextColored(kWarnColor,
                               "Partial submission: recovery required; do not interpret as successful execution.");
        }
        const float scale = ImGui::GetFontSize() / 13.f;
        const float canvasHeight =
            (std::max)(155.f * scale, (std::min)(235.f * scale, ImGui::GetContentRegionAvail().y -
                                                                    2.f * ImGui::GetTextLineHeightWithSpacing()));
        if (ImGui::BeginChild("QueueBatchLanes", ImVec2(0, canvasHeight), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            auto* draw = ImGui::GetWindowDrawList();
            draw->AddText(origin, IM_COL32_WHITE, "Graphics");
            draw->AddText(ImVec2(origin.x, origin.y + 90.f * scale), IM_COL32_WHITE, "Compute");
            const auto point = [&](size_t index)
            {
                return ImVec2(origin.x + (85.f + index * 135.f) * scale,
                              origin.y + (queue.batches[index].queue == RHIQueueKind::Compute ? 105.f : 15.f) * scale);
            };
            for (const auto& wait : queue.waits)
            {
                if (wait.producerBatch < queue.batches.size() && wait.consumerBatch < queue.batches.size())
                {
                    const auto a = point(wait.producerBatch), b = point(wait.consumerBatch);
                    DrawGraphArrow(*draw, ImVec2(a.x + 120.f * scale, a.y + 20.f * scale),
                                   ImVec2(b.x, b.y + 20.f * scale),
                                   wait.submitted ? kGraphWrite : IM_COL32(135, 135, 135, 200));
                }
            }
            for (size_t i = 0; i < queue.batches.size(); ++i)
            {
                const auto& batch = queue.batches[i];
                const auto a = point(i);
                const ImVec2 b(a.x + 120.f * scale, a.y + 58.f * scale);
                const bool selected =
                    std::find(batch.passes.begin(), batch.passes.end(), selectedPass) != batch.passes.end();
                draw->AddRectFilled(a, b, selected ? IM_COL32(74, 95, 114, 255) : IM_COL32(44, 49, 55, 255), 3.f);
                draw->AddRect(a, b, batch.submitted ? kGraphModify : kGraphRead, 3.f);
                char label[96];
                std::snprintf(label, sizeof(label), "Batch %zu | %s\n%zu passes | %u barriers", i,
                              batch.submitted ? "accepted" : "planned", batch.passes.size(), batch.barriers);
                draw->AddText(ImVec2(a.x + 4, a.y + 5), IM_COL32_WHITE, label);
                if (ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b))
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Batch %zu%s", i, batch.passes.empty() ? " (prologue / epilogue)" : "");
                    for (const auto pass : batch.passes)
                    {
                        if (pass < snapshot.passes.size())
                        {
                            ImGui::TextUnformatted(snapshot.passes[pass].name.c_str());
                        }
                    }
                    ImGui::EndTooltip();
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !batch.passes.empty())
                    {
                        selectedPass = batch.passes.front();
                    }
                }
            }
            if (queue.batches.empty())
            {
                ImGui::SetCursorScreenPos(ImVec2(origin.x + 90.f * scale, origin.y + 20.f * scale));
                ImGui::TextWrapped("No multi-queue batch diagnostics in this snapshot. The compiled pass order is "
                                   "available in Matrix; no GPU overlap is inferred.");
            }
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2((100.f + queue.batches.size() * 135.f) * scale, 190.f * scale));
        }
        ImGui::EndChild();
        if (ImGui::TreeNode("Cross-queue waits"))
        {
            for (const auto& wait : queue.waits)
            {
                ImGui::Text("Batch %u -> %u | %s", wait.producerBatch, wait.consumerBatch,
                            wait.submitted ? "accepted" : "planned");
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Planned pass queue assignments"))
        {
            for (const auto& entry : queue.schedule.entries)
            {
                if (entry.pass < snapshot.passes.size())
                {
                    ImGui::Text("%s | %s", entry.queue == RHIQueueKind::Compute ? "Compute" : "Graphics",
                                snapshot.passes[entry.pass].name.c_str());
                }
            }
            ImGui::TreePop();
        }
    }
} // namespace EnhancedRenderDebugUi
