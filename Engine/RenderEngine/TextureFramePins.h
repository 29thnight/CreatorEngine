#pragma once

#include "Ownership.h"
#include "Texture.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <unordered_map>
#include <vector>

// A recording's small texture descriptions and explicitly prepared image payloads.
// Each kind is pinned once per stable runtime ID; descriptions do not imply pixels.
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

    std::vector<own::shared_owner<const Texture>> Owners() const
    {
        std::lock_guard lock(mutex_);
        return textures_;
    }

    void RetainImage(const own::shared_owner<const Texture>& texture,
        const own::shared_owner<const Texture::CodecImage>& image)
    {
        if (!texture || !image)
        {
            return;
        }
        Retain(texture);
        std::lock_guard lock(mutex_);
        // Exact descriptor identity fixes the image key. Never replace a live
        // payload (or destroy its last owner) while holding the table lock.
        images_.try_emplace(Identity(&*texture), image);
    }

    own::shared_owner<const Texture::CodecImage> Image(const Texture* texture) const
    {
        if (!texture)
        {
            return {};
        }
        std::lock_guard lock(mutex_);
        const auto found = images_.find(Identity(texture));
        if (found != images_.end())
        {
            return found->second;
        }
        // Generated/legacy images explicitly retain their only source. This is
        // resident-only and never asks the descriptor to decode or reopen a file.
        return texture->NonRehydratableImage();
    }

    void ReleaseImages() noexcept
    {
        // A default node handle owns no allocated bucket array. This path also
        // runs from abort guards, where allocating an empty map could terminate
        // exception unwinding on the target STL.
        for (;;)
        {
            decltype(images_)::node_type retired;
            {
                std::lock_guard lock(mutex_);
                if (images_.empty())
                {
                    return;
                }
                retired = images_.extract(images_.begin());
            }
            // The final image owner, if any, dies after the table lock.
        }
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
    std::unordered_map<std::uint64_t, own::shared_owner<const Texture::CodecImage>> images_;
};
