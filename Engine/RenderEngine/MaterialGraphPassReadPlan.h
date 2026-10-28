#include "../Containers/ContainerAPI.h"
#pragma once

#include "MaterialGraphRenderBindings.h"
#include "RenderAllocator.h"
#include "Render/Graph/EnhancedRenderGraph.h"
#include <array>
#include <cstdlib>
#include <cstring>

namespace material_graph
{
    inline bool PassReadPlanReuseEnabled()
    {
        static const bool enabled = []
        {
            char flag[8]{};
            size_t bytes{};
            getenv_s(&bytes, flag, sizeof(flag), "CREATOR_PASS_READ_PLAN_REUSE");
            return std::strcmp(flag, "0") != 0;
        }();
        return enabled;
    }
    // CPU recipes only. Draw indices resolve against the current sealed frame;
    // neither graph handles nor native resource owners survive in this cache.
    class PassReadPlan final
    {
      public:
        enum class Pass : uint32_t
        {
            Surface,
            StaticShadow,
            DynamicShadow,
            Count
        };
        struct Reads
        {
            ce::render_vector<size_t> geometry;
            ce::render_vector<RHITextureHandle> textures;
            ce::fixed_array<ce::render_vector<EnhancedRenderGraph::RGPassUsage>, 2> usageTemplates;
        };

        template<class Draws, class Shadows>
        bool Matches(const Draws& draws, const Shadows& shadows) const
        {
            size_t identity{}, texture{};
            const auto matches = [&](const auto& input)
            {
                for (const auto& draw : input)
                {
                    if (identity == m_identities.size())
                    {
                        return false;
                    }
                    const auto& saved = m_identities[identity++];
                    const auto resources =
                        draw.bindings ? draw.bindings->resources.textures : std::span<const RHIBindingDesc>{};
                    if (saved.flags != Flags(draw) || saved.textures != resources.size())
                    {
                        return false;
                    }
                    for (const auto& resource : resources)
                    {
                        if (m_textures[texture++].id != resource.resource.id)
                        {
                            return false;
                        }
                    }
                }
                return true;
            };
            return m_drawCount == draws.size() && m_shadowCount == shadows.size() && matches(draws) &&
                   matches(shadows) && identity == m_identities.size();
        }

        template<class Draws, class Shadows>
        PassReadPlan(const Draws& draws, const Shadows& shadows)
            : m_drawCount(draws.size()), m_shadowCount(shadows.size())
        {
            const auto append = [&](const auto& input, bool shadow)
            {
                size_t index{};
                for (const auto& draw : input)
                {
                    const auto flags = Flags(draw);
                    const auto resources =
                        draw.bindings ? draw.bindings->resources.textures : std::span<const RHIBindingDesc>{};
                    m_identities.push_back({flags, resources.size()});
                    Reads* reads{};
                    if (shadow && draw.shadowEnabled)
                    {
                        reads =
                            &m_reads[static_cast<size_t>(draw.staticShadow ? Pass::StaticShadow : Pass::DynamicShadow)];
                    }
                    else if (!shadow && (flags & 7u) == 1u)
                    {
                        reads = &m_reads[static_cast<size_t>(Pass::Surface)];
                    }
                    if (reads)
                    {
                        reads->geometry.push_back(index);
                    }
                    for (const auto& resource : resources)
                    {
                        m_textures.push_back(resource.resource);
                        if (reads && std::ranges::none_of(reads->textures,
                                                          [&](auto saved) { return saved.id == resource.resource.id; }))
                        {
                            reads->textures.push_back(resource.resource);
                        }
                    }
                    ++index;
                }
            };
            append(draws, false);
            append(shadows, true);
            for (auto& reads : m_reads)
            {
                for (size_t variant = 0; variant < reads.usageTemplates.size(); ++variant)
                {
                    auto& usages = reads.usageTemplates[variant];
                    const auto geometryState =
                        variant ? RHIResourceState::PixelShaderResource : RHIResourceState::ShaderResource;
                    usages.resize(reads.geometry.size() * 2 + reads.textures.size());
                    for (size_t index = 0; index < usages.size(); ++index)
                    {
                        usages[index] = {{},
                                         index < reads.geometry.size() * 2 ? geometryState
                                                                           : RHIResourceState::PixelShaderResource,
                                         RGAccessMode::Read};
                    }
                }
            }
        }

        const Reads& Get(Pass pass) const { return m_reads[static_cast<size_t>(pass)]; }
        size_t StorageBytes() const
        {
            size_t bytes = sizeof(*this) + m_identities.capacity() * sizeof(Identity) +
                           m_textures.capacity() * sizeof(RHITextureHandle);
            for (const auto& reads : m_reads)
            {
                bytes +=
                    reads.geometry.capacity() * sizeof(size_t) + reads.textures.capacity() * sizeof(RHITextureHandle);
                for (const auto& usages : reads.usageTemplates)
                {
                    bytes += usages.capacity() * sizeof(EnhancedRenderGraph::RGPassUsage);
                }
            }
            return bytes;
        }

      private:
        template<class Draw>
        static uint32_t Flags(const Draw& draw)
        {
            return uint32_t(draw.program && draw.program->hasSurface) |
                   (uint32_t(draw.program && draw.program->hasTransmission) << 1) | (uint32_t(draw.blended) << 2) |
                   (uint32_t(draw.shadowEnabled) << 3) | (uint32_t(draw.staticShadow) << 4);
        }
        struct Identity
        {
            uint32_t flags{};
            size_t textures{};
        };
        size_t m_drawCount{}, m_shadowCount{};
        ce::render_vector<Identity> m_identities;
        ce::render_vector<RHITextureHandle> m_textures;
        ce::fixed_array<Reads, static_cast<size_t>(Pass::Count)> m_reads;
    };
} // namespace material_graph
