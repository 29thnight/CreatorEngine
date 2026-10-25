#pragma once
#include "../ModelData.h"

namespace experiment::importer
{
    using SkinErrorVector = std::array<double, 3>;
    struct SkinErrorVertex
    {
        SkinErrorVector position;
        std::array<uint32_t, 4> joints{};
        std::array<double, 4> weights{};
    };
    inline double SkinErrorDot(const SkinErrorVector& a, const SkinErrorVector& b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }
    inline SkinErrorVector SkinErrorSubtract(const SkinErrorVector& a, const SkinErrorVector& b)
    {
        return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    }
    inline std::vector<SkinErrorVertex> SkinErrorVertices(const Mesh& mesh)
    {
        std::vector<SkinErrorVertex> vertices(mesh.vertices.size());
        const auto mask = mesh.vertices.AttributeMask();
        for (size_t i = 0; i < vertices.size(); ++i)
        {
            const auto data = mesh.vertices.Bytes().data() + i * mesh.vertices.Stride();
            float position[3], weights[4];
            uint32_t joints;
            std::memcpy(position, data + OffsetOf(mask, VertexAttribute::Position), 12);
            std::memcpy(weights, data + OffsetOf(mask, VertexAttribute::BoneWeights), 16);
            std::memcpy(&joints, data + OffsetOf(mask, VertexAttribute::BoneIndices), 4);
            auto& v = vertices[i];
            v.position = {position[0], position[1], position[2]};
            double sum{};
            for (auto w : weights)
            {
                if (!std::isfinite(w) || w < 0 || w > 1)
                {
                    throw std::runtime_error("Skinned LOD weight range");
                }
                sum += w;
            }
            if (sum != 0 && std::abs(sum - 1.0) > 1e-4)
            {
                throw std::runtime_error("Skinned LOD normalized weight domain");
            }
            if (sum == 0)
            {
                v.joints[0] = 256; // The GPU's zero-weight bind-pose identity.
                v.weights[0] = 1;
            }
            else
            {
                for (unsigned k = 0; k < 4; ++k)
                {
                    v.joints[k] = (joints >> (8 * k)) & 255u;
                    v.weights[k] = weights[k] / sum;
                }
            }
        }
        return vertices;
    }
    inline std::array<double, 3> SkinErrorClosestBarycentric(const SkinErrorVector& p, const SkinErrorVector& a,
                                                             const SkinErrorVector& b, const SkinErrorVector& c)
    {
        const auto ab = SkinErrorSubtract(b, a), ac = SkinErrorSubtract(c, a), ap = SkinErrorSubtract(p, a);
        const auto aa = SkinErrorDot(ab, ab), bb = SkinErrorDot(ab, ac), cc = SkinErrorDot(ac, ac);
        const auto u = SkinErrorDot(ap, ab), v = SkinErrorDot(ap, ac), determinant = aa * cc - bb * bb;
        if (determinant > 1e-28 * (std::max)(aa * cc, 1.0))
        {
            const auto y = (u * cc - v * bb) / determinant, z = (v * aa - u * bb) / determinant;
            if (y >= 0 && z >= 0 && y + z <= 1)
            {
                return {1 - y - z, y, z};
            }
        }
        const SkinErrorVector points[]{a, b, c};
        std::array<double, 3> best{1, 0, 0};
        double distance = INFINITY;
        for (unsigned edge = 0; edge < 3; ++edge)
        {
            const auto next = (edge + 1) % 3;
            const auto delta = SkinErrorSubtract(points[next], points[edge]);
            const auto length = SkinErrorDot(delta, delta);
            const auto t =
                length > 0 ? std::clamp(SkinErrorDot(SkinErrorSubtract(p, points[edge]), delta) / length, 0., 1.) : 0.;
            SkinErrorVector error;
            for (unsigned k = 0; k < 3; ++k)
            {
                error[k] = p[k] - points[edge][k] - t * delta[k];
            }
            const auto d = SkinErrorDot(error, error);
            if (d < distance)
            {
                distance = d;
                best = {0, 0, 0};
                best[edge] = 1 - t;
                best[next] = t;
            }
        }
        return best;
    }
    struct SkinLodErrorCertificate
    {
        double position{}, weightedPosition{}, weights{};
        uint32_t flags{1};
    };
    inline uint32_t SkinErrorFlags(std::span<const SkinErrorVertex> vertices, std::span<const uint32_t> indices)
    {
        for (const auto i : indices)
        {
            if (vertices[i].joints[0] == 256)
            {
                return 3;
            }
        }
        return 1;
    }
    // Each source triangle maps affinely into ONE target triangle. The three
    // vertex residual bounds extend to its entire interior by convexity.
    // Evaluate both directions to bound the posed surfaces, not just samples.
    inline SkinLodErrorCertificate CertifySkinLod(std::span<const SkinErrorVertex> vertices,
                                                  std::span<const uint32_t> original, std::span<const uint32_t> coarse)
    {
        SkinLodErrorCertificate result;
        result.flags = SkinErrorFlags(vertices, original);
        const auto direction = [&](auto from, auto to)
        {
            for (size_t source = 0; source < from.size(); source += 3)
            {
                double best = INFINITY;
                size_t target{};
                std::array<std::array<double, 3>, 3> bary{};
                for (size_t candidate = 0; candidate < to.size(); candidate += 3)
                {
                    double distance{};
                    std::array<std::array<double, 3>, 3> current;
                    for (unsigned corner = 0; corner < 3; ++corner)
                    {
                        const auto& p = vertices[from[source + corner]].position;
                        current[corner] = SkinErrorClosestBarycentric(p, vertices[to[candidate]].position,
                                                                      vertices[to[candidate + 1]].position,
                                                                      vertices[to[candidate + 2]].position);
                        SkinErrorVector delta = p;
                        for (unsigned k = 0; k < 3; ++k)
                        {
                            for (unsigned axis = 0; axis < 3; ++axis)
                            {
                                delta[axis] -= current[corner][k] * vertices[to[candidate + k]].position[axis];
                            }
                        }
                        distance = (std::max)(distance, SkinErrorDot(delta, delta));
                    }
                    if (distance < best)
                    {
                        best = distance;
                        target = candidate;
                        bary = current;
                    }
                }
                if (!std::isfinite(best))
                {
                    throw std::runtime_error("Empty/nonfinite skinned LOD correspondence");
                }
                result.position = (std::max)(result.position, std::sqrt(best));
                for (unsigned corner = 0; corner < 3; ++corner)
                {
                    struct Residual
                    {
                        uint32_t joint{};
                        SkinErrorVector position{};
                        double weight{};
                    };
                    std::array<Residual, 16> residuals{};
                    size_t count{};
                    const auto add = [&](const SkinErrorVertex& v, double factor)
                    {
                        for (unsigned influence = 0; influence < 4; ++influence)
                        {
                            const auto weight = factor * v.weights[influence];
                            if (weight == 0)
                            {
                                continue;
                            }
                            size_t slot{};
                            while (slot < count && residuals[slot].joint != v.joints[influence])
                            {
                                ++slot;
                            }
                            if (slot == count)
                            {
                                residuals[count++].joint = v.joints[influence];
                            }
                            residuals[slot].weight += weight;
                            for (unsigned axis = 0; axis < 3; ++axis)
                            {
                                residuals[slot].position[axis] += weight * v.position[axis];
                            }
                        }
                    };
                    add(vertices[from[source + corner]], 1);
                    for (unsigned k = 0; k < 3; ++k)
                    {
                        add(vertices[to[target + k]], -bary[corner][k]);
                    }
                    double position{}, weight{};
                    for (size_t i = 0; i < count; ++i)
                    {
                        position += std::sqrt(SkinErrorDot(residuals[i].position, residuals[i].position));
                        weight += std::abs(residuals[i].weight);
                    }
                    result.weightedPosition = (std::max)(result.weightedPosition, position);
                    result.weights = (std::max)(result.weights, weight);
                }
            }
        };
        direction(original, coarse);
        direction(coarse, original);
        return result;
    }
    inline float SkinErrorOutward(double value)
    {
        return std::nextafter(float(value + (1 + value) * 1e-12), INFINITY);
    }
} // namespace experiment::importer
