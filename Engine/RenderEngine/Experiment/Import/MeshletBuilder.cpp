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
            "Update the meshlet builder profile when upgrading meshoptimizer.");
        static_assert(std::endian::native == std::endian::little,
            "The packed model vertex-byte contract is little-endian.");
        static_assert(sizeof(unsigned int) == sizeof(std::uint32_t));

        using MeshletPosition = std::array<float, 3>;
        using MeshletTriangle = std::array<std::uint32_t, 3>;
        static_assert(sizeof(MeshletPosition) == 12);

        [[nodiscard]] bool MeshletFail(std::string& diagnostic, const char* message)
        {
            diagnostic = message;
            return false;
        }

        [[nodiscard]] bool MeshletSettingsSupported(const MeshletBuildSettings& settings)
        {
            return settings.profileVersion == kMeshletProfileVersion
                && settings.builderVersion == kMeshletBuilderVersion
                && settings.meshoptimizerVersion == kMeshletMeshoptimizerVersion
                && settings.maxVertices >= 3 && settings.maxVertices <= kMeshletMaxVertices
                && settings.maxTriangles >= 1 && settings.maxTriangles <= kMeshletMaxTriangles
                && std::isfinite(settings.coneWeight)
                && settings.coneWeight >= 0.0f && settings.coneWeight <= 1.0f;
        }

        [[nodiscard]] bool MeshletSettingsEqual(const MeshletBuildSettings& a, const MeshletBuildSettings& b)
        {
            return a.profileVersion == b.profileVersion && a.builderVersion == b.builderVersion
                && a.meshoptimizerVersion == b.meshoptimizerVersion && a.maxVertices == b.maxVertices
                && a.maxTriangles == b.maxTriangles
                && std::bit_cast<std::uint32_t>(a.coneWeight) == std::bit_cast<std::uint32_t>(b.coneWeight);
        }

        [[nodiscard]] bool MeshletRangeFits(std::size_t offset, std::size_t count, std::size_t size)
        {
            return offset <= size && count <= size - offset;
        }

        [[nodiscard]] MeshletPosition MeshletReadPosition(const Mesh& mesh, std::size_t index)
        {
            MeshletPosition position{};
            const std::size_t offset = index * mesh.vertices.Stride()
                + OffsetOf(mesh.vertices.AttributeMask(), VertexAttribute::Position);
            std::memcpy(position.data(), mesh.vertices.Bytes().data() + offset, sizeof(position));
            return position;
        }

        [[nodiscard]] bool MeshletGeometryValid(const Mesh& mesh, std::string& diagnostic)
        {
            const auto& vertices = mesh.vertices;
            const auto attributes = vertices.AttributeMask();
            const std::size_t stride = vertices.Stride();
            const std::size_t limit = (std::numeric_limits<std::uint32_t>::max)();
            if (!VertexBuffer::IsSupportedLayout(attributes) || stride != StrideOf(attributes)
                || stride == 0 || vertices.ByteSize() % stride != 0)
            {
                return MeshletFail(diagnostic, "Meshlet input has an invalid packed vertex layout.");
            }
            if (vertices.size() > limit || mesh.indices.size() > limit || mesh.indices.size() % 3 != 0)
            {
                return MeshletFail(diagnostic, "Meshlet input exceeds 32-bit ranges or is not a triangle list.");
            }
            for (const std::uint32_t index : mesh.indices)
            {
                if (index >= vertices.size())
                {
                    return MeshletFail(diagnostic, "Meshlet input index is outside the finalized vertex buffer.");
                }
            }

            // Inspect all float streams, including optional UV/color/skin data.
            // VertexBuffer is byte storage; memcpy avoids alignment and aliasing assumptions.
            for (std::size_t vertex = 0; vertex < vertices.size(); ++vertex)
            {
                for (const auto& attribute : kVertexAttributeTable)
                {
                    if (!Has(attributes, attribute.attribute) || attribute.format == VertexFormat::RGBA8Uint)
                    {
                        continue;
                    }
                    const std::size_t begin = vertex * stride + OffsetOf(attributes, attribute.attribute);
                    for (std::size_t component = 0; component < SizeOf(attribute.format); component += sizeof(float))
                    {
                        float value{};
                        std::memcpy(&value, vertices.Bytes().data() + begin + component, sizeof(value));
                        if (!std::isfinite(value))
                        {
                            return MeshletFail(diagnostic, "Meshlet input contains a non-finite vertex attribute.");
                        }
                    }
                }
            }
            return true;
        }

        // Rotations preserve winding. Reverse-wound triangles remain distinct.
        [[nodiscard]] MeshletTriangle MeshletOrientedTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
        {
            const MeshletTriangle first{ a, b, c };
            const MeshletTriangle second{ b, c, a };
            const MeshletTriangle third{ c, a, b };
            return (std::min)(first, (std::min)(second, third));
        }

        void MeshletHashU32(Hash::Sha256& hash, std::uint32_t value) noexcept
        {
            const std::uint8_t bytes[4]{ static_cast<std::uint8_t>(value),
                static_cast<std::uint8_t>(value >> 8), static_cast<std::uint8_t>(value >> 16),
                static_cast<std::uint8_t>(value >> 24) };
            hash.Update(bytes, sizeof(bytes));
        }

        void MeshletHashU64(Hash::Sha256& hash, std::uint64_t value) noexcept
        {
            MeshletHashU32(hash, static_cast<std::uint32_t>(value));
            MeshletHashU32(hash, static_cast<std::uint32_t>(value >> 32));
        }

        [[nodiscard]] bool MeshletFinite3(const float* value)
        {
            return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
        }

        [[nodiscard]] double MeshletDistanceSquared(const float* a, const float* b)
        {
            const double x = static_cast<double>(a[0]) - b[0];
            const double y = static_cast<double>(a[1]) - b[1];
            const double z = static_cast<double>(a[2]) - b[2];
            return x * x + y * y + z * z;
        }

        // Returns the cutoff required by the actual stored axis, and verifies
        // that the apex lies behind every non-degenerate triangle plane. Double
        // arithmetic covers finite float positions without cross-product overflow.
        [[nodiscard]] bool MeshletConeRequirement(const Mesh& mesh, const MeshletPayload& payload,
            const MeshletDescriptor& descriptor, double& requiredCutoff)
        {
            const double axisLengthSquared = static_cast<double>(descriptor.coneAxis[0]) * descriptor.coneAxis[0]
                + static_cast<double>(descriptor.coneAxis[1]) * descriptor.coneAxis[1]
                + static_cast<double>(descriptor.coneAxis[2]) * descriptor.coneAxis[2];
            if (std::abs(axisLengthSquared - 1.0) > 0.00001)
            {
                return false;
            }
            bool hasNormal = false;
            requiredCutoff = 0.0;
            for (std::size_t triangle = 0; triangle < descriptor.triangleCount; ++triangle)
            {
                MeshletPosition points[3]{};
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto local = payload.triangleIndices[descriptor.triangleOffset + triangle * 3 + corner];
                    points[corner] = MeshletReadPosition(mesh, payload.vertexRemap[descriptor.vertexOffset + local]);
                }
                double u[3]{}, v[3]{};
                for (std::size_t component = 0; component < 3; ++component)
                {
                    u[component] = static_cast<double>(points[1][component]) - points[0][component];
                    v[component] = static_cast<double>(points[2][component]) - points[0][component];
                }
                const double normal[3]{ u[1] * v[2] - u[2] * v[1],
                    u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0] };
                const double lengthSquared = normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2];
                if (lengthSquared == 0.0)
                {
                    continue;
                }
                hasNormal = true;
                double projection = 0.0;
                double apexPlane = 0.0;
                for (std::size_t component = 0; component < 3; ++component)
                {
                    projection += normal[component] * descriptor.coneAxis[component];
                    apexPlane += normal[component]
                        * (static_cast<double>(descriptor.coneApex[component]) - points[0][component]);
                }
                if (projection <= 0.0 || apexPlane > 0.0)
                {
                    return false;
                }
                const double cutoffSquared = (std::max)(0.0,
                    axisLengthSquared - projection * projection / lengthSquared);
                requiredCutoff = (std::max)(requiredCutoff, std::sqrt(cutoffSquared));
            }
            return hasNormal && requiredCutoff < 1.0;
        }

        [[nodiscard]] bool MeshletBuildBounds(const Mesh& mesh, MeshletPayload& payload,
            MeshletDescriptor& descriptor, const std::vector<MeshletPosition>& positions, std::string& diagnostic)
        {
            const meshopt_Bounds bounds = meshopt_computeMeshletBounds(
                payload.vertexRemap.data() + descriptor.vertexOffset,
                payload.triangleIndices.data() + descriptor.triangleOffset, descriptor.triangleCount,
                positions.front().data(), positions.size(), sizeof(MeshletPosition));
            for (std::size_t component = 0; component < 3; ++component)
            {
                // meshoptimizer excludes degenerate triangles from its sphere.
                // Also recover a finite center when extreme float arithmetic overflowed.
                descriptor.sphereCenter[component] = MeshletFinite3(bounds.center)
                    ? bounds.center[component] : positions[payload.vertexRemap[descriptor.vertexOffset]][component];
                descriptor.coneApex[component] = bounds.cone_apex[component];
                descriptor.coneAxis[component] = bounds.cone_axis[component];
            }
            double radiusSquared = 0.0;
            for (std::size_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
            {
                const auto& position = positions[payload.vertexRemap[descriptor.vertexOffset + vertex]];
                radiusSquared = (std::max)(radiusSquared,
                    MeshletDistanceSquared(descriptor.sphereCenter, position.data()));
            }
            const double radius = std::sqrt(radiusSquared);
            if (radius > (std::numeric_limits<float>::max)())
            {
                return MeshletFail(diagnostic, "Meshlet bounding sphere exceeds finite float range.");
            }
            descriptor.sphereRadius = static_cast<float>(radius);
            if (radius > 0.0)
            {
                descriptor.sphereRadius = std::nextafter(descriptor.sphereRadius,
                    (std::numeric_limits<float>::infinity)());
            }
            if (!std::isfinite(descriptor.sphereRadius))
            {
                return MeshletFail(diagnostic, "Meshlet bounding sphere cannot be rounded outward safely.");
            }

            const bool deformed = Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneWeights);
            descriptor.flags = deformed ? kMeshletRequiresDeformedBounds : 0u;
            double requiredCutoff = 0.0;
            if (MeshletFinite3(descriptor.coneApex) && MeshletFinite3(descriptor.coneAxis)
                && std::isfinite(bounds.cone_cutoff) && bounds.cone_cutoff >= 0.0f && bounds.cone_cutoff < 1.0f
                && MeshletConeRequirement(mesh, payload, descriptor, requiredCutoff))
            {
                const double cutoff = (std::max)(requiredCutoff, static_cast<double>(bounds.cone_cutoff))
                    + 8.0 * std::numeric_limits<float>::epsilon();
                descriptor.coneCutoff = std::nextafter(static_cast<float>(cutoff), 1.0f);
                if (descriptor.coneCutoff < 1.0f)
                {
                    if (!deformed)
                    {
                        descriptor.flags |= kMeshletConeValid;
                    }
                    return true;
                }
            }

            // An unavailable cone is a finite, explicit reject-nothing sentinel.
            // Useful bind-pose cones on deformed geometry are retained above,
            // but never receive the ConeValid culling-eligibility flag.
            std::copy_n(descriptor.sphereCenter, 3, descriptor.coneApex);
            std::fill_n(descriptor.coneAxis, 3, 0.0f);
            descriptor.coneCutoff = 1.0f;
            return true;
        }

        [[nodiscard]] bool MeshletValidatePayload(const Mesh& mesh, const MeshletPayload& payload,
            std::string& diagnostic, const MeshletBuildSettings& expectedSettings)
        {
            if (payload.IsEmpty())
            {
                return true;
            }
            if (!MeshletSettingsSupported(expectedSettings) || !MeshletSettingsSupported(payload.settings)
                || !MeshletSettingsEqual(payload.settings, expectedSettings))
            {
                return MeshletFail(diagnostic, "Meshlet profile, builder version or settings are stale or unsupported.");
            }
            if (!MeshletGeometryValid(mesh, diagnostic))
            {
                return false;
            }
            const std::size_t limit = (std::numeric_limits<std::uint32_t>::max)();
            if (payload.descriptors.empty() || payload.descriptors.size() > limit
                || payload.vertexRemap.size() > limit || payload.triangleIndices.size() > limit
                || payload.primitiveRemap.size() > limit || payload.lod0.firstMeshlet != 0
                || payload.lod0.meshletCount != payload.descriptors.size()
                || payload.descriptors.size() > mesh.indices.size() / 3
                || payload.vertexRemap.size() > mesh.indices.size()
                || payload.primitiveRemap.size() != mesh.indices.size() / 3
                || payload.triangleIndices.size() != mesh.indices.size())
            {
                return MeshletFail(diagnostic, "Meshlet payload counts or the complete LOD0 range are invalid.");
            }
            if (payload.geometryDigest != ComputeMeshletGeometryDigest(mesh, expectedSettings))
            {
                return MeshletFail(diagnostic, "Meshlet digest does not match finalized geometry and build settings.");
            }

            std::vector<std::uint8_t> seenPrimitives(payload.primitiveRemap.size(), 0);
            std::size_t vertexEnd = 0;
            std::size_t triangleEnd = 0;
            std::size_t primitiveEnd = 0;
            const bool deformed = Has(mesh.vertices.AttributeMask(), VertexAttribute::BoneWeights);
            for (const MeshletDescriptor& descriptor : payload.descriptors)
            {
                if (descriptor.vertexCount == 0 || descriptor.vertexCount > expectedSettings.maxVertices
                    || descriptor.triangleCount == 0 || descriptor.triangleCount > expectedSettings.maxTriangles
                    || descriptor.vertexOffset != vertexEnd || descriptor.triangleOffset != triangleEnd
                    || descriptor.primitiveOffset != primitiveEnd
                    || !MeshletRangeFits(vertexEnd, descriptor.vertexCount, payload.vertexRemap.size())
                    || !MeshletRangeFits(triangleEnd, descriptor.triangleCount * 3u, payload.triangleIndices.size())
                    || !MeshletRangeFits(primitiveEnd, descriptor.triangleCount, payload.primitiveRemap.size()))
                {
                    return MeshletFail(diagnostic, "Meshlet descriptor has an invalid, overlapping or noncontiguous range.");
                }
                if ((descriptor.flags & ~(kMeshletConeValid | kMeshletRequiresDeformedBounds)) != 0
                    || descriptor.reserved[0] != 0 || descriptor.reserved[1] != 0 || descriptor.reservedBounds != 0
                    || ((descriptor.flags & kMeshletRequiresDeformedBounds) != 0) != deformed
                    || (deformed && (descriptor.flags & kMeshletConeValid) != 0))
                {
                    return MeshletFail(diagnostic, "Meshlet flags, padding or deformation eligibility are invalid.");
                }
                if (!MeshletFinite3(descriptor.sphereCenter) || !std::isfinite(descriptor.sphereRadius)
                    || descriptor.sphereRadius < 0.0f || !MeshletFinite3(descriptor.coneApex)
                    || !MeshletFinite3(descriptor.coneAxis) || !std::isfinite(descriptor.coneCutoff)
                    || descriptor.coneCutoff < 0.0f || descriptor.coneCutoff > 1.0f)
                {
                    return MeshletFail(diagnostic, "Meshlet sphere or cone bounds contain invalid values.");
                }
                const double radiusSquared = static_cast<double>(descriptor.sphereRadius) * descriptor.sphereRadius;
                for (std::size_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
                {
                    const auto source = payload.vertexRemap[vertexEnd + vertex];
                    if (source >= mesh.vertices.size())
                    {
                        return MeshletFail(diagnostic, "Meshlet vertex remap is outside the finalized vertex buffer.");
                    }
                    for (std::size_t previous = 0; previous < vertex; ++previous)
                    {
                        if (payload.vertexRemap[vertexEnd + previous] == source)
                        {
                            return MeshletFail(diagnostic, "Meshlet vertex remap contains a duplicate local vertex.");
                        }
                    }
                    const auto position = MeshletReadPosition(mesh, source);
                    if (MeshletDistanceSquared(position.data(), descriptor.sphereCenter) > radiusSquared)
                    {
                        return MeshletFail(diagnostic, "Meshlet bounding sphere does not contain every referenced vertex.");
                    }
                }

                std::array<bool, kMeshletMaxVertices> usedVertices{};
                for (std::size_t triangle = 0; triangle < descriptor.triangleCount; ++triangle)
                {
                    MeshletTriangle actual{};
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto local = payload.triangleIndices[triangleEnd + triangle * 3 + corner];
                        if (local >= descriptor.vertexCount)
                        {
                            return MeshletFail(diagnostic, "Meshlet micro-index is outside its local vertex range.");
                        }
                        usedVertices[local] = true;
                        actual[corner] = payload.vertexRemap[vertexEnd + local];
                    }
                    const auto primitive = payload.primitiveRemap[primitiveEnd + triangle];
                    if (primitive >= seenPrimitives.size() || seenPrimitives[primitive] != 0)
                    {
                        return MeshletFail(diagnostic, "Meshlet primitive remap is not a source-triangle permutation.");
                    }
                    seenPrimitives[primitive] = 1;
                    const std::size_t source = static_cast<std::size_t>(primitive) * 3;
                    if (actual != MeshletTriangle{
                        mesh.indices[source], mesh.indices[source + 1], mesh.indices[source + 2] })
                    {
                        return MeshletFail(diagnostic, "Meshlet primitive remap changes a source triangle or its corner order.");
                    }
                }
                for (std::size_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
                {
                    if (!usedVertices[vertex])
                    {
                        return MeshletFail(diagnostic, "Meshlet vertex remap includes an unreferenced local vertex.");
                    }
                }
                if ((descriptor.flags & kMeshletConeValid) != 0)
                {
                    double requiredCutoff = 0.0;
                    if (descriptor.coneCutoff >= 1.0f
                        || !MeshletConeRequirement(mesh, payload, descriptor, requiredCutoff)
                        || descriptor.coneCutoff < requiredCutoff)
                    {
                        return MeshletFail(diagnostic, "Meshlet cone is not conservative for its source triangles.");
                    }
                }
                vertexEnd += descriptor.vertexCount;
                triangleEnd += descriptor.triangleCount * 3u;
                primitiveEnd += descriptor.triangleCount;
            }
            if (vertexEnd != payload.vertexRemap.size() || triangleEnd != payload.triangleIndices.size()
                || primitiveEnd != payload.primitiveRemap.size())
            {
                return MeshletFail(diagnostic, "Meshlet payload contains unowned trailing data.");
            }
            return true;
        }

        struct MeshletSourceTriangle final
        {
            MeshletTriangle oriented{};
            std::uint32_t primitive{};
            std::uint32_t consumedDuplicates{};
        };

        [[nodiscard]] bool MeshletBuildPayload(const Mesh& mesh, MeshletPayload& payload,
            std::string& diagnostic, const MeshletBuildSettings& settings)
        {
            if (!MeshletSettingsSupported(settings))
            {
                return MeshletFail(diagnostic, "Unsupported meshlet profile, builder version or build settings.");
            }
            if (!MeshletGeometryValid(mesh, diagnostic))
            {
                return false;
            }
            if (mesh.indices.empty())
            {
                return true;
            }

            std::vector<MeshletPosition> positions(mesh.vertices.size());
            for (std::size_t vertex = 0; vertex < positions.size(); ++vertex)
            {
                positions[vertex] = MeshletReadPosition(mesh, vertex);
            }
            const std::size_t meshletBound = meshopt_buildMeshletsBound(
                mesh.indices.size(), settings.maxVertices, settings.maxTriangles);
            if (meshletBound == 0 || meshletBound > (std::numeric_limits<std::uint32_t>::max)())
            {
                return MeshletFail(diagnostic, "Meshlet builder capacity exceeds the 32-bit descriptor contract.");
            }
            std::vector<meshopt_Meshlet> built(meshletBound);
            std::vector<std::uint32_t> vertices(mesh.indices.size());
            std::vector<std::uint8_t> triangles(mesh.indices.size());
            // meshoptimizer 1.2 supports 126 triangles directly and writes tightly
            // packed micro-indices. Do not retain the old multiple-of-four rule.
            const std::size_t meshletCount = meshopt_buildMeshlets(built.data(), vertices.data(), triangles.data(),
                mesh.indices.data(), mesh.indices.size(), positions.front().data(), positions.size(),
                sizeof(MeshletPosition), settings.maxVertices, settings.maxTriangles, settings.coneWeight);
            if (meshletCount == 0 || meshletCount > meshletBound)
            {
                return MeshletFail(diagnostic, "Meshlet builder returned an invalid descriptor count.");
            }

            std::vector<MeshletSourceTriangle> sourceTriangles(mesh.indices.size() / 3);
            for (std::size_t primitive = 0; primitive < sourceTriangles.size(); ++primitive)
            {
                const std::size_t source = primitive * 3;
                sourceTriangles[primitive].oriented = MeshletOrientedTriangle(
                    mesh.indices[source], mesh.indices[source + 1], mesh.indices[source + 2]);
                sourceTriangles[primitive].primitive = static_cast<std::uint32_t>(primitive);
            }
            std::sort(sourceTriangles.begin(), sourceTriangles.end(),
                [](const MeshletSourceTriangle& a, const MeshletSourceTriangle& b)
                {
                    return a.oriented != b.oriented ? a.oriented < b.oriented : a.primitive < b.primitive;
                });

            payload.settings = settings;
            payload.geometryDigest = ComputeMeshletGeometryDigest(mesh, settings);
            payload.descriptors.reserve(meshletCount);
            payload.vertexRemap.reserve(mesh.indices.size());
            payload.triangleIndices.reserve(mesh.indices.size());
            payload.primitiveRemap.reserve(sourceTriangles.size());
            for (std::size_t meshlet = 0; meshlet < meshletCount; ++meshlet)
            {
                const auto& source = built[meshlet];
                if (source.vertex_count == 0 || source.vertex_count > settings.maxVertices
                    || source.triangle_count == 0 || source.triangle_count > settings.maxTriangles
                    || !MeshletRangeFits(source.vertex_offset, source.vertex_count, vertices.size())
                    || !MeshletRangeFits(source.triangle_offset, source.triangle_count * 3u, triangles.size())
                    || !MeshletRangeFits(payload.vertexRemap.size(), source.vertex_count, mesh.indices.size())
                    || !MeshletRangeFits(payload.triangleIndices.size(), source.triangle_count * 3u, mesh.indices.size())
                    || !MeshletRangeFits(payload.primitiveRemap.size(), source.triangle_count, sourceTriangles.size()))
                {
                    return MeshletFail(diagnostic, "Meshlet builder returned an invalid source range.");
                }
                MeshletDescriptor descriptor{};
                descriptor.vertexOffset = static_cast<std::uint32_t>(payload.vertexRemap.size());
                descriptor.triangleOffset = static_cast<std::uint32_t>(payload.triangleIndices.size());
                descriptor.primitiveOffset = static_cast<std::uint32_t>(payload.primitiveRemap.size());
                descriptor.vertexCount = source.vertex_count;
                descriptor.triangleCount = source.triangle_count;
                payload.vertexRemap.insert(payload.vertexRemap.end(), vertices.begin() + source.vertex_offset,
                    vertices.begin() + source.vertex_offset + source.vertex_count);
                payload.triangleIndices.insert(payload.triangleIndices.end(), triangles.begin() + source.triangle_offset,
                    triangles.begin() + source.triangle_offset + source.triangle_count * 3u);
                for (std::size_t vertex = 0; vertex < descriptor.vertexCount; ++vertex)
                {
                    if (payload.vertexRemap[descriptor.vertexOffset + vertex] >= positions.size())
                    {
                        return MeshletFail(diagnostic, "Meshlet builder returned an out-of-range vertex remap.");
                    }
                }
                for (std::size_t triangle = 0; triangle < descriptor.triangleCount; ++triangle)
                {
                    MeshletTriangle actual{};
                    for (std::size_t corner = 0; corner < 3; ++corner)
                    {
                        const auto local = payload.triangleIndices[descriptor.triangleOffset + triangle * 3 + corner];
                        if (local >= descriptor.vertexCount)
                        {
                            return MeshletFail(diagnostic, "Meshlet builder returned an out-of-range micro-index.");
                        }
                        actual[corner] = payload.vertexRemap[descriptor.vertexOffset + local];
                    }
                    const auto key = MeshletOrientedTriangle(actual[0], actual[1], actual[2]);
                    const auto first = std::lower_bound(sourceTriangles.begin(), sourceTriangles.end(), key,
                        [](const MeshletSourceTriangle& sourceTriangle, const MeshletTriangle& oriented)
                        {
                            return sourceTriangle.oriented < oriented;
                        });
                    if (first == sourceTriangles.end() || first->oriented != key)
                    {
                        return MeshletFail(diagnostic, "Meshlet builder returned a triangle absent from indexed geometry.");
                    }
                    const auto remaining = static_cast<std::size_t>(sourceTriangles.end() - first);
                    if (first->consumedDuplicates >= remaining || (first + first->consumedDuplicates)->oriented != key)
                    {
                        return MeshletFail(diagnostic, "Meshlet builder returned an extra duplicate source triangle.");
                    }
                    // Repeated oriented triangles consume source ordinals in ascending
                    // order. No unordered-container iteration or pointer identity is used.
                    const std::uint32_t primitive = (first + first->consumedDuplicates)->primitive;
                    payload.primitiveRemap.push_back(primitive);
                    ++first->consumedDuplicates;

                    // Identical oriented triangles can have different cyclic corner
                    // orders. Restore the selected source's exact order so primitive
                    // barycentrics and provoking-vertex semantics remain meaningful.
                    const std::size_t sourceIndex = static_cast<std::size_t>(primitive) * 3;
                    const std::size_t triangleOffset = descriptor.triangleOffset + triangle * 3;
                    const std::array<std::uint8_t, 3> local{
                        payload.triangleIndices[triangleOffset], payload.triangleIndices[triangleOffset + 1],
                        payload.triangleIndices[triangleOffset + 2] };
                    for (std::size_t rotation = 0; rotation < 3; ++rotation)
                    {
                        if (actual[rotation] == mesh.indices[sourceIndex]
                            && actual[(rotation + 1) % 3] == mesh.indices[sourceIndex + 1]
                            && actual[(rotation + 2) % 3] == mesh.indices[sourceIndex + 2])
                        {
                            for (std::size_t corner = 0; corner < 3; ++corner)
                            {
                                payload.triangleIndices[triangleOffset + corner] = local[(rotation + corner) % 3];
                            }
                            break;
                        }
                    }
                }
                if (!MeshletBuildBounds(mesh, payload, descriptor, positions, diagnostic))
                {
                    return false;
                }
                payload.descriptors.push_back(descriptor);
            }
            payload.lod0 = { 0u, static_cast<std::uint32_t>(meshletCount) };
            return MeshletValidatePayload(mesh, payload, diagnostic, settings);
        }
    }

    MeshletGeometryDigest ComputeMeshletGeometryDigest(const Mesh& mesh, const MeshletBuildSettings& settings) noexcept
    {
        Hash::Sha256 hash;
        static constexpr char domain[] = "CreatorEngine.Meshlet.FinalGeometry.v1";
        hash.Update(domain, sizeof(domain));
        MeshletHashU32(hash, settings.profileVersion);
        MeshletHashU32(hash, settings.builderVersion);
        MeshletHashU32(hash, settings.meshoptimizerVersion);
        MeshletHashU32(hash, settings.maxVertices);
        MeshletHashU32(hash, settings.maxTriangles);
        MeshletHashU32(hash, std::bit_cast<std::uint32_t>(settings.coneWeight));
        MeshletHashU64(hash, kVertexLayoutTableHash);
        MeshletHashU32(hash, mesh.vertices.AttributeMask());
        MeshletHashU32(hash, mesh.vertices.Stride());
        MeshletHashU64(hash, mesh.vertices.size());
        MeshletHashU64(hash, mesh.vertices.ByteSize());
        hash.Update(mesh.vertices.Bytes().data(), mesh.vertices.ByteSize());
        MeshletHashU64(hash, mesh.indices.size());
        for (const std::uint32_t index : mesh.indices)
        {
            MeshletHashU32(hash, index);
        }
        return hash.Finish();
    }

    bool BuildMeshlets(const Mesh& mesh, MeshletPayload& outPayload,
        std::string& diagnostic, const MeshletBuildSettings& settings)
    {
        // settings may alias outPayload.settings during a rebuild.
        const MeshletBuildSettings buildSettings = settings;
        outPayload = {};
        diagnostic.clear();
        try
        {
            MeshletPayload payload;
            if (!MeshletBuildPayload(mesh, payload, diagnostic, buildSettings))
            {
                return false;
            }
            outPayload = std::move(payload);
            return true;
        }
        catch (const std::bad_alloc&)
        {
            return MeshletFail(diagnostic, "Meshlet generation ran out of memory; indexed geometry is retained.");
        }
        catch (const std::length_error&)
        {
            return MeshletFail(diagnostic, "Meshlet generation exceeded container limits; indexed geometry is retained.");
        }
    }

    bool ValidateMeshlets(const Mesh& mesh, const MeshletPayload& payload,
        std::string& diagnostic, const MeshletBuildSettings& expectedSettings)
    {
        diagnostic.clear();
        try
        {
            return MeshletValidatePayload(mesh, payload, diagnostic, expectedSettings);
        }
        catch (const std::bad_alloc&)
        {
            return MeshletFail(diagnostic, "Meshlet validation ran out of memory; the derived route must be disabled.");
        }
        catch (const std::length_error&)
        {
            return MeshletFail(diagnostic, "Meshlet validation exceeded container limits; the derived route must be disabled.");
        }
    }
}
