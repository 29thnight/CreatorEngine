#include "EditorProjectOperations.h"
#include "TagManager.h"
#include "SceneManager.h"
#include "EditorProjectLayerSettings.h"
#include "ReflectionUndo.h"
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace EditorProjectOperations
{
namespace
{
bool ValidTag(const std::string& name)
{
    return !name.empty() && std::none_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c == 127; });
}

class TagDefinitionsCommand final : public Meta::IUndoableCommand
{
  public:
    TagDefinitionsCommand(std::vector<std::string> before, std::vector<std::string> after)
        : m_before(std::move(before)), m_after(std::move(after))
    {
    }
    void Undo() override { Apply(m_before); }
    void Redo() override { Apply(m_after); }

  private:
    static void Apply(const std::vector<std::string>& definitions)
    {
        const auto previous = TagManagers->GetTags();
        TagManagers->SetTagDefinitions(definitions);
        try
        {
            if (!TagManagers->Save())
                throw std::runtime_error("Cannot persist project tags");
        }
        catch (...)
        {
            TagManagers->SetTagDefinitions(previous);
            throw;
        }
    }
    std::vector<std::string> m_before, m_after;
};

class LayerSettingsCommand final : public Meta::IUndoableCommand
{
  public:
    LayerSettingsCommand(project_layer_snapshot before, project_layer_snapshot after)
        : m_before(std::move(before)), m_after(std::move(after)), m_project(SceneManagers->ProjectLayers()),
          m_persistAuthoring(!SceneManagers->IsGameStart())
    {
    }

    void Undo() override { Apply(m_before); }
    void Redo() override { Apply(m_after); }

  private:
    void Apply(const project_layer_snapshot& definitions)
    {
        const auto project = m_project.lock();
        if (!project || project != SceneManagers->ProjectLayers())
            throw std::runtime_error("Project layer Undo target is no longer active");

        if (!project->Restore(definitions, [this](const auto& prepared) -> ce::layers::result<void> {
                if (m_persistAuthoring && !Editor::SaveProjectLayerSettings(prepared))
                    return std::unexpected(ce::layers::error::io_failure);
                return {};
            }))
            throw std::runtime_error("Cannot publish project layer settings");
    }

    project_layer_snapshot m_before, m_after;
    std::weak_ptr<ProjectLayerSettings> m_project;
    // Keep the command's publication policy across Undo/Redo and host mode transitions.
    const bool m_persistAuthoring;
};

template<class Operation>
CommandCore::CommandResult ChangeLayers(Operation&& operation)
{
    using namespace CommandCore;
    const auto project = SceneManagers->ProjectLayers();
    if (!project)
        return PreconditionFailed("layer.unbound", "Project layers are not bound");

    ProjectLayerSettings preview;
    const auto before = project->Snapshot();
    if (!preview.Restore(*before) || !preview.Change(std::forward<Operation>(operation)))
        return InvalidArguments("Invalid layer definition or collision pair");

    try
    {
        Meta::UndoManager::GetInstance()->Execute(std::make_unique<LayerSettingsCommand>(*before, *preview.Snapshot()));
    }
    catch (const std::exception& e)
    {
        return Fail("layer.save_failed", e.what());
    }

    return Layers();
}

CommandCore::CommandResult ChangeTag(const std::string& name, bool remove)
{
    using namespace CommandCore;
    if (!ValidTag(name) || name == "Untagged")
        return InvalidArguments("Invalid or reserved tag name");
    const auto before = TagManagers->GetTags();
    auto after = before;
    const auto found = std::find(after.begin(), after.end(), name);
    const bool changed = remove ? found != after.end() : found == after.end();
    if (changed)
    {
        if (remove && !TagManagers->GetObjectsWithTag(name).empty())
            return PreconditionFailed("tag.in_use", "Cannot remove a tag assigned to scene objects");
        if (remove)
            after.erase(found);
        else
            after.push_back(name);
        try
        {
            Meta::UndoManager::GetInstance()->Execute(
                std::make_unique<TagDefinitionsCommand>(before, std::move(after)));
        }
        catch (const std::exception& e)
        {
            return Fail("tag.save_failed", e.what());
        }
    }
    auto result = HasTag(name);
    result.data.Set("changed", CommandData::Bool(changed));
    std::printf("[tag.%s] has=%s\n", remove ? "remove" : "add", TagManagers->HasTag(name) ? "true" : "false");
    return result;
}
} // namespace

CommandCore::CommandResult Tags()
{
    using namespace CommandCore;
    CommandData data = CommandData::Object(), tags = CommandData::Array(), layers = CommandData::Array();
    for (const auto& tag : TagManagers->GetTags())
        tags.Append(CommandData::String(tag));
    if (const auto project = SceneManagers->ProjectLayers())
        for (const auto& layer : project->Snapshot()->catalog.definitions)
            if (layer && !layer->retired)
                layers.Append(CommandData::String(layer->name));
    data.Set("tags", std::move(tags));
    data.Set("layers", std::move(layers));
    return Ok("Project tags and layers", std::move(data));
}

CommandCore::CommandResult Layers()
{
    using namespace CommandCore;
    const auto project = SceneManagers->ProjectLayers();
    if (!project)
        return PreconditionFailed("layer.unbound", "Project layers are not bound");

    const auto snapshot = project->Snapshot();
    auto data = CommandData::Object(), layers = CommandData::Array();
    for (const auto& layer : snapshot->catalog.definitions)
    {
        if (!layer || layer->retired)
            continue;
        auto item = CommandData::Object();
        item.Set("id", CommandData::String(std::to_string(layer->id.value)));
        item.Set("slot", CommandData::Int(layer->slot.Value()));
        item.Set("name", CommandData::String(layer->name));
        layers.Append(std::move(item));
    }
    data.Set("layers", std::move(layers));
    data.Set("revision", CommandData::String(std::to_string(snapshot->revision)));
    return Ok("Project layers", std::move(data));
}

CommandCore::CommandResult AddLayer(const std::string& name)
{
    return ChangeLayers([&](auto& catalog, auto&) -> ce::layers::result<void> {
        const auto added = catalog.Add(name);
        if (!added)
            return std::unexpected(added.error());
        return {};
    });
}

CommandCore::CommandResult RenameLayer(const std::string& name, const std::string& replacement)
{
    return ChangeLayers([&](auto& catalog, auto&) -> ce::layers::result<void> {
        const auto snapshot = catalog.Snapshot();
        const auto* layer = snapshot->Find(name);
        if (!layer)
            return std::unexpected(ce::layers::error::unknown_layer);
        return catalog.Rename(layer->id, replacement);
    });
}

CommandCore::CommandResult SetCollision(ce::layers::layer_id left, ce::layers::layer_id right, bool enabled)
{
    return ChangeLayers(
        [&](auto& catalog, auto& policy) { return policy.Set(*catalog.Snapshot(), left, right, enabled); });
}

CommandCore::CommandResult HasTag(const std::string& name)
{
    using namespace CommandCore;
    if (!ValidTag(name))
        return InvalidArguments("Invalid tag name");
    CommandData data = CommandData::Object();
    data.Set("name", CommandData::String(name));
    data.Set("exists", CommandData::Bool(TagManagers->HasTag(name)));
    std::printf("[tag.has] has=%s\n", TagManagers->HasTag(name) ? "true" : "false");
    return Ok("Project tag", std::move(data));
}

CommandCore::CommandResult AddTag(const std::string& name)
{
    return ChangeTag(name, false);
}
CommandCore::CommandResult RemoveTag(const std::string& name)
{
    return ChangeTag(name, true);
}
} // namespace EditorProjectOperations
