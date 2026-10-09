#include "SoundGraphEditor.h"
#include "EditorAssetDatabase.h"
#include "EditorAssetDragPayload.h"
#include "EditorWindowRegistry.h"
#include "BrowserDirectorySnapshot.h"
#include "DataSystem.h"
#include "Audio/SoundAssetSerialization.h"
#include "Audio/PlaybackService.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iterator>
#include <mutex>
#include <optional>

namespace editor::sound_graph_editing
{
    namespace
    {
        constexpr const char* kWindowID = "###Editor.SoundGraph";
        constexpr const char* kNodeNames[] = { "Clip", "Random", "Switch", "Layer", "GainPitch", "Parameter", "Output" };
        constexpr const char* kParameterNames[] = { "Boolean", "Integer", "Float", "String" };
        constexpr const char* kSourceNames[] = { "Clip", "Graph" };
        constexpr std::size_t kMaximumSourceBytes = 4u * 1024u * 1024u;

        struct Session final
        {
            std::filesystem::path m_path;
            FileGuid m_guid;
            wave::SoundGraphDefinition m_graph;
            wave::SoundPreset m_preset;
            wave::ParameterMap m_previewParameters;
            bool m_isPreset{};
            bool m_dirty{};
            bool m_sourceDirty{};
            std::string m_message;
            std::string m_yaml;
            std::string m_diskPayload;
            std::string m_newParameter;
            int m_newNodeKind{};
            std::uint32_t m_selectedNode{};
            bool m_selectedOnly{};
        };

        enum class PreviewAction { Play, Stop };
        struct PreviewRequest final
        {
            PreviewAction m_action{ PreviewAction::Stop };
            bool m_isPreset{};
            wave::SoundGraphDefinition m_graph;
            wave::SoundPreset m_preset;
            wave::ParameterMap m_parameters;
            wave::ClipKey m_temporaryID;
        };

        // Only the mailbox is shared. Never publish service pointers or live
        // playback handles to the UI, and never put runtime state on assets.
        struct PreviewMailbox final
        {
            std::mutex m_mutex;
            std::optional<PreviewRequest> m_request;
            std::chrono::steady_clock::time_point m_lastVisible;
            std::string m_status{ "Preview stopped" };
            bool m_accepting{ true };
        };

        struct PreviewRuntime final
        {
            wave::PlaybackService* m_service{};
            wave::PlaybackScope m_scope;
            wave::PlaybackHandle m_playback;
            wave::ClipKey m_temporaryID;
        };

        std::optional<Session> g_session;
        std::optional<Session> g_pendingSession;
        PreviewMailbox g_previewMailbox;
        PreviewRuntime g_previewRuntime;
        struct AuthoringAcceptanceRequest final
        {
            std::filesystem::path directory;
            std::string name;
            std::string clipGuid;
        };
        std::mutex g_acceptanceMutex;
        std::optional<AuthoringAcceptanceRequest> g_acceptanceRequest;
        std::string g_acceptanceStatus{ "Idle" };

        std::string PathText(const std::filesystem::path& path)
        {
            const auto utf8 = path.u8string();
            return { reinterpret_cast<const char*>(utf8.data()), utf8.size() };
        }

        std::string Extension(const std::filesystem::path& path)
        {
            auto extension = path.extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return extension;
        }

        bool ReadText(const std::filesystem::path& path, std::string& text, std::string& error)
        {
            std::error_code ec;
            const auto size = std::filesystem::file_size(path, ec);
            if (ec || size > kMaximumSourceBytes)
            {
                error = ec ? ec.message() : "Sound assets must be smaller than 4 MiB.";
                return false;
            }
            std::ifstream input(path, std::ios::binary);
            if (!input)
            {
                error = "Could not open the sound asset.";
                return false;
            }
            text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
            if (input.bad())
            {
                error = "Could not read the complete sound asset.";
                return false;
            }
            return true;
        }

        std::string Serialize(const Session& session)
        {
            return session.m_isPreset ? wave::WriteSoundPreset(session.m_preset) : wave::WriteSoundGraph(session.m_graph);
        }

        wave::ParameterValue DefaultValue(wave::ParameterType type)
        {
            switch (type)
            {
            case wave::ParameterType::Boolean: return false;
            case wave::ParameterType::Integer: return std::int32_t{};
            case wave::ParameterType::Float: return 0.0f;
            case wave::ParameterType::String: return std::string{};
            }
            return 0.0f;
        }

        bool DrawValue(const char* label, wave::ParameterValue& value)
        {
            switch (wave::TypeOfParameter(value))
            {
            case wave::ParameterType::Boolean: return ImGui::Checkbox(label, &std::get<bool>(value));
            case wave::ParameterType::Integer:
                return ImGui::InputScalar(label, ImGuiDataType_S32, &std::get<std::int32_t>(value));
            case wave::ParameterType::Float: return ImGui::InputFloat(label, &std::get<float>(value));
            case wave::ParameterType::String: return ImGui::InputText(label, &std::get<std::string>(value));
            }
            return false;
        }

        bool DrawTypedValue(wave::ParameterValue& value)
        {
            bool changed = false;
            int type = static_cast<int>(wave::TypeOfParameter(value));
            if (ImGui::Combo("Type", &type, kParameterNames, 4))
            {
                value = DefaultValue(static_cast<wave::ParameterType>(type));
                changed = true;
            }
            return DrawValue("Value", value) || changed;
        }

