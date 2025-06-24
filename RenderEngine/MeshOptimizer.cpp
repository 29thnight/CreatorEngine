#include "MeshOptimizer.h"
#include "ResourceAllocator.h"
#include <unordered_map>
#include <cstring>

void RecalculateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
{
    std::vector<Mathf::Vector3> tan1(vertices.size(), {});
    std::vector<Mathf::Vector3> tan2(vertices.size(), {});

    for (size_t i = 0; i < indices.size(); i += 3)
    {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        const auto& p0 = v0.position;
        const auto& p1 = v1.position;
        const auto& p2 = v2.position;

        const auto& uv0 = v0.uv0;
        const auto& uv1 = v1.uv0;
        const auto& uv2 = v2.uv0;

        float x1 = p1.x - p0.x;
        float x2 = p2.x - p0.x;
        float y1 = p1.y - p0.y;
        float y2 = p2.y - p0.y;
        float z1 = p1.z - p0.z;
        float z2 = p2.z - p0.z;

        float s1 = uv1.x - uv0.x;
        float s2 = uv2.x - uv0.x;
        float t1 = uv1.y - uv0.y;
        float t2 = uv2.y - uv0.y;

        float r = 1.0f / (s1 * t2 - s2 * t1);
        Mathf::Vector3 sdir((t2 * x1 - t1 * x2) * r,
            (t2 * y1 - t1 * y2) * r,
            (t2 * z1 - t1 * z2) * r);
        Mathf::Vector3 tdir((s1 * x2 - s2 * x1) * r,
            (s1 * y2 - s2 * y1) * r,
            (s1 * z2 - s2 * z1) * r);

        tan1[i0] += sdir; tan2[i0] += tdir;
        tan1[i1] += sdir; tan2[i1] += tdir;
        tan1[i2] += sdir; tan2[i2] += tdir;
    }

    for (size_t i = 0; i < vertices.size(); ++i)
    {
        const auto& n = vertices[i].normal;
        const auto& t = tan1[i];

        // Gram-Schmidt orthogonalize
        Mathf::Vector3 normalized = Mathf::Vector3(t - n * n.Dot(t));
		normalized.Normalize();
		vertices[i].tangent = normalized;

        // Calculate handedness (w component of tangent)
        float handedness = (n.Cross(t).Dot(tan2[i]) < 0.0f) ? -1.0f : 1.0f;
        vertices[i].bitangent = n.Cross(vertices[i].tangent) * handedness;
    }
}

void MeshOptimizer::Optimize(Mesh& mesh, float overdrawThreshold)
{
    meshopt_setAllocator(malloc, free);

    const auto& srcVertices = mesh.m_vertices;
    const auto& srcIndices = mesh.m_indices;

    const size_t vertexCount = srcVertices.size();
    const size_t indexCount = srcIndices.size();

    // 1. 압축된 vertex 구조로 복사
    std::vector<OptVertex> optVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        optVertices[i] = srcVertices[i];

    // 2. 인덱스 최적화
    std::vector<uint32_t> indices = srcIndices;

    meshopt_optimizeVertexCache(indices.data(), indices.data(), indexCount, vertexCount);

    meshopt_optimizeOverdraw(
        indices.data(),
        indices.data(),
        indexCount,
        reinterpret_cast<const float*>(optVertices.data()),
        vertexCount,
        sizeof(OptVertex),
        overdrawThreshold);

    std::vector<uint32_t> remap(indexCount);
    size_t newVertexCount = meshopt_optimizeVertexFetchRemap(
        remap.data(),
        indices.data(),
        indexCount,
        vertexCount);

    std::vector<OptVertex> remappedOptVertices(newVertexCount);
    meshopt_remapVertexBuffer(
        remappedOptVertices.data(),
        optVertices.data(),
        vertexCount,
        sizeof(OptVertex),
        remap.data());

    meshopt_remapIndexBuffer(
        indices.data(),
        indices.data(),
        indexCount,
        remap.data());

    // 3. 다시 Vertex로 변환
    std::vector<Vertex> newVertices(newVertexCount);
    for (size_t i = 0; i < newVertexCount; ++i)
        remappedOptVertices[i].ToVertex(newVertices[i]);

    mesh.m_vertices = std::move(newVertices);
    mesh.m_indices = std::move(indices);

    RecalculateTangents(mesh.m_vertices, mesh.m_indices);
}

