#pragma once

#include "../Core/LXGraph.h"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <map>
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
    Card
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
};

class LXNodeItemRegistry
{
  public:
    using TextureResolver = std::function<ImTextureID(const std::string& assetId)>;

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

    ImTextureID ResolveTexture(const std::string& assetId) const
    {
        return textureResolver_ ? textureResolver_(assetId) : ImTextureID_Invalid;
    }

  private:
    std::map<std::string, std::map<std::string, LXNodeItemSpec>> definitions_;
    TextureResolver textureResolver_;
};

} // namespace LX