        bool IsAssetReference(const wave::ClipKey& key, wave::SoundSourceKind kind)
        {
            Uuid::Uuid16 guid;
            if (!key.IsGuid() || !Uuid::TryParse(key.Text(), guid) || !FileGuid(guid).IsRandomV4())
            {
                return false;
            }
            const auto path = DataSystems->GetFilePath(FileGuid(guid));
            const auto extension = Extension(path);
            std::error_code error;
            if (path.empty() || !std::filesystem::is_regular_file(path, error) || error)
            {
                return false;
            }
            if (kind == wave::SoundSourceKind::Graph)
            {
                return extension == ".soundgraph";
            }
            if (kind == wave::SoundSourceKind::Preset)
            {
                return extension == ".soundpreset";
            }
            return extension == ".wav" || extension == ".mp3" || extension == ".flac";
        }

        bool DrawAssetReference(const char* label, wave::ClipKey& key, wave::SoundSourceKind kind)
        {
            bool changed = false;
            auto text = key.Text();
            if (ImGui::InputText(label, &text))
            {
                Uuid::Uuid16 guid;
                key = Uuid::TryParse(text, guid) ? wave::ClipKey::FromGuid(guid) : wave::ClipKey(text);
                changed = true;
            }
            if (ImGui::BeginDragDropTarget())
            {
                const auto* payload = ImGui::GetDragDropPayload();
                if (payload && editor::asset_drag::carries_path(*payload))
                {
                    if (const auto* accepted = ImGui::AcceptDragDropPayload(payload->DataType))
                    {
                        const auto path = editor::asset_drag::path_of(*accepted);
                        const auto guid = DataSystems->GetFileGuid(path);
                        const auto candidate = wave::ClipKey::FromGuid(guid.m_guid);
                        if (IsAssetReference(candidate, kind))
                        {
                            key = candidate;
                            changed = true;
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Paste an asset GUID, or drop the matching asset from Content Browser.");
            }
            return changed;
        }

        bool ParameterPicker(const char* label, std::string& name,
            const wave::SoundGraphDefinition& graph, bool numericOnly = false)
        {
            bool changed = false;
            if (ImGui::BeginCombo(label, name.empty() ? "(none)" : name.c_str()))
            {
                if (ImGui::Selectable("(none)", name.empty()))
                {
                    name.clear();
                    changed = true;
                }
                for (const auto& parameter : graph.parameters)
                {
                    if (numericOnly && parameter.type != wave::ParameterType::Float
                        && parameter.type != wave::ParameterType::Integer)
                    {
                        continue;
                    }
                    if (ImGui::Selectable(parameter.name.c_str(), parameter.name == name))
                    {
                        name = parameter.name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        bool NodePicker(const char* label, std::uint32_t& id, const wave::SoundGraphDefinition& graph,
            std::uint32_t excluded = 0u, bool outputOnly = false)
        {
            bool changed = false;
            const auto selected = std::to_string(id);
            if (ImGui::BeginCombo(label, selected.c_str()))
            {
                for (const auto& node : graph.nodes)
                {
                    if (node.id == excluded || node.kind == wave::SoundNodeKind::Parameter
                        || (outputOnly != (node.kind == wave::SoundNodeKind::Output)))
                    {
                        continue;
                    }
                    const auto name = std::to_string(node.id) + " : " + kNodeNames[static_cast<int>(node.kind)];
                    if (ImGui::Selectable(name.c_str(), node.id == id))
                    {
                        id = node.id;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            return changed;
        }

        void QueueStop()
        {
            std::lock_guard lock(g_previewMailbox.m_mutex);
            if (g_previewMailbox.m_accepting)
            {
                g_previewMailbox.m_request = PreviewRequest{};
            }
        }

        void QueuePreview(const Session& session)
        {
            PreviewRequest request;
            request.m_action = PreviewAction::Play;
            request.m_isPreset = session.m_isPreset;
            request.m_graph = session.m_graph;
            request.m_preset = session.m_preset;
            request.m_parameters = session.m_previewParameters;
            // Temporary registration must never replace the catalog asset's
            // program while scene playbacks are using it.
            request.m_temporaryID = wave::ClipKey::FromGuid(FileGuid::CreateRandomV4().m_guid);
            std::lock_guard lock(g_previewMailbox.m_mutex);
            if (g_previewMailbox.m_accepting)
            {
                g_previewMailbox.m_request = std::move(request);
                g_previewMailbox.m_status = "Preview queued";
            }
        }

        void StopRuntime(wave::PlaybackService* playback)
        {
            if (playback && playback == g_previewRuntime.m_service)
            {
                if (g_previewRuntime.m_scope.IsValid())
                {
                    playback->EndScope(g_previewRuntime.m_scope);
                }
                if (!g_previewRuntime.m_temporaryID.IsEmpty())
                {
                    playback->UnregisterAsset(g_previewRuntime.m_temporaryID);
                }
            }
            g_previewRuntime = {};
        }

        bool Validate(Session& session)
        {
            std::string error;
            if (session.m_sourceDirty)
            {
                session.m_message = "Apply or reset the edited YAML before validating or previewing.";
                return false;
            }
            if (!session.m_isPreset)
            {
                wave::SoundGraphDefinition checked;
                if (!wave::ReadSoundGraph(wave::WriteSoundGraph(session.m_graph), checked, error))
                {
                    session.m_message = error;
                    return false;
                }
                const auto program = wave::CompileSoundGraph(session.m_graph, [](const wave::ClipKey& key)
                {
                    return IsAssetReference(key, wave::SoundSourceKind::Clip);
                }, error);
                if (!program)
                {
                    session.m_message = error;
                    return false;
                }
            }
            else
            {
                wave::SoundPreset checked;
                if (!wave::ReadSoundPreset(wave::WriteSoundPreset(session.m_preset), checked, error))
                {
                    session.m_message = error;
                    return false;
                }
                if (!IsAssetReference(session.m_preset.source.asset, session.m_preset.source.kind))
                {
                    session.m_message = "Source GUID does not resolve to an asset of the selected type.";
                    return false;
                }
                if (session.m_preset.source.kind == wave::SoundSourceKind::Graph)
                {
                    Uuid::Uuid16 sourceID;
                    if (!Uuid::TryParse(session.m_preset.source.asset.Text(), sourceID))
                    {
                        session.m_message = "Invalid graph GUID.";
                        return false;
                    }
                    const auto sourcePath = DataSystems->GetFilePath(FileGuid(sourceID));
                    std::string payload;
                    wave::SoundGraphDefinition graph;
                    if (!ReadText(sourcePath, payload, error) || !wave::ReadSoundGraph(payload, graph, error))
                    {
                        session.m_message = error;
                        return false;
                    }
                    const auto program = wave::CompileSoundGraph(graph, [](const wave::ClipKey& key)
                    {
                        return IsAssetReference(key, wave::SoundSourceKind::Clip);
                    }, error);
                    wave::ParameterMap resolved;
                    if (!program || !wave::ResolveGraphParameters(*program, session.m_preset.parameters, resolved, error))
                    {
                        session.m_message = error;
                        return false;
                    }
                }
                else if (!session.m_preset.parameters.empty())
                {
                    session.m_message = "Only graph presets can declare parameter overrides.";
                    return false;
                }
            }
            session.m_message = "Valid authoring data. Preview also checks loaded runtime dependencies.";
            return true;
        }

        bool Save(Session& session)
        {
            if (session.m_sourceDirty)
            {
                session.m_message = "Apply or reset the edited YAML before saving.";
                return false;
            }
            if (DataSystems->GetFileGuid(session.m_path) != session.m_guid)
            {
                session.m_message = "The asset identity changed. Reopen it before saving.";
                return false;
            }
            if (!Validate(session))
            {
                return false;
            }
            std::string disk;
            if (!ReadText(session.m_path, disk, session.m_message))
            {
                return false;
            }
            if (disk != session.m_diskPayload)
            {
                session.m_message = "This asset changed on disk. Reopen it before saving to avoid overwriting external edits.";
                return false;
            }
            const auto payload = Serialize(session);
            if (EditorAssetDatabase::Get().WriteTextAssetWithMeta(session.m_path, payload, session.m_guid) != session.m_guid)
            {
                session.m_message = "Could not save the asset and its GUID metadata.";
                return false;
            }
            session.m_diskPayload = payload;
            session.m_dirty = false;
            session.m_message = "Saved";
            editor::browser_cache_invalidate();
            return true;
        }

        void DrawParameters(Session& session)
        {
            auto& parameters = session.m_graph.parameters;
            if (!ImGui::CollapsingHeader("Typed parameters", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            for (std::size_t index = 0; index < parameters.size();)
            {
                ImGui::PushID(static_cast<int>(index));
                auto& parameter = parameters[index];
                const auto oldName = parameter.name;
                bool changed = ImGui::InputText("Name", &parameter.name);
                if (changed)
                {
                    for (auto& node : session.m_graph.nodes)
                    {
                        if (node.parameter == oldName) { node.parameter = parameter.name; }
                        if (node.gainParameter == oldName) { node.gainParameter = parameter.name; }
                        if (node.pitchParameter == oldName) { node.pitchParameter = parameter.name; }
                    }
                    session.m_previewParameters.erase(oldName);
                }
                changed |= DrawTypedValue(parameter.defaultValue);
                parameter.type = wave::TypeOfParameter(parameter.defaultValue);
                session.m_dirty |= changed;
                const bool remove = ImGui::SmallButton("Remove parameter");
                ImGui::Separator();
                ImGui::PopID();
                if (remove)
                {
                    session.m_previewParameters.erase(parameter.name);
                    parameters.erase(parameters.begin() + index);
                    session.m_dirty = true;
                }
                else
                {
                    ++index;
                }
            }
            if (ImGui::Button("Add parameter"))
            {
                parameters.push_back({ "Parameter" + std::to_string(parameters.size() + 1u),
                    wave::ParameterType::Float, 1.0f });
                session.m_dirty = true;
            }
        }

        void DrawInputs(Session& session, wave::SoundNode& node)
        {
            for (std::size_t index = 0; index < node.inputs.size();)
            {
                ImGui::PushID(static_cast<int>(index));
                session.m_dirty |= NodePicker("Input node", node.inputs[index], session.m_graph, node.id);
                if (node.kind == wave::SoundNodeKind::Random && !node.weights.empty())
                {
                    node.weights.resize(node.inputs.size(), 1.0f);
                    session.m_dirty |= ImGui::DragFloat("Weight", &node.weights[index], 0.05f, 0.0f, 1000.0f);
                }
                if (node.kind == wave::SoundNodeKind::Switch)
                {
                    if (index < node.cases.size())
                    {
                        const auto parameter = std::find_if(session.m_graph.parameters.begin(), session.m_graph.parameters.end(),
                            [&node](const wave::ParameterDefinition& value) { return value.name == node.parameter; });
                        if (parameter != session.m_graph.parameters.end()
                            && wave::TypeOfParameter(node.cases[index]) != parameter->type)
                        {
                            node.cases[index] = DefaultValue(parameter->type);
                            session.m_dirty = true;
                        }
                        session.m_dirty |= DrawValue("Case value", node.cases[index]);
                    }
                    else
                    {
                        ImGui::TextDisabled("Default branch");
                    }
                }
                const bool remove = ImGui::SmallButton("Remove input");
                ImGui::PopID();
                if (remove)
                {
                    node.inputs.erase(node.inputs.begin() + index);
                    if (index < node.weights.size()) { node.weights.erase(node.weights.begin() + index); }
                    if (index < node.cases.size()) { node.cases.erase(node.cases.begin() + index); }
                    session.m_dirty = true;
                }
                else
                {
                    ++index;
                }
            }
            const bool singleInput = node.kind == wave::SoundNodeKind::GainPitch || node.kind == wave::SoundNodeKind::Output;
            ImGui::BeginDisabled(singleInput && !node.inputs.empty());
            if (ImGui::SmallButton("Add input"))
            {
                const bool preserveDefault = node.kind == wave::SoundNodeKind::Switch
                    && node.inputs.size() == node.cases.size() + 1u;
                if (preserveDefault)
                {
                    node.inputs.insert(node.inputs.end() - 1, 0u);
                }
                else
                {
                    node.inputs.push_back(0u);
                }
                if (!node.weights.empty()) { node.weights.push_back(1.0f); }
                if (node.kind == wave::SoundNodeKind::Switch)
                {
                    node.cases.push_back(0.0f);
                }
                session.m_dirty = true;
            }
            ImGui::EndDisabled();
        }

        void DrawTopology(Session& session)
        {
            if (!ImGui::CollapsingHeader("Graph overview", ImGuiTreeNodeFlags_DefaultOpen))
            {
                return;
            }
            // The small overview has a fixed drawing budget. Full graph data
            // remains editable below; large assets never stall the UI here.
            const auto& nodes = session.m_graph.nodes;
            const std::size_t count = std::min<std::size_t>(nodes.size(), 64u);
            std::vector<unsigned int> depths(count, 0u);
            for (std::size_t pass = 0; pass < std::min<std::size_t>(count, 16u); ++pass)
            {
                auto next = depths;
                for (std::size_t index = 0; index < count; ++index)
                {
                    for (const auto input : nodes[index].inputs)
                    {
                        for (std::size_t source = 0; source < count; ++source)
                        {
                            if (nodes[source].id == input)
                            {
                                next[index] = std::max(next[index], std::min(depths[source] + 1u, 16u));
                            }
                        }
                    }
                }
                if (next == depths)
                {
                    break;
                }
                depths = std::move(next);
            }
            ImGui::TextDisabled("Audio flows left to right. Click a node to edit it; blue lines bind typed parameters.");
            ImGui::BeginChild("Topology", ImVec2(0.0f, 270.0f), ImGuiChildFlags_Borders,
                ImGuiWindowFlags_HorizontalScrollbar);
            const auto origin = ImGui::GetCursorScreenPos();
            const float scale = ImGui::GetFontSize() / 13.0f;
            const float width = 164.0f * scale;
            const float height = 58.0f * scale;
            std::array<unsigned int, 17> rows{};
            std::vector<ImVec2> positions;
            float maximumX = 0.0f;
            float maximumY = 0.0f;
            for (std::size_t index = 0; index < count; ++index)
            {
                const float x = 12.0f + depths[index] * 208.0f * scale;
                const float y = 12.0f + rows[depths[index]]++ * 86.0f * scale;
                positions.emplace_back(origin.x + x, origin.y + y);
                maximumX = std::max(maximumX, x + width + 12.0f);
                maximumY = std::max(maximumY, y + height + 12.0f);
            }
            auto* draw = ImGui::GetWindowDrawList();
            const auto edge = [&](std::size_t source, std::size_t target, ImU32 color)
            {
                const ImVec2 start(positions[source].x + width, positions[source].y + height * 0.5f);
                const ImVec2 end(positions[target].x, positions[target].y + height * 0.5f);
                const float bend = std::max(24.0f * scale, std::abs(end.x - start.x) * 0.5f);
                draw->AddBezierCubic(start, ImVec2(start.x + bend, start.y),
                    ImVec2(end.x - bend, end.y), end, color, 2.0f);
                draw->AddTriangleFilled(end, ImVec2(end.x - 7.0f, end.y - 4.0f),
                    ImVec2(end.x - 7.0f, end.y + 4.0f), color);
            };
            for (std::size_t target = 0; target < count; ++target)
            {
                for (std::size_t source = 0; source < count; ++source)
                {
                    if (std::find(nodes[target].inputs.begin(), nodes[target].inputs.end(), nodes[source].id)
                        != nodes[target].inputs.end())
                    {
                        edge(source, target, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    }
                    if (nodes[source].kind == wave::SoundNodeKind::Parameter && source != target
                        && !nodes[source].parameter.empty()
                        && (nodes[target].parameter == nodes[source].parameter
                            || nodes[target].gainParameter == nodes[source].parameter
                            || nodes[target].pitchParameter == nodes[source].parameter))
                    {
                        edge(source, target, IM_COL32(80, 160, 255, 255));
                    }
                }
            }
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto& node = nodes[index];
                const auto minimum = positions[index];
                const ImVec2 maximum(minimum.x + width, minimum.y + height);
                draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGuiCol_FrameBg), 5.0f);
                draw->AddRect(minimum, maximum, ImGui::GetColorU32(node.id == session.m_selectedNode
                    ? ImGuiCol_ButtonActive : ImGuiCol_Border), 5.0f);
                const auto title = std::to_string(node.id) + "  " + kNodeNames[static_cast<int>(node.kind)];
                draw->PushClipRect(minimum, maximum, true);
                draw->AddText(ImVec2(minimum.x + 8.0f, minimum.y + 7.0f),
                    ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
                const std::string detail = node.kind == wave::SoundNodeKind::Clip
                    ? (node.clip.IsEmpty() ? "Choose clip GUID" : node.clip.Text().substr(0u, 16u))
                    : node.kind == wave::SoundNodeKind::Parameter || node.kind == wave::SoundNodeKind::Switch
                        ? node.parameter : std::to_string(node.inputs.size()) + " audio input(s)";
                draw->AddText(ImVec2(minimum.x + 8.0f, minimum.y + 29.0f * scale),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), detail.c_str());
                draw->PopClipRect();
                ImGui::SetCursorScreenPos(minimum);
                ImGui::PushID(static_cast<int>(index));
                if (ImGui::InvisibleButton("Node", ImVec2(width, height)))
                {
                    session.m_selectedNode = node.id;
                    session.m_selectedOnly = true;
                }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(maximumX, maximumY));
            ImGui::EndChild();
            if (count != nodes.size())
            {
                ImGui::TextWrapped("Overview shows the first 64 nodes. All %zu nodes are available in the property list.",
                    nodes.size());
            }
        }

        void DrawGraph(Session& session)
        {
            DrawTopology(session);
            session.m_dirty |= ImGui::InputScalar("Maximum voices", ImGuiDataType_U32, &session.m_graph.maximumVoices);
            session.m_dirty |= NodePicker("Output", session.m_graph.output, session.m_graph, 0u, true);
            DrawParameters(session);
            ImGui::SeparatorText("Nodes and connections");
            ImGui::Checkbox("Show selected node only", &session.m_selectedOnly);
            ImGui::Combo("New node type", &session.m_newNodeKind, kNodeNames, 7);
            if (ImGui::Button("Add node"))
            {
                std::uint32_t id = 1u;
                while (std::any_of(session.m_graph.nodes.begin(), session.m_graph.nodes.end(),
                    [id](const wave::SoundNode& node) { return node.id == id; }))
                {
                    ++id;
                }
                wave::SoundNode node;
                node.id = id;
                node.kind = static_cast<wave::SoundNodeKind>(session.m_newNodeKind);
                session.m_graph.nodes.push_back(node);
                session.m_selectedNode = id;
                if (node.kind == wave::SoundNodeKind::Output)
                {
                    session.m_graph.output = id;
                }
                session.m_dirty = true;
            }
            for (std::size_t index = 0; index < session.m_graph.nodes.size();)
            {
                auto& node = session.m_graph.nodes[index];
                if (session.m_selectedOnly && node.id != session.m_selectedNode)
                {
                    ++index;
                    continue;
                }
                ImGui::PushID(static_cast<int>(index));
                const auto title = std::to_string(node.id) + " : " + kNodeNames[static_cast<int>(node.kind)];
                bool remove = false;
                if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                {
                    const auto oldID = node.id;
                    auto candidateID = oldID;
                    if (ImGui::InputScalar("ID", ImGuiDataType_U32, &candidateID)
                        && candidateID != 0u && std::none_of(session.m_graph.nodes.begin(), session.m_graph.nodes.end(),
                            [&](const wave::SoundNode& other) { return &other != &node && other.id == candidateID; }))
                    {
                        node.id = candidateID;
                        if (session.m_selectedNode == oldID)
                        {
                            session.m_selectedNode = node.id;
                        }
                        for (auto& other : session.m_graph.nodes)
                        {
                            for (auto& input : other.inputs)
                            {
                                if (input == oldID) { input = node.id; }
                            }
                        }
                        if (session.m_graph.output == oldID) { session.m_graph.output = node.id; }
                        session.m_dirty = true;
                    }
                    if (node.kind == wave::SoundNodeKind::Clip)
                    {
                        session.m_dirty |= DrawAssetReference("Clip GUID", node.clip, wave::SoundSourceKind::Clip);
                    }
                    if (node.kind == wave::SoundNodeKind::Switch || node.kind == wave::SoundNodeKind::Parameter)
                    {
                        session.m_dirty |= ParameterPicker("Parameter", node.parameter, session.m_graph);
                    }
                    if (node.kind == wave::SoundNodeKind::GainPitch)
                    {
                        session.m_dirty |= ImGui::DragFloat("Gain", &node.gain, 0.01f, 0.0f, 16.0f);
                        session.m_dirty |= ImGui::DragFloat("Pitch", &node.pitch, 0.01f, 0.01f, 4.0f);
                        session.m_dirty |= ParameterPicker("Gain multiplier", node.gainParameter, session.m_graph, true);
                        session.m_dirty |= ParameterPicker("Pitch multiplier", node.pitchParameter, session.m_graph, true);
                    }
                    if (node.kind == wave::SoundNodeKind::Random)
                    {
                        bool weighted = !node.weights.empty();
                        if (ImGui::Checkbox("Weighted", &weighted))
                        {
                            node.weights = weighted ? std::vector<float>(node.inputs.size(), 1.0f) : std::vector<float>{};
                            session.m_dirty = true;
                        }
                    }
                    if (node.kind == wave::SoundNodeKind::Switch)
                    {
                        bool hasDefault = node.inputs.size() == node.cases.size() + 1u;
                        if (ImGui::Checkbox("Last input is default", &hasDefault) && !node.inputs.empty())
                        {
                            node.cases.resize(node.inputs.size() - (hasDefault ? 1u : 0u), 0.0f);
                            session.m_dirty = true;
                        }
                    }
                    if (node.kind != wave::SoundNodeKind::Clip && node.kind != wave::SoundNodeKind::Parameter)
                    {
                        DrawInputs(session, node);
                    }
                    remove = ImGui::Button("Remove node");
                    ImGui::TreePop();
                }
                ImGui::PopID();
                if (remove)
                {
                    // Preserve broken references visibly; silently selecting a
                    // different branch would change the authored sound.
                    session.m_graph.nodes.erase(session.m_graph.nodes.begin() + index);
                    session.m_selectedOnly = false;
                    session.m_dirty = true;
                }
                else
                {
                    ++index;
                }
            }
        }

        void DrawPreset(Session& session)
        {
            int source = session.m_preset.source.kind == wave::SoundSourceKind::Graph ? 1 : 0;
            if (ImGui::Combo("Source type", &source, kSourceNames, 2))
            {
                session.m_preset.source.kind = source == 0 ? wave::SoundSourceKind::Clip : wave::SoundSourceKind::Graph;
                session.m_dirty = true;
            }
            session.m_dirty |= DrawAssetReference("Source GUID", session.m_preset.source.asset, session.m_preset.source.kind);
            auto& defaults = session.m_preset.defaults;
            ImGui::SeparatorText("Playback defaults");
            session.m_dirty |= ImGui::DragFloat("Volume", &defaults.volume, 0.01f, 0.0f, 16.0f);
            session.m_dirty |= ImGui::DragFloat("Pitch", &defaults.pitch, 0.01f, 0.01f, 4.0f);
            session.m_dirty |= ImGui::SliderInt("Priority", &defaults.priority, 0, 255);
            session.m_dirty |= ImGui::Checkbox("Loop", &defaults.loop);
            const char* buses[] = { "BGM", "SFX", "Player", "Monster", "UI", "Master", "Room" };
            int bus = static_cast<int>(defaults.bus.value) - 1;
            if (ImGui::Combo("Bus", &bus, buses, 7))
            {
                defaults.bus.value = static_cast<std::uint16_t>(bus + 1);
                session.m_dirty = true;
            }
            session.m_dirty |= ImGui::SliderFloat("Spatial blend", &defaults.spatialBlend, 0.0f, 1.0f);
            session.m_dirty |= ImGui::DragFloat("Minimum distance", &defaults.minimumDistance, 0.1f, 0.0f, 10000.0f);
            session.m_dirty |= ImGui::DragFloat("Maximum distance", &defaults.maximumDistance, 0.1f, 0.0f, 10000.0f);
            session.m_dirty |= ImGui::Checkbox("Virtualization", &defaults.allowVirtualization);
            session.m_dirty |= ImGui::Checkbox("Preempt same clip", &defaults.preemptSameClip);
            session.m_dirty |= ImGui::InputScalar("Concurrency group", ImGuiDataType_U32, &defaults.concurrencyGroup.value);
            session.m_dirty |= ImGui::Checkbox("Reverb send", &defaults.useReverbSend);
            session.m_dirty |= ImGui::DragFloat("Reverb send (dB)", &defaults.reverbSendDecibels, 0.1f, -80.0f, 10.0f);
            ImGui::TextDisabled("All serialized settings are also editable in the YAML tab.");
            ImGui::SeparatorText("Typed parameter overrides");
            std::vector<std::string> names;
            for (const auto& [name, value] : session.m_preset.parameters)
            {
                names.push_back(name);
            }
            std::sort(names.begin(), names.end());
            for (const auto& name : names)
            {
                ImGui::PushID(name.c_str());
                ImGui::TextUnformatted(name.c_str());
                session.m_dirty |= DrawTypedValue(session.m_preset.parameters.at(name));
                if (ImGui::SmallButton("Remove override"))
                {
                    session.m_preset.parameters.erase(name);
                    session.m_dirty = true;
                }
                ImGui::PopID();
                ImGui::Separator();
            }
            ImGui::InputText("New parameter name", &session.m_newParameter);
            if (ImGui::Button("Add override") && !session.m_newParameter.empty())
            {
                session.m_preset.parameters.try_emplace(session.m_newParameter, 0.0f);
                session.m_newParameter.clear();
                session.m_dirty = true;
            }
        }

        void DrawPreviewParameters(Session& session)
        {
            if (session.m_isPreset || !ImGui::CollapsingHeader("Preview parameter values"))
            {
                return;
            }
            for (const auto& parameter : session.m_graph.parameters)
            {
                auto [entry, inserted] = session.m_previewParameters.try_emplace(parameter.name, parameter.defaultValue);
                if (wave::TypeOfParameter(entry->second) != parameter.type)
                {
                    entry->second = parameter.defaultValue;
                }
                ImGui::PushID(parameter.name.c_str());
                DrawValue(parameter.name.c_str(), entry->second);
                ImGui::PopID();
            }
            ImGui::TextDisabled("Preview uses these values at the next Play; authored defaults stay unchanged.");
            if (ImGui::SmallButton("Reset preview values"))
            {
                session.m_previewParameters.clear();
            }
        }

        void DrawSource(Session& session)
        {
            if (session.m_yaml.empty())
            {
                session.m_yaml = Serialize(session);
            }
            if (ImGui::Button("Reset YAML from properties"))
            {
                session.m_yaml = Serialize(session);
                session.m_sourceDirty = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Apply YAML"))
            {
                wave::SoundGraphDefinition graph;
                wave::SoundPreset preset;
                std::string error;
                const bool valid = session.m_isPreset ? wave::ReadSoundPreset(session.m_yaml, preset, error)
                    : wave::ReadSoundGraph(session.m_yaml, graph, error);
                if (valid)
                {
                    session.m_graph = std::move(graph);
                    session.m_preset = std::move(preset);
                    session.m_previewParameters.clear();
                    session.m_dirty = true;
                    session.m_sourceDirty = false;
                    session.m_message = "YAML applied; validate before saving or previewing.";
                }
                else
                {
                    session.m_message = error;
                }
            }
            ImGui::TextWrapped("Apply replaces the property draft. Editing YAML alone does not change the saved asset.");
            session.m_sourceDirty |= ImGui::InputTextMultiline("##SoundSource", &session.m_yaml,
                ImVec2(-1.0f, 420.0f), ImGuiInputTextFlags_AllowTabInput);
        }
    }

    bool CanOpen(const std::filesystem::path& path)
    {
        const auto extension = Extension(path);
        return extension == ".soundgraph" || extension == ".soundpreset";
    }

    bool OpenAsset(const std::filesystem::path& path, std::string& error)
    {
        error.clear();
        if (!CanOpen(path))
        {
            error = "Choose a .soundgraph or .soundpreset asset.";
            return false;
        }
        Session session;
        session.m_path = path;
        session.m_isPreset = Extension(path) == ".soundpreset";
        session.m_guid = DataSystems->GetFileGuid(path);
        if (!session.m_guid.IsRandomV4())
        {
            error = "The sound asset needs valid GUID metadata. Refresh the Content Browser first.";
            return false;
        }
        if (!ReadText(path, session.m_diskPayload, error))
        {
            return false;
        }
        const bool parsed = session.m_isPreset
            ? wave::ReadSoundPreset(session.m_diskPayload, session.m_preset, error)
            : wave::ReadSoundGraph(session.m_diskPayload, session.m_graph, error);
        if (!parsed)
        {
            return false;
        }
        editor::open_window(kWindowID);
        editor::queue_window_request(kWindowID, editor::window_request::focus);
        if (g_session && (g_session->m_dirty || g_session->m_sourceDirty))
        {
            g_pendingSession = std::move(session);
        }
        else
        {
            QueueStop();
            g_session = std::move(session);
        }
        return true;
    }

    bool CreateAsset(const std::filesystem::path& directory, std::string_view name,
        bool preset, std::filesystem::path& created, std::string& error)
    {
        created.clear();
        error.clear();
        if (name.empty() || name == "." || name == ".." || name.back() == '.' || name.back() == ' '
            || name.find_first_of("<>:\"/\\|?*") != std::string_view::npos
            || std::any_of(name.begin(), name.end(), [](unsigned char value) { return value < 32u; }))
        {
            error = "Enter a valid asset name, without directory separators.";
            return false;
        }
        const auto extension = preset ? ".soundpreset" : ".soundgraph";
        auto filename = std::filesystem::path(std::u8string(
            reinterpret_cast<const char8_t*>(name.data()), name.size()));
        if (Extension(filename) != extension)
        {
            filename += extension;
        }
        const auto path = directory / filename;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) || ec)
        {
            error = ec ? ec.message() : "An asset with that name already exists.";
            return false;
        }
        Session session;
        session.m_path = path;
        session.m_guid = FileGuid::CreateRandomV4();
        session.m_isPreset = preset;
        if (!preset)
        {
            wave::SoundNode clip;
            clip.id = 1u;
            wave::SoundNode output;
            output.id = 2u;
            output.kind = wave::SoundNodeKind::Output;
            output.inputs = { 1u };
            session.m_graph.nodes = { clip, output };
            session.m_graph.output = 2u;
        }
        session.m_diskPayload = Serialize(session);
        if (EditorAssetDatabase::Get().WriteTextAssetWithMeta(path, session.m_diskPayload, session.m_guid) != session.m_guid)
        {
            error = "Could not create the sound asset and its GUID metadata inside Assets.";
            return false;
        }
        session.m_message = "Choose a source GUID, then validate and save.";
        editor::open_window(kWindowID);
        editor::queue_window_request(kWindowID, editor::window_request::focus);
        if (g_session && (g_session->m_dirty || g_session->m_sourceDirty))
        {
            g_pendingSession = std::move(session);
        }
        else
        {
            QueueStop();
            g_session = std::move(session);
        }
        created = path;
        editor::browser_cache_invalidate();
        return true;
    }

    bool QueueAuthoringAcceptance(const std::filesystem::path& directory,
        std::string_view name, std::string_view clipGuid, std::string& error)
    {
        Uuid::Uuid16 guid;
        if (!Uuid::TryParse(clipGuid, guid) || !FileGuid(guid).IsRandomV4())
        {
            error = "A canonical clip GUID is required.";
            return false;
        }
        std::lock_guard lock(g_acceptanceMutex);
        if (g_acceptanceRequest)
        {
            error = "An authoring request is already queued.";
            return false;
        }
        g_acceptanceRequest = AuthoringAcceptanceRequest{ directory, std::string(name), std::string(clipGuid) };
        g_acceptanceStatus = "Queued";
        editor::queue_window_request(kWindowID, editor::window_request::open);
        return true;
    }

    std::string AuthoringAcceptanceStatus()
    {
        std::lock_guard lock(g_acceptanceMutex);
        return g_acceptanceStatus;
    }

    std::string PreviewStatus()
    {
        std::lock_guard lock(g_previewMailbox.m_mutex);
        return g_previewMailbox.m_status;
    }

    void Draw()
    {
        std::optional<AuthoringAcceptanceRequest> acceptance;
        {
            std::lock_guard lock(g_acceptanceMutex);
            acceptance = std::move(g_acceptanceRequest);
            g_acceptanceRequest.reset();
        }
        if (acceptance)
        {
            std::string error;
            std::filesystem::path created;
            bool passed = false;
            if (g_session && (g_session->m_dirty || g_session->m_sourceDirty))
            {
                error = "Save the current sound asset before running acceptance.";
            }
            else if (CreateAsset(acceptance->directory, acceptance->name, false, created, error))
            {
                Uuid::Uuid16 clip;
                Uuid::TryParse(acceptance->clipGuid, clip);
                g_session->m_graph.nodes.front().clip = wave::ClipKey::FromGuid(clip);
                g_session->m_graph.nodes.front().gain = 0.001f;
                g_session->m_dirty = true;
                const auto identity = g_session->m_guid;
                if (Save(*g_session))
                {
                    const auto payload = Serialize(*g_session);
                    passed = OpenAsset(created, error) && g_session->m_guid == identity
                        && Serialize(*g_session) == payload && Validate(*g_session);
                    if (passed)
                    {
                        QueuePreview(*g_session);
                    }
                }
                if (!passed && error.empty())
                {
                    error = g_session->m_message;
                }
            }
            std::lock_guard lock(g_acceptanceMutex);
            g_acceptanceStatus = passed ? "Saved/reloaded; preview queued: " + PathText(created) : "Failed: " + error;
        }
        {
            std::lock_guard lock(g_previewMailbox.m_mutex);
            g_previewMailbox.m_lastVisible = std::chrono::steady_clock::now();
        }
        if (!g_session)
        {
            ImGui::TextWrapped("Create a Sound Graph or Sound Preset in Content Browser, or double-click an existing asset.");
            return;
        }
        auto& session = *g_session;
        if (g_pendingSession)
        {
            ImGui::OpenPopup("Unsaved sound asset");
        }
        if (ImGui::BeginPopupModal("Unsaved sound asset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("Save changes to %s before opening the other asset?", PathText(session.m_path.filename()).c_str());
            const bool save = ImGui::Button("Save and open");
            ImGui::SameLine();
            const bool discard = ImGui::Button("Discard and open");
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
            {
                g_pendingSession.reset();
                ImGui::CloseCurrentPopup();
            }
            const bool saved = save && Save(session);
            if (saved && g_pendingSession && g_pendingSession->m_path == session.m_path)
            {
                // A same-file reopen must not replace the just-saved draft
                // with the stale disk snapshot captured before the prompt.
                g_pendingSession = session;
            }
            if (discard || saved)
            {
                QueueStop();
                g_session = std::move(g_pendingSession);
                g_pendingSession.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        ImGui::Text("%s%s", PathText(session.m_path.filename()).c_str(), (session.m_dirty || session.m_sourceDirty) ? " *" : "");
        ImGui::TextDisabled("%s", PathText(session.m_path).c_str());
        if (ImGui::Button("Save")) { Save(session); }
        ImGui::SameLine();
        if (ImGui::Button("Validate")) { Validate(session); }
        ImGui::SameLine();
        if (ImGui::Button("Play preview") && Validate(session))
        {
            QueuePreview(session);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop preview")) { QueueStop(); }
        std::string status;
        {
            std::lock_guard lock(g_previewMailbox.m_mutex);
            status = g_previewMailbox.m_status;
        }
        ImGui::TextWrapped("%s", status.c_str());
        ImGui::TextDisabled("Preview stops when this panel is hidden. Unsaved edits remain in this editor.");
        if (!session.m_message.empty())
        {
            ImGui::TextWrapped("%s", session.m_message.c_str());
        }
        DrawPreviewParameters(session);
        if (ImGui::BeginTabBar("SoundAssetTabs"))
        {
            if (ImGui::BeginTabItem("Properties"))
            {
                if (session.m_isPreset) { DrawPreset(session); }
                else { DrawGraph(session); }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("YAML"))
            {
                DrawSource(session);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }

    void TickPreview(wave::PlaybackService* playback)
    {
        std::optional<PreviewRequest> request;
        bool visible = false;
        {
            std::lock_guard lock(g_previewMailbox.m_mutex);
            request = std::move(g_previewMailbox.m_request);
            g_previewMailbox.m_request.reset();
            visible = std::chrono::steady_clock::now() - g_previewMailbox.m_lastVisible < std::chrono::seconds(1);
        }
        std::string status;
        if (!visible || !playback)
        {
            StopRuntime(playback);
            status = playback ? "Preview stopped (panel hidden)" : "Audio playback is unavailable";
        }
        else if (request)
        {
            StopRuntime(playback);
            status = "Preview stopped";
            if (request->m_action == PreviewAction::Play)
            {
                bool registered = false;
                if (request->m_isPreset)
                {
                    registered = playback->RegisterPreset(request->m_temporaryID, request->m_preset);
                    status = playback->LastError();
                }
                else
                {
                    auto program = wave::CompileSoundGraph(request->m_graph, [playback](const wave::ClipKey& key)
                    {
                        return playback->HasSource({ wave::SoundSourceKind::Clip, key });
                    }, status);
                    registered = program && playback->RegisterGraph(request->m_temporaryID, std::move(program));
                    if (!registered && status.empty())
                    {
                        status = playback->LastError();
                    }
                }
                if (registered)
                {
                    g_previewRuntime.m_service = playback;
                    g_previewRuntime.m_temporaryID = request->m_temporaryID;
                    g_previewRuntime.m_scope = playback->CreateScope(wave::ScopeKind::EditorPreview);
                    wave::PlaybackRequest play;
                    play.source = { request->m_isPreset ? wave::SoundSourceKind::Preset : wave::SoundSourceKind::Graph,
                        request->m_temporaryID };
                    play.parameters = request->m_parameters;
                    g_previewRuntime.m_playback = playback->Play2D(g_previewRuntime.m_scope, std::move(play));
                    status = g_previewRuntime.m_playback.IsValid() ? "Preview playing" : playback->LastError();
                    if (!g_previewRuntime.m_playback.IsValid())
                    {
                        StopRuntime(playback);
                    }
                }
                else if (status.empty())
                {
                    status = "Preview registration failed. Check runtime asset dependencies.";
                }
            }
        }
        else if (g_previewRuntime.m_playback.IsValid()
            && !playback->IsAlive(g_previewRuntime.m_playback))
        {
            StopRuntime(playback);
            status = "Preview finished";
        }
        if (!status.empty())
        {
            std::lock_guard lock(g_previewMailbox.m_mutex);
            g_previewMailbox.m_status = std::move(status);
        }
    }

    void ShutdownPreview(wave::PlaybackService* playback)
    {
        StopRuntime(playback);
        std::lock_guard lock(g_previewMailbox.m_mutex);
        g_previewMailbox.m_accepting = false;
        g_previewMailbox.m_request.reset();
        g_previewMailbox.m_status = "Preview stopped";
    }
}

namespace editor::windows
{
    void draw_sound_graph()
    {
        sound_graph_editing::Draw();
    }
}
