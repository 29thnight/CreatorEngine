#pragma once

#include "CollisionGeometryCodec.h"
#include "CookedCollisionGeometry.h"
#include "../Physics/PhysicsScene.h"
#include "../EngineDiagnostics/ProfileScope.h"
#include <flat_map>
#include <functional>
#include <thread>

namespace ce::physics
{
// Scene-owned, owner-thread cache. UUID/revision is immutable; cached assets own
// their SDK session and may outlive both this cache and the temporary cooking scene.
class CollisionGeometryLibrary final
{
  public:
    struct statistics
    {
        std::uint64_t cache_hits = 0, cooks = 0, failures = 0, imports = 0;
        std::size_t assets = 0;
    };

    CollisionGeometryLibrary() = default;
    CollisionGeometryLibrary(const CollisionGeometryLibrary&) = delete;
    CollisionGeometryLibrary& operator=(const CollisionGeometryLibrary&) = delete;

    template<class Loader>
    result<std::shared_ptr<const CollisionGeometry>> Resolve(geometry_asset_key key, Loader&& loader)
    {
        if (auto owner = RequireOwner(); !owner)
            return std::unexpected(owner.error());
        ce::profile_scope scope{ce::marker<"Physics.GeometryResolve">()};

        if (const auto found = m_assets.find(key); found != m_assets.end())
        {
            ++m_stats.cache_hits;
            return found->second.asset;
        }

        auto source = std::invoke(std::forward<Loader>(loader), key);
        if (!source)
            return std::unexpected(source.error());
        if (source->key != key)
            return std::unexpected(
                error{error_code::invalid_argument, 0, "Collision geometry identity/revision mismatch"});

        return Publish(*source);
    }

    result<std::shared_ptr<const CollisionGeometry>> Publish(const CollisionGeometrySource& source)
    {
        return Publish(source, [](std::span<const std::byte>) { return true; });
    }

    template<class Loader>
    result<std::shared_ptr<const CollisionGeometry>> ResolveCooked(geometry_asset_key key, Loader&& loader)
    {
        if (auto owner = RequireOwner(); !owner) return std::unexpected(owner.error());
        if (m_publishing)
            return std::unexpected(error{error_code::wrong_phase, 0, "Reentrant cooked geometry import"});

        ce::profile_scope scope{ce::marker<"Physics.GeometryArtifactResolve">()};
        if (const auto found = m_assets.find(key); found != m_assets.end())
        {
            ++m_stats.cache_hits;
            return found->second.asset;
        }

        m_publishing = true;
        struct guard { bool& active; ~guard() { active = false; } } reset{m_publishing};
        try
        {
            auto bytes = std::invoke(std::forward<Loader>(loader), key);
            if (!bytes) return std::unexpected(bytes.error());

            auto record = CookedCollisionGeometry::Decode(*bytes, key);
            if (!record) return std::unexpected(record.error());

            auto prepared = m_assets;
            auto payload = std::make_shared<const std::vector<std::byte>>(std::move(record->payload));
            const auto added = prepared.emplace(key, entry{{}, payload}).first;
            scene_config config;
            config.workers = 1;
            auto importer = PhysicsScene::create(config);
            if (!importer) return std::unexpected(importer.error());

            ce::profile_scope importing{ce::marker<"Physics.GeometryArtifactImport">()};
            auto asset = (*importer)->load_geometry_blob(record->kind, *payload);
            if (!asset) return std::unexpected(asset.error());

            added->second.asset = *asset;
            m_assets.swap(prepared);
            ++m_stats.imports;
            m_stats.assets = m_assets.size();
            return *asset;
        }
        catch (const std::bad_alloc&)
        {
            return std::unexpected(error{error_code::out_of_memory, 0, "Geometry import allocation failed"});
        }
        catch (...)
        {
            return std::unexpected(error{error_code::invalid_argument, 0, "Geometry artifact loader failed"});
        }
    }

