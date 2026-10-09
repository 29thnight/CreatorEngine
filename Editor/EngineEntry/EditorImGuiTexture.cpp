#include "EditorImGuiTexture.h"
#include "DataSystem.h"
#include "RHI/IImGuiHost.h"

#include <unordered_map>

namespace EditorImGuiTexture
{
    namespace
    {
        struct PreparedImage
        {
            own::weak_owner<const Texture> descriptor;
            AssetDepot::AssetRequest<Texture::CodecImage> request;
            own::shared_owner<const Texture::CodecImage> image;
            bool requested{};
            bool demanded{};
        };

        // Presentation-thread consumer state only. DataSystem owns accepted jobs
        // and drain tracking; dropping this request cancels only this subscriber.
        std::unordered_map<uint64_t, PreparedImage>& PreparedImages()
        {
            static std::unordered_map<uint64_t, PreparedImage> images;
            return images;
        }
    }

    bool IsHostActive() noexcept
    {
        return GetImGuiHost().IsActive();
    }

    void BeginFrame()
    {
        for (auto& [identity, prepared] : PreparedImages())
        {
            prepared.demanded = false;
        }
    }

    void EndFrame()
    {
        auto& images = PreparedImages();
        for (auto found = images.begin(); found != images.end();)
        {
            if (!found->second.demanded || !found->second.descriptor.lock())
            {
                found->second.request.Cancel();
                found = images.erase(found);
            }
            else
            {
                ++found;
            }
        }
    }

    void Shutdown()
    {
        for (auto& [identity, prepared] : PreparedImages())
        {
            prepared.request.Cancel();
        }
        PreparedImages().clear();
    }

    uint64_t From(const Texture* texture)
    {
        IImGuiHost& host = GetImGuiHost();
        if (!host.IsActive())
        {
            return 0;
        }
        // This borrow never schedules work or attempts to recover an owner.
        // Generated editor artwork explicitly retains its irreplaceable source.
        const auto image = texture && !host.IsTextureReady(texture)
            ? texture->NonRehydratableImage()
            : own::shared_owner<const Texture::CodecImage>{};
        return host.RegisterTexture(texture, image);
    }

    uint64_t From(const own::shared_owner<const Texture>& texture)
    {
        IImGuiHost& host = GetImGuiHost();
        if (!host.IsActive())
        {
            return 0;
        }
        if (!texture)
        {
            return host.RegisterTexture(nullptr, {});
        }

        const Texture* descriptor = &*texture.borrow();
        const auto identity = static_cast<uint64_t>(texture->m_assetId.m_ID_Data);
        auto& images = PreparedImages();
        // Let a GPU hit (including a newly allocated ImGui slot) resolve first.
        // A valid native texture never requires CPU image residency.
        const auto residentId = host.RegisterTexture(descriptor, {});
        if (host.IsTextureReady(descriptor))
        {
            if (const auto found = images.find(identity); found != images.end())
            {
                found->second.request.Cancel();
                images.erase(found);
            }
            return residentId;
        }

        auto& prepared = images[identity];
        prepared.descriptor = texture;
        prepared.demanded = true;
        if (!prepared.image && prepared.requested)
        {
            const auto result = prepared.request.Snapshot();
            if (result.status == AssetDepot::AssetRequestStatus::Ready)
            {
                // Keep this owner before releasing the request's result. A
                // zero-budget cache has no other strong handoff reference.
                prepared.image = result.asset;
                prepared.request = {};
            }
        }
        if (!prepared.image)
        {
            // A terminal subscriber is not an instruction to restart I/O. A
            // later compatible cache success from another exact consumer may
            // still satisfy this visible preview without hiding/reopening it.
            prepared.image = texture->NonRehydratableImage();
            if (!prepared.image)
            {
                if (auto* data = DataSystem::GetIfAlive())
                {
                    prepared.image = data->TryAcquire<Texture::CodecImage>(texture);
                    if (!prepared.image && !prepared.requested)
                    {
                        prepared.request = data->RequestAsync<Texture::CodecImage>(texture);
                        prepared.requested = true;
                    }
                }
            }
        }

        const auto textureId = prepared.image
            ? host.RegisterTexture(descriptor, prepared.image) : residentId;
        if (host.IsTextureReady(descriptor))
        {
            // The host copies rows synchronously. GPU upload and submission
            // allocations keep their existing completion ownership.
            prepared.request.Cancel();
            images.erase(identity);
        }
        return textureId;
    }

    void Prime(const Texture* texture)
    {
        if (texture)
        {
            (void)From(texture);
        }
    }

    void Prime(const own::shared_owner<const Texture>& texture,
        const own::shared_owner<const Texture::CodecImage>& image)
    {
        IImGuiHost& host = GetImGuiHost();
        if (texture && host.IsActive())
        {
            (void)host.RegisterTexture(&*texture.borrow(), image);
        }
    }

    bool IsReady(const Texture* texture)
    {
        if (!texture)
        {
            return false;
        }
        IImGuiHost& host = GetImGuiHost();
        return host.IsActive() && host.IsTextureReady(texture);
    }
}
