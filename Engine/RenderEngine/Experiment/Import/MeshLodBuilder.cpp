#include "MeshLodBuilder.h"

#include "MeshletBuilder.h"
#if defined(CE_CONTRACT_STANDALONE)
#include "../../../../Tools/build/ModuleAdapters/Creator.Core.Sha256.h"
#else
import Creator.Core.Sha256;
#endif
#include "meshoptimizer.h"

#include <stdint.h>
#include "../../../Utility_Framework/Foundation/StandardLibrary.h"

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
    return settings.builderVersion == kMeshLodBuilderVersion &&
           settings.meshoptimizerVersion == kMeshletMeshoptimizerVersion && settings.levelCount <= kMeshLodMaxLevels &&
           std::isfinite(settings.reductionRatio) && settings.reductionRatio > 0.0f && settings.reductionRatio < 1.0f &&
           std::isfinite(settings.targetRelativeError) && settings.targetRelativeError >= 0.0f &&
           settings.targetRelativeError <= 1.0f && settings.flags == kMeshLodLockBorder;
}

bool MeshLodHasSkinning(const Mesh& mesh)
{
    return Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneIndices) ||
           Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneWeights);
}

bool MeshLodBaseValid(const Mesh& mesh, std::string& diagnostic)
{
    const auto& vertices = mesh.vertices;
    const auto mask = vertices.AttributeMask();
    constexpr auto limit = (std::numeric_limits<std::uint32_t>::max)();
    if (!VertexBuffer::IsSupportedLayout(mask) || vertices.Stride() != StrideOf(mask) || !vertices.Stride() ||
        vertices.ByteSize() % vertices.Stride() != 0 || vertices.size() > limit || mesh.indices.size() > limit ||
        mesh.indices.size() % 3 != 0)
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

void MeshLodHashU32(ce::hash::sha256& hash, std::uint32_t value) noexcept
{
    hash.update(&value, sizeof(value));
}

template<class T>
void MeshLodHashVector(ce::hash::sha256& hash, const std::vector<T>& values) noexcept
{
    const auto count = static_cast<std::uint64_t>(values.size());
    hash.update(&count, sizeof(count));
    if (!values.empty())
    {
        hash.update(values.data(), values.size() * sizeof(T));
    }
}

void MeshLodHashMeshletSettings(ce::hash::sha256& hash, const MeshletBuildSettings& settings) noexcept
{
    MeshLodHashU32(hash, settings.profileVersion);
    MeshLodHashU32(hash, settings.builderVersion);
    MeshLodHashU32(hash, settings.meshoptimizerVersion);
    MeshLodHashU32(hash, settings.maxVertices);
    MeshLodHashU32(hash, settings.maxTriangles);
    MeshLodHashU32(hash, std::bit_cast<std::uint32_t>(settings.coneWeight));
}

using Boundary = std::map<std::pair<uint32_t, uint32_t>, uint32_t>;
Boundary PatchBoundary(const std::vector<uint32_t>& indices, const std::vector<uint32_t>& positionRemap)
{
    Boundary result;
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        const auto x = positionRemap[indices[i]], y = positionRemap[indices[i + 1]], z = positionRemap[indices[i + 2]];
        if (x == y || y == z || z == x)
            continue; // Zero-area coincident wedges do not bound visible coverage.
        for (unsigned c = 0; c < 3; ++c)
        {
            const auto a = positionRemap[indices[i + c]], b = positionRemap[indices[i + (c + 1) % 3]];
            auto reverse = result.find({b, a});
            if (reverse != result.end())
            {
                if (!--reverse->second)
                    result.erase(reverse);
            }
            else
                ++result[{a, b}];
        }
    }
    return result;
}
std::vector<uint32_t> GroupIndices(const Mesh& mesh, const MeshClusterGroup& group)
{
    std::vector<uint32_t> result;
    for (uint32_t i = 0; i < group.meshletCount; ++i)
    {
        const auto& d = mesh.meshlets.descriptors[group.firstMeshlet + i];
        for (uint32_t t = 0; t < d.triangleCount; ++t)
        {
            const auto primitive = mesh.meshlets.primitiveRemap[d.primitiveOffset + t];
            result.insert(result.end(), mesh.indices.begin() + primitive * 3, mesh.indices.begin() + primitive * 3 + 3);
        }
    }
    return result;
}
void PatchSphere(const std::vector<MeshLodPosition>& positions, const std::vector<uint32_t>& indices,
                 float (&sphere)[4])
{
    double lo[3]{INFINITY, INFINITY, INFINITY}, hi[3]{-INFINITY, -INFINITY, -INFINITY};
    for (auto v : indices)
        for (unsigned c = 0; c < 3; ++c)
        {
            lo[c] = std::min(lo[c], double(positions[v][c]));
            hi[c] = std::max(hi[c], double(positions[v][c]));
        }
    for (unsigned c = 0; c < 3; ++c)
        sphere[c] = float((lo[c] + hi[c]) * .5);
    double radius{};
    for (auto v : indices)
    {
        double d{};
        for (unsigned c = 0; c < 3; ++c)
        {
            const double x = double(positions[v][c]) - sphere[c];
            d += x * x;
        }
        radius = std::max(radius, d);
    }
    sphere[3] = std::nextafter(float(std::sqrt(radius)), INFINITY);
}
void AppendMeshlets(MeshletPayload& dst, const MeshletPayload& src, uint32_t primitiveBase)
{
    for (auto d : src.descriptors)
    {
        d.vertexOffset += uint32_t(dst.vertexRemap.size());
        d.triangleOffset += uint32_t(dst.triangleIndices.size());
        d.primitiveOffset += uint32_t(dst.primitiveRemap.size());
        d.clusterId = uint32_t(dst.descriptors.size());
        dst.descriptors.push_back(d);
    }
    dst.vertexRemap.insert(dst.vertexRemap.end(), src.vertexRemap.begin(), src.vertexRemap.end());
    dst.triangleIndices.insert(dst.triangleIndices.end(), src.triangleIndices.begin(), src.triangleIndices.end());
    for (auto p : src.primitiveRemap)
        dst.primitiveRemap.push_back(primitiveBase + p);
}
bool MeshLodValidateChain(const Mesh& mesh, const MeshLodChain& lods, std::string& diagnostic)
{
    diagnostic.clear();
    if (lods.IsEmpty())
        return MeshLodSettingsSupported(lods.settings) ||
               MeshLodFail(diagnostic, "Stale LOD builder; recook required.");
    if (!MeshLodSettingsSupported(lods.settings) || lods.levels.empty() ||
        lods.levels.size() > lods.settings.levelCount || lods.groups.empty() ||
        lods.groups.size() > kMeshClusterMaxGroups || !MeshLodBaseValid(mesh, diagnostic) || MeshLodHasSkinning(mesh))
        return MeshLodFail(diagnostic, "Invalid spatial DAG settings, geometry or posed error domain.");
    if (!ValidateMeshlets(mesh, mesh.meshlets, diagnostic, mesh.meshlets.settings))
        return false;
    if (lods.geometryDigest != ComputeMeshLodGeometryDigest(mesh, lods))
        return MeshLodFail(diagnostic, "Spatial DAG digest mismatch; recook required.");
    std::vector<MeshLodPosition> positions(mesh.vertices.size());
    for (size_t v = 0; v < positions.size(); ++v)
        std::memcpy(positions[v].data(),
                    mesh.vertices.Bytes().data() + v * mesh.vertices.Stride() +
                        OffsetOf(mesh.vertices.AttributeMask(), VertexAttribute::Position),
                    12);
    std::vector<uint32_t> positionRemap(positions.size());
    meshopt_generatePositionRemap(positionRemap.data(), positions.front().data(), positions.size(),
                                  sizeof(MeshLodPosition));
    std::vector<const MeshletPayload*> payloads{&mesh.meshlets};
    std::vector<uint32_t> clusterBases{0}, primitiveBases{0};
    uint64_t clusters = mesh.meshlets.descriptors.size(), primitives = mesh.indices.size() / 3;
    Mesh levelMesh;
    levelMesh.vertices = mesh.vertices;
    for (const auto& level : lods.levels)
    {
        if (level.indices.empty() || level.indices.size() % 3 || level.indices.size() > mesh.indices.size() ||
            !std::isfinite(level.geometricError) || level.geometricError < 0)
            return MeshLodFail(diagnostic, "Invalid spatial level topology/error.");
        levelMesh.indices = level.indices;
        if (!ValidateMeshlets(levelMesh, level.meshlets, diagnostic, level.meshlets.settings))
            return false;
        if (clusters > UINT32_MAX || primitives > UINT32_MAX)
            return MeshLodFail(diagnostic, "Spatial DAG addressing overflow.");
        clusterBases.push_back(uint32_t(clusters));
        primitiveBases.push_back(uint32_t(primitives));
        payloads.push_back(&level.meshlets);
        clusters += level.meshlets.descriptors.size();
        primitives += level.indices.size() / 3;
    }
    if (clusters > UINT32_MAX || primitives > UINT32_MAX)
        return MeshLodFail(diagnostic, "Spatial DAG addressing overflow.");
    std::vector<uint8_t> covered(size_t(clusters), 0), parents(lods.groups.size(), 0);
    std::vector<std::vector<uint32_t>> originals(lods.groups.size());
    std::vector<uint32_t> depths(lods.groups.size(), 0);
    for (uint32_t i = 0; i < lods.groups.size(); ++i)
    {
        const auto& g = lods.groups[i];
        if (g.lodIndex >= payloads.size() || !g.meshletCount || g.firstMeshlet < clusterBases[g.lodIndex] ||
            uint64_t(g.firstMeshlet) + g.meshletCount >
                uint64_t(clusterBases[g.lodIndex]) + payloads[g.lodIndex]->descriptors.size() ||
            g.levelFirstMeshlet != clusterBases[g.lodIndex] || g.primitiveBase != primitiveBases[g.lodIndex] ||
            !std::isfinite(g.geometricError) || g.geometricError < 0 || !std::isfinite(g.sphere[3]) ||
            g.sphere[3] < 0 || g.reserved[0] || g.reserved[1] || g.reserved[2])
            return MeshLodFail(diagnostic, "Invalid spatial DAG ranges/extent/error.");
        for (uint32_t m = 0; m < g.meshletCount; ++m)
            if (covered[g.firstMeshlet + m]++)
                return MeshLodFail(diagnostic, "Overlapping DAG representations.");
        if (g.childGroup == UINT32_MAX)
        {
            if (g.secondChild != UINT32_MAX || g.lodIndex || g.geometricError != 0)
                return MeshLodFail(diagnostic, "Invalid DAG leaf.");
            originals[i] = GroupIndices(mesh, g);
        }
        else
        {
            if (g.childGroup >= i || g.secondChild >= i || g.childGroup == g.secondChild)
                return MeshLodFail(diagnostic, "Cyclic/invalid DAG edge.");
            for (auto child : {g.childGroup, g.secondChild})
            {
                if (++parents[child] != 1 || lods.groups[child].parentGroup != i ||
                    g.geometricError < lods.groups[child].geometricError)
                    return MeshLodFail(diagnostic, "Invalid DAG ownership/monotonic error.");
                originals[i].insert(originals[i].end(), originals[child].begin(), originals[child].end());
                depths[i] = std::max(depths[i], depths[child] + 1);
            }
            if (g.lodIndex != depths[i] || depths[i] > lods.settings.levelCount)
                return MeshLodFail(diagnostic, "Invalid DAG depth.");
            Mesh patch;
            patch.indices = lods.levels[g.lodIndex - 1].indices;
            patch.meshlets = *payloads[g.lodIndex];
            auto local = g;
            local.firstMeshlet -= clusterBases[g.lodIndex];
            const auto coarse = GroupIndices(patch, local);
            if (PatchBoundary(coarse, positionRemap) != PatchBoundary(originals[i], positionRemap))
                return MeshLodFail(diagnostic, "DAG replacement changes its original patch boundary.");
            std::vector<uint8_t> allowed(mesh.vertices.size(), 0);
            for (auto v : originals[i])
                allowed[v] = 1;
            for (auto v : coarse)
                if (v >= allowed.size() || !allowed[v])
                    return MeshLodFail(diagnostic, "DAG replacement escapes its source patch.");
        }
        for (unsigned c = 0; c < 3; ++c)
            if (!std::isfinite(g.sphere[c]))
                return MeshLodFail(diagnostic, "Nonfinite DAG bound.");
        for (auto v : originals[i])
        {
            float p[3];
            std::memcpy(p,
                        mesh.vertices.Bytes().data() + size_t(v) * mesh.vertices.Stride() +
                            OffsetOf(mesh.vertices.AttributeMask(), VertexAttribute::Position),
                        12);
            double d{};
            for (unsigned c = 0; c < 3; ++c)
            {
                const double x = double(p[c]) - g.sphere[c];
                d += x * x;
            }
            if (d > double(g.sphere[3]) * g.sphere[3])
                return MeshLodFail(diagnostic, "DAG bound excludes original geometry.");
        }
    }
    if (std::ranges::find(covered, 0) != covered.end())
        return MeshLodFail(diagnostic, "DAG omits cooked clusters.");
    for (uint32_t i = 0; i < lods.groups.size(); ++i)
        if (i + 1 == lods.groups.size() ? (parents[i] || lods.groups[i].parentGroup != UINT32_MAX) : parents[i] != 1)
            return MeshLodFail(diagnostic, "DAG has an orphan or multiple roots.");
    return true;
}
bool MeshLodBuildChain(const Mesh& mesh, MeshLodChain& result, std::string& diagnostic,
                       const MeshLodBuildSettings& settings, const MeshletBuildSettings& meshletSettings)
{
    if (!MeshLodSettingsSupported(settings))
        return MeshLodFail(diagnostic, "LOD builder settings unsupported.");
    result.settings = settings;
    if (!settings.levelCount || MeshLodHasSkinning(mesh))
    {
        diagnostic = "Spatial LOD disabled or deformation error unavailable; original level retained.";
        return true;
    }
    if (!MeshLodBaseValid(mesh, diagnostic) || !ValidateMeshlets(mesh, mesh.meshlets, diagnostic, meshletSettings))
        return false;
    if (mesh.meshlets.descriptors.size() < 2)
    {
        diagnostic = "Single-cluster geometry has no spatial replacement hierarchy.";
        return true;
    }
    std::vector<MeshLodPosition> positions(mesh.vertices.size());
    for (size_t v = 0; v < positions.size(); ++v)
        std::memcpy(positions[v].data(),
                    mesh.vertices.Bytes().data() + v * mesh.vertices.Stride() +
                        OffsetOf(mesh.vertices.AttributeMask(), VertexAttribute::Position),
                    12);
    std::vector<uint32_t> positionRemap(positions.size());
    meshopt_generatePositionRemap(positionRemap.data(), positions.front().data(), positions.size(),
                                  sizeof(MeshLodPosition));
    std::vector<std::vector<uint32_t>> originals;
    std::vector<uint32_t> leaves;
    const uint32_t leafWidth =
        uint32_t((mesh.meshlets.descriptors.size() + (1u << settings.levelCount) - 1) / (1u << settings.levelCount));
    for (uint32_t first = 0; first < mesh.meshlets.descriptors.size(); first += leafWidth)
    {
        MeshClusterGroup g;
        g.firstMeshlet = first;
        g.meshletCount = std::min(leafWidth, uint32_t(mesh.meshlets.descriptors.size()) - first);
        auto indices = GroupIndices(mesh, g);
        PatchSphere(positions, indices, g.sphere);
        leaves.push_back(uint32_t(result.groups.size()));
        result.groups.push_back(g);
        originals.push_back(std::move(indices));
    }
    std::function<uint32_t(std::vector<uint32_t>)> build = [&](std::vector<uint32_t> nodes) -> uint32_t {
        if (nodes.size() == 1)
            return nodes[0];
        float lo[3]{INFINITY, INFINITY, INFINITY}, hi[3]{-INFINITY, -INFINITY, -INFINITY};
        for (auto n : nodes)
            for (unsigned c = 0; c < 3; ++c)
            {
                lo[c] = std::min(lo[c], result.groups[n].sphere[c]);
                hi[c] = std::max(hi[c], result.groups[n].sphere[c]);
            }
        unsigned axis = 0;
        for (unsigned c = 1; c < 3; ++c)
            if (hi[c] - lo[c] > hi[axis] - lo[axis])
                axis = c;
        std::stable_sort(nodes.begin(), nodes.end(),
                         [&](auto a, auto b) { return result.groups[a].sphere[axis] < result.groups[b].sphere[axis]; });
        const auto middle = nodes.begin() + nodes.size() / 2;
        const auto left = build(std::vector<uint32_t>(nodes.begin(), middle));
        const auto right = build(std::vector<uint32_t>(middle, nodes.end()));
        MeshClusterGroup g;
        g.childGroup = left;
        g.secondChild = right;
        g.lodIndex = std::max(result.groups[left].lodIndex, result.groups[right].lodIndex) + 1;
        std::vector<uint32_t> indices = originals[left];
        indices.insert(indices.end(), originals[right].begin(), originals[right].end());
        PatchSphere(positions, indices, g.sphere);
        Mesh patch;
        patch.vertices = mesh.vertices;
        patch.indices.resize(indices.size());
        const float scale = meshopt_simplifyScale(positions.front().data(), positions.size(), sizeof(MeshLodPosition));
        float relative{};
        const auto target = std::max(
            size_t(3), size_t(double(indices.size() / 3) * std::pow(double(settings.reductionRatio), g.lodIndex)) * 3);
        // Lock every wedge at an original geometric boundary position. Border
        // classification alone can remap coincident UV/normal seam indices.
        const auto boundary = PatchBoundary(indices, positionRemap);
        std::vector<unsigned char> locked(positions.size(), 0);
        for (const auto& edge : boundary)
        {
            locked[edge.first.first] = meshopt_SimplifyVertex_Lock;
            locked[edge.first.second] = meshopt_SimplifyVertex_Lock;
        }
        for (size_t v = 0; v < locked.size(); ++v)
            locked[v] = locked[positionRemap[v]];
        const auto count = meshopt_simplifyWithAttributes(
            patch.indices.data(), indices.data(), indices.size(), positions.front().data(), positions.size(),
            sizeof(MeshLodPosition), nullptr, 0, nullptr, 0, locked.data(), target, settings.targetRelativeError,
            meshopt_SimplifyLockBorder, &relative);
        if (!count || count % 3 || count > indices.size() || !std::isfinite(relative) || relative < 0)
            throw std::runtime_error("Spatial patch simplification failed.");
        patch.indices.resize(count);
        if (PatchBoundary(patch.indices, positionRemap) != boundary)
            throw std::runtime_error("Spatial simplification changed a locked patch boundary.");
        g.geometricError = std::max({result.groups[left].geometricError, result.groups[right].geometricError,
                                     std::nextafter(relative * scale, INFINITY)});
        MeshletPayload payload;
        if (!BuildMeshlets(patch, payload, diagnostic, meshletSettings))
            throw std::runtime_error(diagnostic);
        if (result.levels.size() < g.lodIndex)
            result.levels.resize(g.lodIndex);
        auto& level = result.levels[g.lodIndex - 1];
        g.firstMeshlet = uint32_t(level.meshlets.descriptors.size());
        g.meshletCount = uint32_t(payload.descriptors.size());
        AppendMeshlets(level.meshlets, payload, uint32_t(level.indices.size() / 3));
        level.indices.insert(level.indices.end(), patch.indices.begin(), patch.indices.end());
        level.geometricError = std::max(level.geometricError, g.geometricError);
        const auto id = uint32_t(result.groups.size());
        result.groups[left].parentGroup = id;
        result.groups[right].parentGroup = id;
        result.groups.push_back(g);
        originals.push_back(std::move(indices));
        return id;
    };
    build(leaves);
    uint32_t first = uint32_t(mesh.meshlets.descriptors.size()), primitiveBase = uint32_t(mesh.indices.size() / 3);
    for (uint32_t level = 1; level <= result.levels.size(); ++level)
    {
        auto& l = result.levels[level - 1];
        Mesh m;
        m.vertices = mesh.vertices;
        m.indices = l.indices;
        l.meshlets.settings = meshletSettings;
        l.meshlets.geometryDigest = ComputeMeshletGeometryDigest(m, meshletSettings);
        l.meshlets.lod0 = {0, uint32_t(l.meshlets.descriptors.size())};
        for (auto& g : result.groups)
            if (g.lodIndex == level)
            {
                g.firstMeshlet += first;
                g.levelFirstMeshlet = first;
                g.primitiveBase = primitiveBase;
            }
        first += uint32_t(l.meshlets.descriptors.size());
        primitiveBase += uint32_t(l.indices.size() / 3);
    }
    result.geometryDigest = ComputeMeshLodGeometryDigest(mesh, result);
    return MeshLodValidateChain(mesh, result, diagnostic);
}

} // namespace

