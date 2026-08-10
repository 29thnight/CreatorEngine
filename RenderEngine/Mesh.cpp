#include "Mesh.h"
#include "Camera.h"
#include "RenderPassData.h"
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

uint32_t Mesh::SelectLOD(Camera* camera, const Mathf::Matrix& worldMatrix) const
{
	if (m_LODs.empty() || m_LODThresholds.empty() || nullptr == camera)
	{
		return 0; // No LODs or invalid camera, return LOD 0
	}

	// 프레임 밀봉된 카메라 스냅샷을 쓴다(PHASE 3-2). LOD 판정은 렌더 스레드에서
	// 도는데, 여기서 살아 있는 카메라를 다시 읽으면 게임 스레드가 카메라를
	// 움직이는 중일 때 프록시마다 다른 카메라를 보게 된다.
	if (!RenderPassData::VaildCheck(camera))
	{
		return 0;
	}
	const RenderPassData* renderData = RenderPassData::GetData(camera);

	// Get camera's view and projection matrices
	Mathf::Matrix viewMatrix = renderData->GetFrameSnapshot().view;
	Mathf::Matrix projectionMatrix = renderData->GetFrameSnapshot().projection;

	// Get camera's world position
	Mathf::Vector3 cameraPosition = renderData->GetFrameSnapshot().eyePosition;

	// Calculate bounding sphere in world space using the provided world matrix
	// Assuming m_boundingBox and m_boundingSphere are in object-local space
	DirectX::BoundingSphere worldBoundingSphere{};
	DirectX::BoundingSphere::CreateFromBoundingBox(worldBoundingSphere, m_boundingBox);

	// Transform local bounding sphere center to world space
	worldBoundingSphere.Center = Mathf::Vector3::Transform(m_boundingSphere.Center, worldMatrix);

	// Calculate world-space radius by scaling the local radius by the maximum scale factor
	// Extract scale from the world matrix (assuming no shear)
	float scaleX = Mathf::Vector3(worldMatrix._11, worldMatrix._12, worldMatrix._13).Length();
	float scaleY = Mathf::Vector3(worldMatrix._21, worldMatrix._22, worldMatrix._23).Length();
	float scaleZ = Mathf::Vector3(worldMatrix._31, worldMatrix._32, worldMatrix._33).Length();
	float maxScale = std::max({ scaleX, scaleY, scaleZ });
	worldBoundingSphere.Radius = m_boundingSphere.Radius * maxScale;

	// Calculate distance from camera to object's bounding sphere center
	float distance = Mathf::Vector3::Distance(cameraPosition, worldBoundingSphere.Center);

	// Avoid division by zero or very small numbers
	if (distance < 0.001f)
	{
		return 0; // Very close, use highest LOD
	}

	// Get projection matrix's Y-scale (cot(FOV_Y / 2))
	float projectionYScale = projectionMatrix.m[1][1]; // Assuming projectionMatrix is in DirectX format

	// Get screen height from camera (assuming GetScreenSize returns Core::Sizef with width/height)
	float screenHeight = camera->GetScreenSize().height;

	// Calculate screen-space size
	// This formula is (worldRadius / distance) * projectionYScale * (screenHeight / 2)
	// The (screenHeight / 2) part is often implicitly handled by the thresholds
	// if thresholds are defined in terms of NDC space (0-2 range).
	// Let's use the NDC-space screen-space size for comparison with thresholds.
	float screenSpaceSize = (worldBoundingSphere.Radius / distance) * projectionYScale;

	// Iterate through thresholds to find the appropriate LOD
	// Assuming m_LODThresholds are ordered from largest to smallest.
	// m_LODThresholds[0] is the threshold for LOD 1.
	// If screenSpaceSize is greater than m_LODThresholds[0], it means we should use LOD 0.
	if (!m_LODThresholds.empty() && screenSpaceSize > m_LODThresholds[0])
	{
		return 0; // Use LOD 0 (highest detail)
	}

	for (uint32_t i = 0; i < m_LODThresholds.size(); ++i)
	{
		if (screenSpaceSize > m_LODThresholds[i])
		{
			// Found a threshold, return this LOD level
			// If m_LODThresholds[i] is the threshold for LOD i+1, then return i+1.
			return i + 1;
		}
	}

	// If no threshold is met, return the lowest detail LOD available
	// This would be m_LODs.size() - 1
	return static_cast<uint32_t>(m_LODs.size() - 1);
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

