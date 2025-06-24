#pragma once
#include "Mesh.h" // ����� ���� Mesh ����
#include <meshoptimizer.h>

class MeshOptimizer
{
private:
    struct OptVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;

        OptVertex& operator=(const Vertex& vertex)
        {
            px = vertex.position.x;
            py = vertex.position.y;
            pz = vertex.position.z;
            nx = vertex.normal.x;
            ny = vertex.normal.y;
            nz = vertex.normal.z;
            u  = vertex.uv0.x;
            v  = vertex.uv0.y;

            return *this;
		}

        void ToVertex(Vertex& vertex) const
        {
            vertex.position = { px, py, pz };
            vertex.normal   = { nx, ny, nz };
            vertex.uv0      = { u, v };
            // bitangent�� tangent�� �ʿ�� �߰�
		}
    };
public:
    // ����ȭ ����: Mesh ��ü ���� ����
    static void Optimize(Mesh& mesh, float overdrawThreshold = 1.05f);

    // LOD �޽� ���� (simplifiedMesh�� ���� �ܰ��)
    static void GenerateLODs(std::vector<Mesh*>& lods, const Mesh* sourceMesh, size_t maxLODs = 3, float lodFactor = 0.5f);

    //�׸��� �޽� ����
    static void GenerateShadowMesh(Mesh& mesh);
};