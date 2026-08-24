#include "Mesh.h"
#include "Camera.h"
#include "MeshOptimizer.h"

// ★ DeviceState.h include가 여기 있었다 (A-③, 2026-08-09).
//
//   MakeShadowOptimizedBuffer의 DirectX11::CreateBuffer 때문이었다. 그것을
//   걷었으므로 이 파일에 DX11이 남지 않는다 - Mesh.cpp는 이제 CPU 배열만
//   다룬다.

// CreateLODBuffers 헬퍼가 여기 있었다.
//
// 이름대로 LOD별 DX11 정점·인덱스 버퍼를 만들던 함수인데, 그 두 본문이
// 앞선 정리에서 비워지고 indexCount 대입 한 줄만 남아 있었다. 껍데기를
// 함수로 감싸 두면 "여기서 GPU 자원이 생긴다"로 읽히므로 호출부에 편다.

Mesh::Mesh(std::string_view _name, const std::vector<Vertex>& _vertices, const std::vector<uint32>& _indices) :
	m_name(_name),
	m_vertices(_vertices),
	m_indices(_indices)
{
	/*for (int i = 0; i < m_indices.size(); i += 3)
	{
		uint32 index0 = m_indices[i];
		uint32 index1 = m_indices[i + 1];
		uint32 index2 = m_indices[i + 2];

		Vertex& vertex0 = m_vertices[index0];
		Vertex& vertex1 = m_vertices[index1];
		Vertex& vertex2 = m_vertices[index2];

		Mathf::Vector3 edge0 = vertex1.position - vertex0.position;
		Mathf::Vector3 edge1 = vertex2.position - vertex0.position;
		Mathf::Vector2 dUV0 = vertex1.uv0 - vertex0.uv0;
		Mathf::Vector2 dUV1 = vertex2.uv0 - vertex0.uv0;

		float f = 1.0f / (dUV0.x * dUV1.y - dUV1.x * dUV0.y);
		Mathf::Vector3 tangent
		{
			f * (dUV1.y * edge0.x - dUV0.y * edge1.x),
			f * (dUV1.y * edge0.y - dUV0.y * edge1.y),
			f * (dUV1.y * edge0.z - dUV0.y * edge1.z),
		};

		Mathf::Vector3 bitangent
		{
			f * (dUV1.x * edge0.x - dUV0.x * edge1.x),
			f * (dUV1.x * edge0.y - dUV0.x * edge1.y),
			f * (dUV1.x * edge0.z - dUV0.x * edge1.z),
		};

		vertex0.tangent = tangent;
		vertex1.tangent = tangent;
		vertex2.tangent = tangent;

		vertex0.bitangent = bitangent;
		vertex1.bitangent = bitangent;
		vertex2.bitangent = bitangent;
	}*/

}

Mesh::Mesh(std::string_view _name, std::vector<Vertex>&& _vertices, std::vector<uint32>&& _indices) :
	m_name(_name), m_vertices(std::move(_vertices)), m_indices(std::move(_indices))
{
}

Mesh::Mesh(Mesh&& _other) noexcept :
	m_vertices(std::move(_other.m_vertices)),
	m_indices(std::move(_other.m_indices)),
	m_name(std::move(_other.m_name)),
	m_materialIndex(_other.m_materialIndex),
	m_boundingBox(_other.m_boundingBox),
	m_boundingSphere(_other.m_boundingSphere),
	m_hashingMesh(_other.m_hashingMesh),
	m_modelName(std::move(_other.m_modelName))
{
}

Mesh::~Mesh()
{
}

void Mesh::AssetInit()
{
}

// [NEW] Check if LODs have been generated
bool Mesh::HasLODs() const
{
	return !m_LODs.empty();
}

// [NEW] Generate LODs
void Mesh::GenerateLODs(const std::vector<float>&lodThresholds)
{
	if (m_vertices.empty() || m_indices.empty())
	{
		std::cerr << "Mesh::GenerateLODs: Original mesh data is empty. Cannot generate LODs." << std::endl;
		return;
	}

	// Store the thresholds
	m_LODThresholds = lodThresholds;

	// Clear existing LODs (if any) before generating new ones
	m_LODs.clear();
	m_LODs.reserve(1 + lodThresholds.size()); // LOD 0 + generated LODs

	// Add LOD 0 (original mesh) as the first LOD resource
	LODResource lod0_resource;
	lod0_resource.indexCount = static_cast<uint32>(m_indices.size());
	m_LODs.push_back(lod0_resource);

	// Generate simplified LODs using MeshOptimizer
	std::optional<std::vector<MeshOptimizer::LOD>> generatedLODs =
		MeshOptimizer::GenerateLODs(
			*this, // Pass the current Mesh object (which has GetVertices/GetIndices)
			lodThresholds);

	if (generatedLODs.has_value())
	{
		for (uint32_t i = 0; i < generatedLODs->size(); ++i)
		{
			const auto& lod_data = generatedLODs->at(i);
			LODResource lod_resource;
			lod_resource.indexCount = static_cast<uint32>(lod_data.indices.size());
			m_LODs.push_back(lod_resource);
		}
	}
	else
	{
		std::cerr << "Mesh::GenerateLODs: MeshOptimizer failed to generate LODs." << std::endl;
		// If generation fails, m_LODs will only contain LOD 0.
	}
}

// ── Mesh에서 DX11이 사라진 자취 ──
//
// ① 드로우 8종 (T5). Draw ×2 · DrawShadow ×2 · DrawInstanced · DrawLOD ·
//    DrawShadowLOD · DrawInstancedLOD. 유일한 소비자가 PrimitiveRenderProxy의
//    드로우 셋이었고 그 셋도 호출자가 0이었다 - DX11 렌더러가 은퇴하면서
//    함께 도달 불가가 됐다.
//
// ② 정점·인덱스 버퍼 (D4). 예전 주석은 "EffectSystem의 MeshModuleGPU가
//    GetVertexBuffer/GetIndexBuffer로 쓰므로 남는다"였는데, 그 EffectSystem이
//    PHASE 10에서 통째로 걷혔다. 소비자가 함께 사라져 버퍼도 갔다.
//
// ③ 그림자 최적화 계통 (A-③, 2026-08-09). MakeShadowOptimizedBuffer와
//    m_shadow* 여섯.
//
//    ★ 만들기만 하고 아무도 안 그렸다. 게터 둘(GetShadowVertexBuffer/
//      GetShadowIndexBuffer)의 호출자가 0이었고, 더 안쪽을 보면 애초에
//      MakeShadowOptimizedBuffer를 부르는 곳도 0이었으며, 원본이 될
//      m_shadowVertices/m_shadowIndices를 채우는 코드도 없었다
//      (MeshOptimizer는 그림자를 아예 다루지 않는다).
//
//      즉 빈 배열로 크기 0짜리 DX11 버퍼를 만드는 경로였고, 그 죽은 경로
//      하나 때문에 Mesh.cpp가 DeviceState.h를 붙들고 있었다.
//
//      그림자용 저정점 메시가 다시 필요해지면 DX12 쪽에서 CPU 배열을
//      원본으로 새로 세운다 - 여기 남은 껍데기를 되살릴 이유는 없다.

UIMesh::UIMesh()
{
	m_vertices = UIQuad;
	m_indices = UIIndices;
}

UIMesh::~UIMesh()
{

}

