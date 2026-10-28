#include "../Containers/ContainerAPI.h"
#pragma once
#include "Mesh.h" // 사용자 정의 Mesh 구조

class MeshOptimizer
{
public:
    struct LOD
    {
        using Optional = std::optional<ce::dynamic_array<MeshOptimizer::LOD>>;
        ce::dynamic_array<Vertex> vertices;
        ce::dynamic_array<uint32> indices;
        float threshold;
    };

    static LOD::Optional GenerateLODs(const Mesh& originalMesh, const ce::dynamic_array<float>& lodThresholds);

private:
    static void OptimizeMesh(ce::dynamic_array<Vertex>& vertices, ce::dynamic_array<uint32>& indices);
};