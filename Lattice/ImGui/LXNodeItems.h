#pragma once

#include "../Core/LXGraph.h"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace LX
{

enum class LXNodeItemKind
{
    Text,
    FloatSlider,
    Color,
    TexturePreview,
    Card,
    Section,
    Vector,
    Boolean,
    Integer,
    Choice,
    String
};

// A domain supplies presentation and editing range. Socket values stay in LXGraph;
// the graph never owns an ImGui texture.
struct LXNodeItemSpec
{
    std::string key;
    LXNodeItemKind kind = LXNodeItemKind::Text;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float height = 0.0f;
    // The input identifier shares the item's row and keeps its pin beside the control.
    std::string inputPin;
    std::string styleRole;
    std::string label;
    std::string valuePin;
    std::vector<std::string> choices;
    std::string propertyKey;
};

struct LXNodeRow
{
    std::string key;
    std::string pinIdentifier;
    Direction direction = Direction::Input;
    std::string section;
};

class LXNodeItemRegistry
{
  public:
    using TextureResolver = std::function<ImTextureID(const std::string& assetId)>;
    using PinPredicate = std::function<bool(const Pin&)>;
    using ResourceEditor = std::function<std::optional<std::string>()>;

    // Pin geometry asks for the same visible rows repeatedly. Keep those rows
    // only for one canvas draw, so opaque predicates are reevaluated next frame.
    class RowCacheScope
    {
      public:
        explicit RowCacheScope(const LXNodeItemRegistry& registry) : registry_(registry)
        {
            if (registry_.rowCacheDepth_++ == 0)
            {
                registry_.visibleRows_.clear();
            }
        }

        ~RowCacheScope()
        {
            if (--registry_.rowCacheDepth_ == 0)
            {
                registry_.visibleRows_.clear();
            }
        }

        RowCacheScope(const RowCacheScope&) = delete;
        RowCacheScope& operator=(const RowCacheScope&) = delete;

        void Invalidate() const { registry_.visibleRows_.clear(); }

      private:
        const LXNodeItemRegistry& registry_;
    };

    void RegisterRows(std::string nodeType, std::vector<LXNodeRow> rows)
    {
        rows_.insert_or_assign(std::move(nodeType), std::move(rows));
        visibleRows_.clear();
    }

    bool HasRows(const Node& node) const { return rows_.contains(node.type); }

    const Pin* RowPin(const Node& node, const LXNodeRow& row) const
    {
        const auto pin = std::find_if(node.pins.begin(), node.pins.end(), [&](const Pin& candidate) {
            return candidate.Identifier() == row.pinIdentifier && candidate.direction == row.direction;
        });
        return row.pinIdentifier.empty() || pin == node.pins.end() ? nullptr : &*pin;
    }

    bool SectionOpen(const Node& node, const std::string& section) const
    {
        if (section.empty() || openSections_.contains({node.id, section}))
        {
            return true;
        }
        const auto layout = rows_.find(node.type);
        if (layout != rows_.end() && connected_)
        {
            for (const auto& row : layout->second)
            {
                if (row.section != section) continue;
                const Pin* pin = RowPin(node, row);
                if (pin && connected_(*pin))
                {
                    return true;
                }
            }
        }
        return false;
    }

    void ToggleSection(const Node& node, const std::string& section) const
    {
        const auto key = std::make_pair(node.id, section);
        if (!openSections_.erase(key))
        {
            openSections_.insert(key);
        }
        visibleRows_.erase(node.id);
    }

    std::vector<LXNodeRow> Rows(const Node& node) const { return VisibleRows(node); }

    const std::vector<LXNodeRow>& VisibleRows(const Node& node) const
    {
        const auto cached = visibleRows_.find(node.id);
        if (rowCacheDepth_ > 0 && cached != visibleRows_.end())
        {
            return cached->second;
        }
        return visibleRows_.insert_or_assign(node.id, BuildRows(node)).first->second;
    }

  private:
    std::vector<LXNodeRow> BuildRows(const Node& node) const
    {
        std::vector<LXNodeRow> result;
        const auto layout = rows_.find(node.type);
        if (layout == rows_.end())
        {
            return result;
        }
        for (const auto& row : layout->second)
        {
            const Pin* pin = RowPin(node, row);
            if ((!pin || !visible_ || visible_(*pin) || (connected_ && connected_(*pin))) &&
                SectionOpen(node, row.section))
            {
                result.push_back(row);
            }
        }
        return result;
    }

  public:
    std::optional<std::size_t> PinRow(const Node& node, const Pin& pin) const
    {
        const auto& rows = VisibleRows(node);
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            if (!rows[index].pinIdentifier.empty() && rows[index].direction == pin.direction && rows[index].pinIdentifier == pin.Identifier())
            {
                return index;
            }
        }
        return std::nullopt;
    }

    void SetPinPredicates(PinPredicate visible, PinPredicate connected)
    {
        visible_ = std::move(visible);
        connected_ = std::move(connected);
        visibleRows_.clear();
    }
    bool Connected(const Pin& pin) const { return connected_ && connected_(pin); }

    void Register(std::string nodeType, LXNodeItemSpec item)
    {
        const std::string key = item.key;
        definitions_[std::move(nodeType)].insert_or_assign(key, std::move(item));
    }

    const LXNodeItemSpec* Find(const Node& node, const std::string& key) const
    {
        const auto type = definitions_.find(node.type);
        if (type == definitions_.end())
        {
            return nullptr;
        }
        const auto item = type->second.find(key);
        return item == type->second.end() ? nullptr : &item->second;
    }

    std::vector<std::string> Keys(const Node& node) const
    {
        std::vector<std::string> keys;
        if (HasRows(node))
        {
            for (const auto& row : VisibleRows(node))
            {
                if (!row.key.empty())
                {
                    keys.push_back(row.key);
                }
            }
            return keys;
        }
        keys.reserve(node.properties.size());
        for (const auto& [key, value] : node.properties)
        {
            keys.push_back(key);
        }
        const auto type = definitions_.find(node.type);
        if (type != definitions_.end())
        {
            for (const auto& [key, item] : type->second)
            {
                if (std::find(keys.begin(), keys.end(), key) == keys.end())
                {
                    keys.push_back(key);
                }
            }
        }
        std::sort(keys.begin(), keys.end());
        return keys;
    }

    const Pin* ValuePin(const Node& node, const std::string& key) const
    {
        const LXNodeItemSpec* item = Find(node, key);
        if (!item || item->valuePin.empty())
        {
            return nullptr;
        }
        const auto pin = std::find_if(node.pins.begin(), node.pins.end(),
                                      [&](const Pin& candidate) { return candidate.Identifier() == item->valuePin; });
        return pin == node.pins.end() ? nullptr : &*pin;
    }

    void SetTextureResolver(TextureResolver resolver) { textureResolver_ = std::move(resolver); }
    void SetResourceEditor(ResourceEditor editor) { resourceEditor_ = std::move(editor); }
    std::optional<std::string> EditResource() const { return resourceEditor_ ? resourceEditor_() : std::nullopt; }

    ImTextureID ResolveTexture(const std::string& assetId) const
    {
        return textureResolver_ ? textureResolver_(assetId) : ImTextureID_Invalid;
    }

  private:
    std::map<std::string, std::map<std::string, LXNodeItemSpec>> definitions_;
    TextureResolver textureResolver_;
    ResourceEditor resourceEditor_;
    std::map<std::string, std::vector<LXNodeRow>> rows_;
    mutable std::set<std::pair<Id, std::string>> openSections_;
    PinPredicate visible_, connected_;
    mutable std::map<Id, std::vector<LXNodeRow>> visibleRows_;
    mutable unsigned rowCacheDepth_ = 0;
};

} // namespace LX
