#include "MeshLodBuilder.h"

#include "MeshletBuilder.h"
#include "Sha256.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>

namespace experiment::importer
{
    namespace
    {
        static_assert(MESHOPTIMIZER_VERSION == kMeshletMeshoptimizerVersion,
            "Version the LOD builder when upgrading meshoptimizer.");
        static_assert(std::endian::native == std::endian::little);
        static_assert(sizeof(unsigned int) == sizeof(std::uint32_t));

        using MeshLodPosition = std::array<float, 3>;
        static_assert(sizeof(MeshLodPosition) == 12);

        bool MeshLodFail(std::string& diagnostic, const char* message)
        {
            diagnostic = message;
            return false;
        }

        bool MeshLodSettingsSupported(const MeshLodBuildSettings& settings)
        {
            return settings.builderVersion == kMeshLodBuilderVersion
                && settings.meshoptimizerVersion == kMeshletMeshoptimizerVersion
                && settings.levelCount <= kMeshLodMaxLevels
                && std::isfinite(settings.reductionRatio) && settings.reductionRatio > 0.0f
                && settings.reductionRatio < 1.0f
                && std::isfinite(settings.targetRelativeError) && settings.targetRelativeError >= 0.0f
                && settings.targetRelativeError <= 1.0f && settings.flags == kMeshLodLockBorder;
        }

