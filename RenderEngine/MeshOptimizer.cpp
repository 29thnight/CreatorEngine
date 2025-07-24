#include "MeshOptimizer.h"
#include "ResourceAllocator.h"
#include <meshoptimizer.h>
#include <stdexcept>

void RecalculateNormalsAndTangents(std::vector<Vertex>& vertices, const std::vector<uint32>& indices)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }

    // �ӽ� ����Ҹ� ����� ��ְ� ź��Ʈ�� �����մϴ�.
    std::vector<Mathf::Vector3> newNormals(vertices.size(), Mathf::Vector3::Zero);
    std::vector<Mathf::Vector3> newTangents(vertices.size(), Mathf::Vector3::Zero);
    std::vector<Mathf::Vector3> newBitangents(vertices.size(), Mathf::Vector3::Zero);

    // ��� �ﰢ���� ��ȸ�ϸ� ��ְ� ź��Ʈ�� ����ϰ� �� ������ �����մϴ�.
    for (size_t i = 0; i < indices.size(); i += 3)
    {
        uint32 i0 = indices[i + 0];
        uint32 i1 = indices[i + 1];
        uint32 i2 = indices[i + 2];

        Vertex& v0 = vertices[i0];
        Vertex& v1 = vertices[i1];
        Vertex& v2 = vertices[i2];

        // ��ġ ���� ���� (Edge)
        Mathf::Vector3 edge1 = v1.position - v0.position;
        Mathf::Vector3 edge2 = v2.position - v0.position;

        // UV ��ǥ ���� (Delta UV)
        Mathf::Vector2 deltaUV1 = v1.uv0 - v0.uv0;
        Mathf::Vector2 deltaUV2 = v2.uv0 - v0.uv0;

        // �� ��� ��� �� ����
        Mathf::Vector3 faceNormal = edge1.Cross(edge2);
        newNormals[i0] += faceNormal;
        newNormals[i1] += faceNormal;
        newNormals[i2] += faceNormal;

        // �� ź��Ʈ �� ����ź��Ʈ ���
        float r = 1.0f / (deltaUV1.x * deltaUV2.y - deltaUV1.y * deltaUV2.x);
        if (!isinf(r) && !isnan(r))
        {
            Mathf::Vector3 tangent = (edge1 * deltaUV2.y - edge2 * deltaUV1.y) * r;
            Mathf::Vector3 bitangent = (edge2 * deltaUV1.x - edge1 * deltaUV2.x) * r;

            newTangents[i0] += tangent;
            newTangents[i1] += tangent;
            newTangents[i2] += tangent;

            newBitangents[i0] += bitangent;
            newBitangents[i1] += bitangent;
            newBitangents[i2] += bitangent;
        }
    }

    // ��� ������ ��ȸ�ϸ� ������ ���� ����ȭ�ϰ� ����ȭ�մϴ�.
    for (size_t i = 0; i < vertices.size(); ++i)
    {
        // ��� ����ȭ
        vertices[i].normal = newNormals[i];
        vertices[i].normal.Normalize();

        // �׶�-����Ʈ ����ȭ�� ����Ͽ� ź��Ʈ ����
        vertices[i].tangent = newTangents[i] - vertices[i].normal * newTangents[i].Dot(newTangents[i]);
        vertices[i].tangent.Normalize();

        // ����ź��Ʈ�� ���⼺(handedness)�� ����ϰ� ���� ����ź��Ʈ ����
        if (vertices[i].normal.Cross(newTangents[i]).Dot(newBitangents[i]) < 0.0f)
        {
            vertices[i].tangent *= -1.0f;
        }

        vertices[i].bitangent = vertices[i].normal.Cross(vertices[i].tangent);
    }
}

MeshOptimizer::LOD::Optional MeshOptimizer::GenerateLODs(const Mesh& originalMesh, const std::vector<float>& lodThresholds)
{
    if (originalMesh.GetVertices().empty() || originalMesh.GetIndices().empty() || lodThresholds.empty())
    {
        return std::nullopt;
    }

    try
    {
        std::vector<LOD> lods;
        lods.reserve(lodThresholds.size());

        const std::vector<Vertex>& sourceVertices = originalMesh.GetVertices();
        const std::vector<uint32>& sourceIndices = originalMesh.GetIndices();

        std::vector<uint32> simplifiedIndices(sourceIndices.size());
        std::vector<Vertex> simplifiedVertices(sourceVertices.size());

        for (float threshold : lodThresholds)
        {
            if (0 == threshold)
            {
                throw std::exception("threshold value 0");
            }

            const size_t target_index_count = static_cast<size_t>(sourceIndices.size() * threshold);
            const float target_error = 1.0f - threshold;

            simplifiedIndices.resize(sourceIndices.size());
            size_t newIndexCount = meshopt_simplify(
                &simplifiedIndices[0],
                &sourceIndices[0], sourceIndices.size(),
                &sourceVertices[0].position.x, sourceVertices.size(), sizeof(Vertex),
                target_index_count, target_error);
            simplifiedIndices.resize(newIndexCount);

            if(0 != newIndexCount)
            {
                simplifiedVertices.resize(sourceVertices.size());
                size_t newVertexCount = meshopt_optimizeVertexFetch(
                    &simplifiedVertices[0],
                    &simplifiedIndices[0], simplifiedIndices.size(),
                    &sourceVertices[0], sourceVertices.size(), sizeof(Vertex));
                simplifiedVertices.resize(newVertexCount);
            }
            else
            {
                throw std::exception("newIndexCount count 0");
            }

            LOD lod;
            lod.threshold = threshold;
            lod.vertices = simplifiedVertices;
            lod.indices = simplifiedIndices;

            RecalculateNormalsAndTangents(lod.vertices, lod.indices);

            lods.push_back(std::move(lod));
        }

        return lods;
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "GenerateLODs failed: Not enough memory. " << e.what() << std::endl;
        return std::nullopt; // ���� �� std::nullopt�� ��ȯ�մϴ�.
    }
    catch (const std::exception& e)
    {
        std::cerr << "GenerateLODs failed: " << e.what() << std::endl;
        return std::nullopt; // �ٸ� ���� �߻� �ÿ��� std::nullopt�� ��ȯ�մϴ�.
	}
}

void MeshOptimizer::OptimizeMesh(std::vector<Vertex>& vertices, std::vector<uint32>& indices)
{
    if (vertices.empty() || indices.empty())
    {
        return;
    }

    try
    {
        meshopt_optimizeVertexCache(&indices[0], &indices[0], indices.size(), vertices.size());
        meshopt_optimizeOverdraw(&indices[0], &indices[0], indices.size(), &vertices[0].position.x, vertices.size(), sizeof(Vertex), 1.05f);
        meshopt_optimizeVertexFetch(&vertices[0], &indices[0], indices.size(), &vertices[0], vertices.size(), sizeof(Vertex));
        RecalculateNormalsAndTangents(vertices, indices);
    }
    catch (const std::bad_alloc& e)
    {
        std::cerr << "OptimizeMesh failed: Not enough memory. " << e.what() << std::endl;
        // ���� �� �ƹ��͵� ���� �ʰ� ��ȯ�Ͽ� ���� �����͸� �����մϴ�.
        return;
    }
}
