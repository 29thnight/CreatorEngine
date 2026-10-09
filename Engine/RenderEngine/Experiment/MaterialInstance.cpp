#include "MaterialInstance.h"
#include "MaterialResolver.h"

#include <algorithm>

namespace experiment
{
    bool MaterialInstance::BindPreparedTextureOwners(
        std::span<const own::shared_owner<const Texture>> textures, std::string& outError)
    {
        if (!base_ || !base_->assetOrigin)
        {
            outError.clear();
            return true;
        }
        Material effective;
        if (!BuildEffectiveMaterial(effective, outError)
            || !BindPreparedMaterialTextures(effective, textures, outError))
        {
            return false;
        }
        auto base = own::make_shared<Material>(*base_);
        base->assetOrigin = std::move(effective.assetOrigin);
        base_ = std::move(base);
        ++revision_;
        outError.clear();
        return true;
    }

    bool MaterialInstance::SetPropertyOverride(std::string_view name,
        MaterialPropertyValue value)
    {
        if (name.empty()) return false;
        const auto found = std::find_if(propertyOverrides_.begin(),
            propertyOverrides_.end(), [&](const MaterialProperty& candidate)
            {
                return candidate.name == name;
            });
        if (found != propertyOverrides_.end())
        {
            found->value = std::move(value);
        }
        else
        {
            propertyOverrides_.push_back({ std::string(name), std::move(value) });
        }
        ++revision_;
        return true;
    }

    bool MaterialInstance::ClearPropertyOverride(std::string_view name)
    {
        const auto found = std::find_if(propertyOverrides_.begin(),
            propertyOverrides_.end(), [&](const MaterialProperty& candidate)
            {
                return candidate.name == name;
            });
        if (found == propertyOverrides_.end()) return false;
        propertyOverrides_.erase(found);
        ++revision_;
        return true;
    }

    bool MaterialInstance::AddKeywordOverride(std::string_view keywordValue)
    {
        if (keywordValue.empty()) return false;
        const auto found = std::find(keywordOverrides_.begin(),
            keywordOverrides_.end(), keywordValue);
        if (found != keywordOverrides_.end()) keywordOverrides_.erase(found);
        keywordOverrides_.emplace_back(keywordValue);
        ++revision_;
        return true;
    }

    bool MaterialInstance::ClearKeywordOverride(std::string_view keywordValue)
    {
        const auto found = std::find(keywordOverrides_.begin(),
            keywordOverrides_.end(), keywordValue);
        if (found == keywordOverrides_.end()) return false;
        keywordOverrides_.erase(found);
        ++revision_;
        return true;
    }

    void MaterialInstance::SetKeywordSelectionOverrides(std::span<const std::uint16_t> selections)
    {
        std::vector<std::uint16_t> candidate(selections.begin(), selections.end());
        keywordSelectionOverrides_ = std::move(candidate);
        keywordOverrides_.clear();
        ++revision_;
    }

    bool MaterialInstance::ClearKeywordSelectionOverrides()
    {
        if (!keywordSelectionOverrides_)
        {
            return false;
        }
        keywordSelectionOverrides_.reset();
        ++revision_;
        return true;
    }

    void MaterialInstance::ClearAllOverrides()
    {
        if (propertyOverrides_.empty() && keywordOverrides_.empty() && !keywordSelectionOverrides_)
        {
            return;
        }
        propertyOverrides_.clear();
        keywordOverrides_.clear();
        keywordSelectionOverrides_.reset();
        ++revision_;
    }

    bool MaterialInstance::BuildEffectiveMaterial(Material& outMaterial,
        std::string& outError) const
    {
        if (!base_)
        {
            outError = "MaterialInstance에 base material이 없다";
            return false;
        }

        Material effective = *base_;
        for (const MaterialProperty& override_ : propertyOverrides_)
        {
            const auto found = std::find_if(effective.properties.begin(),
                effective.properties.end(), [&](const MaterialProperty& candidate)
                {
                    return candidate.name == override_.name;
                });
            if (found != effective.properties.end()) found->value = override_.value;
            else effective.properties.push_back(override_);
        }
        if (keywordSelectionOverrides_)
        {
            effective.keywordSelections = *keywordSelectionOverrides_;
            effective.keywords.clear();
        }
        // resolver는 목록 순서대로 축 선택을 덮으므로, 뒤에 덧붙인 override가
        // base의 같은 축 선택을 이긴다.
        for (const std::string& keyword : keywordOverrides_)
            effective.keywords.push_back(keyword);

        outMaterial = std::move(effective);
        outError.clear();
        return true;
    }
}
