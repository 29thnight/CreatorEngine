#pragma once

#include "Ownership.h"
#include "Texture.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

// A recording's CPU texture representations, pinned once per stable runtime ID.
// Mutable append-only recording bookkeeping, shared across views and accepted
// packets. All access is synchronized; published slots are never replaced/erased.
// This table is not an immutable asset generation.
// Draw records carry only indices and borrows; native GPU retirement stays in RHI.
class TextureFramePins final
{
public:
    static constexpr std::size_t InvalidIndex = (std::numeric_limits<std::size_t>::max)();

    static std::uint64_t Identity(const Texture* texture) noexcept
    {
        return texture ? static_cast<std::uint64_t>(texture->m_assetId) : 0;
    }

    std::size_t Retain(const own::shared_owner<const Texture>& texture)
    {
        if (!texture)
        {
            return InvalidIndex;
        }
        std::lock_guard lock(mutex_);
        const auto id = static_cast<std::uint64_t>(texture->m_assetId);
        if (const auto found = indices_.find(id); found != indices_.end())
        {
            return found->second;
        }
        const auto index = textures_.size();
        textures_.push_back(texture);
        try
        {
            indices_.emplace(id, index);
        }
        catch (...)
        {
            textures_.pop_back();
            throw;
        }
        return index;
    }

    const Texture* Borrow(std::size_t index) const
    {
        std::lock_guard lock(mutex_);
        if (index >= textures_.size())
        {
            return nullptr;
        }
        return &*textures_[index].borrow();
    }

    std::size_t Size() const
    {
        std::lock_guard lock(mutex_);
        return textures_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<own::shared_owner<const Texture>> textures_;
    std::unordered_map<std::uint64_t, std::size_t> indices_;
};
