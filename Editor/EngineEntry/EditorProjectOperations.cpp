#include "EditorProjectOperations.h"
#include "TagManager.h"
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
            return !name.empty() && std::none_of(name.begin(), name.end(),
                [](unsigned char c) { return c < 32 || c == 127; });
        }

        class TagDefinitionsCommand final : public Meta::IUndoableCommand
        {
        public:
            TagDefinitionsCommand(std::vector<std::string> before, std::vector<std::string> after)
                : m_before(std::move(before)), m_after(std::move(after)) {}
            void Undo() override { Apply(m_before); }
            void Redo() override { Apply(m_after); }
        private:
            static void Apply(const std::vector<std::string>& definitions)
            {
                const auto previous = TagManagers->GetTags();
                TagManagers->SetTagDefinitions(definitions);
                try
                {
                    if (!TagManagers->Save()) throw std::runtime_error("Cannot persist project tags");
                }
                catch (...)
                {
                    TagManagers->SetTagDefinitions(previous);
                    throw;
                }
            }
            std::vector<std::string> m_before, m_after;
        };

        CommandCore::CommandResult ChangeTag(const std::string& name, bool remove)
        {
            using namespace CommandCore;
            if (!ValidTag(name) || name == "Untagged") return InvalidArguments("Invalid or reserved tag name");
            const auto before = TagManagers->GetTags();
            auto after = before;
            const auto found = std::find(after.begin(), after.end(), name);
            const bool changed = remove ? found != after.end() : found == after.end();
            if (changed)
            {
                if (remove && !TagManagers->GetObjectsWithTag(name).empty())
                    return PreconditionFailed("tag.in_use", "Cannot remove a tag assigned to scene objects");
                if (remove) after.erase(found); else after.push_back(name);
                try
                {
                    Meta::UndoManager::GetInstance()->Execute(
                        std::make_unique<TagDefinitionsCommand>(before, std::move(after)));
                }
                catch (const std::exception& e) { return Fail("tag.save_failed", e.what()); }
            }
            auto result = HasTag(name);
            result.data.Set("changed", CommandData::Bool(changed));
            std::printf("[tag.%s] has=%s\n", remove ? "remove" : "add",
                TagManagers->HasTag(name) ? "true" : "false");
            return result;
        }
    }

    CommandCore::CommandResult Tags()
    {
        using namespace CommandCore;
        CommandData data = CommandData::Object(), tags = CommandData::Array(), layers = CommandData::Array();
        for (const auto& tag : TagManagers->GetTags()) tags.Append(CommandData::String(tag));
        for (const auto& layer : TagManagers->GetLayers()) layers.Append(CommandData::String(layer));
        data.Set("tags", std::move(tags));
        data.Set("layers", std::move(layers));
        return Ok("Project tags and layers", std::move(data));
    }

    CommandCore::CommandResult HasTag(const std::string& name)
    {
        using namespace CommandCore;
        if (!ValidTag(name)) return InvalidArguments("Invalid tag name");
        CommandData data = CommandData::Object();
        data.Set("name", CommandData::String(name));
        data.Set("exists", CommandData::Bool(TagManagers->HasTag(name)));
        std::printf("[tag.has] has=%s\n", TagManagers->HasTag(name) ? "true" : "false");
        return Ok("Project tag", std::move(data));
    }

    CommandCore::CommandResult AddTag(const std::string& name) { return ChangeTag(name, false); }
    CommandCore::CommandResult RemoveTag(const std::string& name) { return ChangeTag(name, true); }
}