MeshletGeometryDigest ComputeMeshLodGeometryDigest(const Mesh& mesh, const MeshLodChain& lods) noexcept
{
    ce::hash::sha256 hash;
    constexpr char domain[] = "CreatorEngine.SpatialClusterDag.v3";
    hash.update(domain, sizeof(domain) - 1u);
    MeshLodHashU32(hash, mesh.vertices.AttributeMask());
    MeshLodHashU32(hash, mesh.vertices.Stride());
    const auto vertexBytes = static_cast<std::uint64_t>(mesh.vertices.ByteSize());
    hash.update(&vertexBytes, sizeof(vertexBytes));
    if (vertexBytes)
    {
        hash.update(mesh.vertices.Bytes().data(), mesh.vertices.ByteSize());
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
        hash.update(meshlets.geometryDigest.data(), meshlets.geometryDigest.size());
        MeshLodHashVector(hash, meshlets.descriptors);
        MeshLodHashVector(hash, meshlets.vertexRemap);
        MeshLodHashVector(hash, meshlets.triangleIndices);
        MeshLodHashVector(hash, meshlets.primitiveRemap);
        MeshLodHashU32(hash, meshlets.lod0.firstMeshlet);
        MeshLodHashU32(hash, meshlets.lod0.meshletCount);
    }
    MeshLodHashVector(hash, lods.groups);
    return hash.finish();
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
    catch (const std::runtime_error& e)
    {
        return MeshLodFail(diagnostic, e.what());
    }
    catch (const std::bad_alloc&)
    {
        return MeshLodFail(diagnostic, "LOD authoring allocation failed.");
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
} // namespace experiment::importer