        bool MeshLodHasSkinning(const Mesh& mesh)
        {
            return Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneIndices)
                || Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneWeights);
        }

        bool MeshLodBaseValid(const Mesh& mesh, std::string& diagnostic)
        {
            const auto& vertices = mesh.vertices;
            const auto mask = vertices.AttributeMask();
            constexpr auto limit = (std::numeric_limits<std::uint32_t>::max)();
            if (!VertexBuffer::IsSupportedLayout(mask) || vertices.Stride() != StrideOf(mask)
                || !vertices.Stride() || vertices.ByteSize() % vertices.Stride() != 0
                || vertices.size() > limit || mesh.indices.size() > limit || mesh.indices.size() % 3 != 0)
            {
                return MeshLodFail(diagnostic, "LOD base geometry has an invalid layout or triangle range.");
            }
            for (const auto index : mesh.indices)
            {
                if (index >= vertices.size())
                {
                    return MeshLodFail(diagnostic, "LOD base index is outside the finalized vertex array.");
                }
            }
            // Validate every stored floating stream, including unused vertices.
            for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
            {
                for (const auto& attribute : kVertexAttributeTable)
                {
                    if (!Has(mask, attribute.attribute) || attribute.format == VertexFormat::RGBA8Uint)
                    {
                        continue;
                    }
                    const auto begin = vertex * vertices.Stride() + OffsetOf(mask, attribute.attribute);
                    for (std::size_t component = 0; component < SizeOf(attribute.format); component += sizeof(float))
                    {
                        float value{};
                        std::memcpy(&value, vertices.Bytes().data() + begin + component, sizeof(value));
                        if (!std::isfinite(value))
                        {
                            return MeshLodFail(diagnostic, "LOD base geometry contains a non-finite vertex attribute.");
                        }
                    }
                }
            }
            return true;
        }

        void MeshLodHashU32(Hash::Sha256& hash, std::uint32_t value) noexcept
        {
            hash.Update(&value, sizeof(value));
        }

        template <class T>
        void MeshLodHashVector(Hash::Sha256& hash, const std::vector<T>& values) noexcept
        {
            const auto count = static_cast<std::uint64_t>(values.size());
            hash.Update(&count, sizeof(count));
            if (!values.empty())
            {
                hash.Update(values.data(), values.size() * sizeof(T));
            }
        }

        void MeshLodHashMeshletSettings(Hash::Sha256& hash, const MeshletBuildSettings& settings) noexcept
        {
            MeshLodHashU32(hash, settings.profileVersion);
            MeshLodHashU32(hash, settings.builderVersion);
            MeshLodHashU32(hash, settings.meshoptimizerVersion);
            MeshLodHashU32(hash, settings.maxVertices);
            MeshLodHashU32(hash, settings.maxTriangles);
            MeshLodHashU32(hash, std::bit_cast<std::uint32_t>(settings.coneWeight));
        }

        bool MeshLodValidateChain(const Mesh& mesh, const MeshLodChain& lods, std::string& diagnostic)
        {
            diagnostic.clear();
            if (lods.IsEmpty())
            {
                return true;
            }
            if (!MeshLodSettingsSupported(lods.settings) || lods.levels.empty()
                || lods.levels.size() > lods.settings.levelCount || !MeshLodBaseValid(mesh, diagnostic))
            {
                if (!diagnostic.empty())
                {
                    return false;
                }
                return MeshLodFail(diagnostic, "LOD chain has unsupported settings or level count.");
            }
            if (MeshLodHasSkinning(mesh))
            {
                return MeshLodFail(diagnostic, "Coarse LODs require static geometry; posed simplification error is unavailable.");
            }
            if (lods.geometryDigest != ComputeMeshLodGeometryDigest(mesh, lods))
            {
                return MeshLodFail(diagnostic, "LOD chain digest does not match finalized geometry/settings/payload.");
            }

            // Build a view-compatible validation mesh without copying the chain
            // recursively. The base geometry remains owned and unchanged.
            Mesh levelMesh;
            levelMesh.vertices = mesh.vertices;
            levelMesh.material = mesh.material;
            levelMesh.bounds = mesh.bounds;
            // Raster LOD bounds are derived from the validated base meshlet
            // union. Unreferenced packed vertices are outside that contract,
            // even if a cooked coarse payload has a self-consistent digest.
            std::vector<std::uint8_t> baseVertices(mesh.vertices.size(), 0u);
            for (const auto index : mesh.indices)
            {
                baseVertices[index] = 1u;
            }
            std::size_t previousCount = mesh.indices.size();
            float previousError = 0.0f;
            for (const auto& level : lods.levels)
            {
                if (level.indices.empty() || level.indices.size() % 3 != 0 || level.indices.size() >= previousCount
                    || !std::isfinite(level.geometricError) || level.geometricError < previousError
                    || !level.meshlets.HasMeshlets())
                {
                    return MeshLodFail(diagnostic, "LOD levels require decreasing triangle counts, monotonic finite error, and meshlets.");
                }
                for (const auto index : level.indices)
                {
                    if (index >= baseVertices.size() || baseVertices[index] == 0u)
                    {
                        return MeshLodFail(diagnostic, "LOD topology references a vertex absent from original indexed geometry.");
                    }
                }
                levelMesh.indices = level.indices;
                if (!ValidateMeshlets(levelMesh, level.meshlets, diagnostic, level.meshlets.settings))
                {
                    return false;
                }
                previousCount = level.indices.size();
                previousError = level.geometricError;
            }
            return true;
        }

        bool MeshLodBuildChain(const Mesh& mesh, MeshLodChain& result, std::string& diagnostic,
            const MeshLodBuildSettings& settings, const MeshletBuildSettings& meshletSettings)
        {
            if (!MeshLodSettingsSupported(settings))
            {
                return MeshLodFail(diagnostic, "LOD builder settings are unsupported.");
            }
            if (settings.levelCount == 0)
            {
                diagnostic = "LOD generation is disabled; original geometry retained.";
                return true;
            }
            if (!MeshLodBaseValid(mesh, diagnostic))
            {
                return false;
            }
            if (MeshLodHasSkinning(mesh))
            {
                diagnostic = "Coarse LOD generation skipped for skinned geometry; posed error is not supported.";
                return true;
            }
            if (mesh.indices.size() <= 3)
            {
                diagnostic = "LOD generation produced no reduction; original geometry retained.";
                return true;
            }

            std::vector<MeshLodPosition> positions(mesh.vertices.size());
            const auto positionOffset = OffsetOf(mesh.vertices.AttributeMask(), VertexAttribute::Position);
            for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
            {
                std::memcpy(positions[vertex].data(), mesh.vertices.Bytes().data()
                    + vertex * mesh.vertices.Stride() + positionOffset, sizeof(MeshLodPosition));
            }
            const float scale = meshopt_simplifyScale(positions.front().data(), positions.size(), sizeof(MeshLodPosition));
            if (!std::isfinite(scale))
            {
                return MeshLodFail(diagnostic, "LOD simplification scale is not finite.");
            }
            if (scale <= 0.0f)
            {
                diagnostic = "LOD generation skipped zero-extent geometry; original geometry retained.";
                return true;
            }

            Mesh levelMesh;
            levelMesh.vertices = mesh.vertices;
            levelMesh.material = mesh.material;
            levelMesh.bounds = mesh.bounds;
            std::size_t previousCount = mesh.indices.size();
            float previousError = 0.0f;
            result.settings = settings;
            for (std::uint32_t levelIndex = 0; levelIndex < settings.levelCount; ++levelIndex)
            {
                const auto targetTriangles = (std::max)(std::size_t{1}, static_cast<std::size_t>(std::floor(
                    double(mesh.indices.size() / 3u) * std::pow(double(settings.reductionRatio), double(levelIndex + 1u)))));
                const auto targetCount = targetTriangles * 3u;
                if (targetCount >= previousCount)
                {
                    continue;
                }
                levelMesh.indices.resize(mesh.indices.size());
                float relativeError{};
                // Direct-from-base for every level, no accumulated simplification
                // drift. Border locking prevents cracks between material meshes;
                // the standard simplifier also preserves topology/attribute seams.
                const auto count = meshopt_simplify(levelMesh.indices.data(), mesh.indices.data(), mesh.indices.size(),
                    positions.front().data(), positions.size(), sizeof(MeshLodPosition), targetCount,
                    settings.targetRelativeError, meshopt_SimplifyLockBorder, &relativeError);
                if (!count || count % 3u != 0u || count > mesh.indices.size()
                    || !std::isfinite(relativeError) || relativeError < 0.0f)
                {
                    return MeshLodFail(diagnostic, "LOD simplifier returned invalid topology or error.");
                }
                if (count >= previousCount)
                {
                    continue; // Error/border constraints prevented a useful level.
                }
                levelMesh.indices.resize(count);
                const double measuredError = (std::max)(double(previousError), double(relativeError) * double(scale));
                float absoluteError = static_cast<float>(measuredError);
                if (measuredError > 0.0)
                {
                    absoluteError = std::nextafter(absoluteError, (std::numeric_limits<float>::infinity)());
                }
                if (!std::isfinite(absoluteError))
                {
                    return MeshLodFail(diagnostic, "LOD absolute simplification error cannot be represented.");
                }

                MeshLodLevel level;
                level.geometricError = absoluteError;
                if (!BuildMeshlets(levelMesh, level.meshlets, diagnostic, meshletSettings))
                {
                    return false;
                }
                level.indices = std::move(levelMesh.indices);
                result.levels.push_back(std::move(level));
                previousCount = count;
                previousError = absoluteError;
            }
            if (result.levels.empty())
            {
                result = {};
                diagnostic = "LOD generation produced no reduction within the border/error constraints; original geometry retained.";
                return true;
            }
            result.geometryDigest = ComputeMeshLodGeometryDigest(mesh, result);
            return MeshLodValidateChain(mesh, result, diagnostic);
        }
    }

    MeshletGeometryDigest ComputeMeshLodGeometryDigest(const Mesh& mesh, const MeshLodChain& lods) noexcept
    {
        Hash::Sha256 hash;
        constexpr char domain[] = "CreatorEngine.StaticMeshLod.v1";
        hash.Update(domain, sizeof(domain) - 1u);
        MeshLodHashU32(hash, mesh.vertices.AttributeMask());
        MeshLodHashU32(hash, mesh.vertices.Stride());
        const auto vertexBytes = static_cast<std::uint64_t>(mesh.vertices.ByteSize());
        hash.Update(&vertexBytes, sizeof(vertexBytes));
        if (vertexBytes)
        {
            hash.Update(mesh.vertices.Bytes().data(), mesh.vertices.ByteSize());
        }
        MeshLodHashVector(hash, mesh.indices);
        MeshLodHashU32(hash, lods.settings.builderVersion);
        MeshLodHashU32(hash, lods.settings.meshoptimizerVersion);
        MeshLodHashU32(hash, lods.settings.levelCount);
        MeshLodHashU32(hash, std::bit_cast<std::uint32_t>(lods.settings.reductionRatio));
        MeshLodHashU32(hash, std::bit_cast<std::uint32_t>(lods.settings.targetRelativeError));
        MeshLodHashU32(hash, lods.settings.flags);
        MeshLodHashU32(hash, static_cast<std::uint32_t>(lods.levels.size()));
        for (const auto& level : lods.levels)
        {
            MeshLodHashU32(hash, std::bit_cast<std::uint32_t>(level.geometricError));
            MeshLodHashVector(hash, level.indices);
            const auto& meshlets = level.meshlets;
            MeshLodHashMeshletSettings(hash, meshlets.settings);
            hash.Update(meshlets.geometryDigest.data(), meshlets.geometryDigest.size());
            MeshLodHashVector(hash, meshlets.descriptors);
            MeshLodHashVector(hash, meshlets.vertexRemap);
            MeshLodHashVector(hash, meshlets.triangleIndices);
            MeshLodHashVector(hash, meshlets.primitiveRemap);
            MeshLodHashU32(hash, meshlets.lod0.firstMeshlet);
            MeshLodHashU32(hash, meshlets.lod0.meshletCount);
        }
        return hash.Finish();
    }

    bool BuildMeshLods(const Mesh& mesh, MeshLodChain& outLods, std::string& diagnostic,
        const MeshLodBuildSettings& settings, const MeshletBuildSettings& meshletSettings)
    {
        // Rebuild callers may pass settings owned by the output being replaced.
        const auto buildSettings = settings;
        const auto buildMeshletSettings = meshletSettings;
        outLods = {};
        diagnostic.clear();
        try
        {
            MeshLodChain candidate;
            if (!MeshLodBuildChain(mesh, candidate, diagnostic, buildSettings, buildMeshletSettings))
            {
                return false;
            }
            outLods = std::move(candidate);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return MeshLodFail(diagnostic, "LOD authoring allocation failed; original geometry retained.");
        }
        catch (const std::length_error&)
        {
            return MeshLodFail(diagnostic, "LOD authoring allocation exceeded the addressable range.");
        }
    }

    bool ValidateMeshLods(const Mesh& mesh, const MeshLodChain& lods, std::string& diagnostic)
    {
        try
        {
            return MeshLodValidateChain(mesh, lods, diagnostic);
        }
        catch (const std::bad_alloc&)
        {
            return MeshLodFail(diagnostic, "LOD validation allocation failed.");
        }
        catch (const std::length_error&)
        {
            return MeshLodFail(diagnostic, "LOD validation allocation exceeded the addressable range.");
        }
    }
}
