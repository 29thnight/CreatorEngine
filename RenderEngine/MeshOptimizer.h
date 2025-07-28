#pragma once
#include "Mesh.h" // 사용자 정의 Mesh 구조

class MeshOptimizer
{
public:
    struct LOD
    {
        using Optional = std::optional<std::vector<MeshOptimizer::LOD>>;
        std::vector<Vertex> vertices;
        std::vector<uint32> indices;
        float threshold;
    };

    static LOD::Optional GenerateLODs(const Mesh& originalMesh, const std::vector<float>& lodThresholds);

private:
    static void OptimizeMesh(std::vector<Vertex>& vertices, std::vector<uint32>& indices);
};