    // The host publisher owns file staging/atomic publication. Prepare cache
    // allocation and cooking before that call, then commit with a noexcept swap.
    template<class Publisher>
    result<std::shared_ptr<const CollisionGeometry>> Publish(const CollisionGeometrySource& source,
                                                             Publisher&& publisher)
    {
        if (auto owner = RequireOwner(); !owner)
            return std::unexpected(owner.error());
        if (m_publishing)
            return std::unexpected(error{error_code::wrong_phase, 0, "Reentrant geometry publication is forbidden"});

        m_publishing = true;
        struct publication_guard
        {
            bool& value;
            ~publication_guard() { value = false; }
        } guard{m_publishing};

        ce::profile_scope scope{ce::marker<"Physics.GeometryPublish">()};
        auto encoded = CollisionGeometryCodec::Encode(source);
        if (!encoded)
            return std::unexpected(encoded.error());

        try
        {
            const auto found = m_assets.find(source.key);
            if (found != m_assets.end() && *found->second.source != *encoded)
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Existing geometry revision has different contents"});

            if (found != m_assets.end())
            {
                if (!std::invoke(std::forward<Publisher>(publisher), std::span<const std::byte>(*encoded)))
                    return std::unexpected(
                        error{error_code::invalid_argument, 0, "Collision geometry host publication failed"});
                ++m_stats.cache_hits;
                return found->second.asset;
            }

            // flat_map insertion can clear its storage on an exception; mutate
            // a prepared copy so allocation failure never destroys the live cache.
            auto prepared = m_assets;
            auto payload = std::make_shared<const std::vector<std::byte>>(std::move(*encoded));
            const auto added = prepared.emplace(source.key, entry{{}, payload}).first;
            scene_config config;
            config.workers = 1;
            auto cooker = PhysicsScene::create(config);
            if (!cooker)
                return std::unexpected(cooker.error());

            auto cooked = [&] {
                ce::profile_scope cooking{ce::marker<"Physics.GeometryCook">()};
                ++m_stats.cooks;
                return std::visit(
                    [&](const auto& input) -> result<std::shared_ptr<const CollisionGeometry>> {
                        using T = std::remove_cvref_t<decltype(input)>;
                        if constexpr (std::same_as<T, convex_source>)
                            return (*cooker)->cook_convex(input.points);
                        else if constexpr (std::same_as<T, triangle_mesh_source>)
                            return (*cooker)->cook_triangle_mesh(input.points, input.triangles);
                        else
                            return (*cooker)->cook_heightfield({input.rows, input.columns, input.heights});
                    },
                    source.form);
            }();

            if (!cooked)
            {
                ++m_stats.failures;
                return std::unexpected(cooked.error());
            }

            added->second.asset = *cooked;
            if (!std::invoke(std::forward<Publisher>(publisher), std::span<const std::byte>(*payload)))
            {
                ++m_stats.failures;
                return std::unexpected(
                    error{error_code::invalid_argument, 0, "Collision geometry host publication failed"});
            }

            m_assets.swap(prepared);
            m_stats.assets = m_assets.size();
            return *cooked;
        }
        catch (const std::bad_alloc&)
        {
            ++m_stats.failures;
            return std::unexpected(error{error_code::out_of_memory, 0, "Collision geometry cache allocation failed"});
        }
        catch (...)
        {
            ++m_stats.failures;
            return std::unexpected(
                error{error_code::invalid_argument, 0, "Collision geometry host publication raised an exception"});
        }
    }

    result<statistics> Stats() const
    {
        if (auto owner = RequireOwner(); !owner)
            return std::unexpected(owner.error());
        return m_stats;
    }

  private:
    struct entry
    {
        std::shared_ptr<const CollisionGeometry> asset;
        std::shared_ptr<const std::vector<std::byte>> source;
    };

    result<void> RequireOwner() const
    {
        if (m_owner != std::this_thread::get_id())
            return std::unexpected(error{error_code::wrong_phase, 0, "Collision geometry requires its owner thread"});
        return {};
    }

    std::thread::id m_owner = std::this_thread::get_id();
    std::flat_map<geometry_asset_key, entry> m_assets;
    statistics m_stats;
    bool m_publishing = false;
};
} // namespace ce::physics