void MeshOptimizer::GenerateLODs(std::vector<Mesh*>& lods, const Mesh* sourceMesh, size_t maxLODs, float lodFactor)
{
    meshopt_setAllocator(malloc, free);

    const auto& srcVertices = sourceMesh->m_vertices;
    const auto& srcIndices = sourceMesh->m_indices;
    const size_t vertexCount = srcVertices.size();
    const size_t indexCount = srcIndices.size();

    static std::vector<uint32_t> simplifiedIndices;

    // Step 1: Vertex 압축
    std::vector<OptVertex> optVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        optVertices[i] = srcVertices[i];

    lods.push_back(const_cast<Mesh*>(sourceMesh)); // LOD0 포함 (주의: 수정하지 말 것)

    std::vector<uint32_t> workingIndices = srcIndices;

    for (size_t lodLevel = 1; lodLevel <= maxLODs; ++lodLevel)
    {
        size_t targetIndexCount = static_cast<size_t>(indexCount * std::pow(lodFactor, lodLevel));
        if (targetIndexCount < 96)
            break;

        simplifiedIndices.clear();
        simplifiedIndices.resize(indexCount);
        size_t resultCount = meshopt_simplify(
            simplifiedIndices.data(),
            workingIndices.data(),
            workingIndices.size(),
            reinterpret_cast<const float*>(&optVertices[0].px),
            vertexCount,
            sizeof(OptVertex),
            targetIndexCount,
            1e-2f); // aggressiveness

        if (resultCount == 0)
            break;

        // Vertex Fetch Remap
        std::vector<uint32_t> remap(resultCount);
        size_t newVertexCount = meshopt_optimizeVertexFetchRemap(
            remap.data(),
            simplifiedIndices.data(),
            resultCount,
            vertexCount);

        std::vector<OptVertex> remappedOptVertices(newVertexCount);
        meshopt_remapVertexBuffer(
            remappedOptVertices.data(),
            optVertices.data(),
            vertexCount,
            sizeof(OptVertex),
            remap.data());

        meshopt_remapIndexBuffer(
            simplifiedIndices.data(),
            simplifiedIndices.data(),
            resultCount,
            remap.data());

        // 다시 Vertex로 변환
        std::vector<Vertex> finalVertices;
        finalVertices.resize(newVertexCount);
        for (size_t i = 0; i < remappedOptVertices.size(); ++i)
        {
            remappedOptVertices[i].ToVertex(finalVertices[i]);
        }

        std::vector<uint32_t> shrink_to_fitIndexes(resultCount);
		std::copy(
            simplifiedIndices.begin(), 
            simplifiedIndices.begin() + resultCount, 
            shrink_to_fitIndexes.begin()
        );

        // tangent/bitangent 재계산
        RecalculateTangents(finalVertices, shrink_to_fitIndexes);

        // 리소스 풀을 통한 Mesh* 생성
        std::string lodName = sourceMesh->GetName() + "_LOD" + std::to_string(lodLevel);

        Mesh* lodMesh = AllocateResource<Mesh>(lodName, std::move(finalVertices), std::move(shrink_to_fitIndexes));
        MeshOptimizer::GenerateShadowMesh(*lodMesh);

        lods.push_back(lodMesh);
    }
}

void MeshOptimizer::GenerateShadowMesh(Mesh& mesh)
{
    meshopt_setAllocator(malloc, free);

    const auto& srcVertices = mesh.m_vertices;
    const auto& srcIndices = mesh.m_indices;

    const size_t vertexCount = srcVertices.size();
    const size_t indexCount = srcIndices.size();

    // Step 1: 압축된 vertex 생성 (position only로 쓰기 위함)
    std::vector<OptVertex> optVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        optVertices[i] = srcVertices[i];

    // Step 2: 그림자 인덱스 생성
    std::vector<uint32_t> shadowIndices(indexCount); // 최대 indexCount만큼 필요
    meshopt_generateShadowIndexBuffer(
        shadowIndices.data(),
        srcIndices.data(),
        indexCount,
        reinterpret_cast<const float*>(&optVertices[0].px), // position only
        vertexCount,
        sizeof(float) * 3, // position 크기
        sizeof(OptVertex)       // stride
    );

    // Step 3: 정점 캐시 최적화
    meshopt_optimizeVertexCache(
        shadowIndices.data(),
        shadowIndices.data(),
        indexCount,
        vertexCount
    );

    // Step 4: shadowVertices 복사
    std::vector<Vertex> shadowVertices(vertexCount);
    for (size_t i = 0; i < vertexCount; ++i)
        optVertices[i].ToVertex(shadowVertices[i]);

    // Step 5: 결과 저장
    mesh.m_shadowVertices = std::move(shadowVertices);
    mesh.m_shadowIndices = std::move(shadowIndices);

	// Step 6: 그림자 버퍼 생성
	mesh.MakeShadowOptimizedBuffer();
    mesh.m_isShadowOptimized = true;
}
