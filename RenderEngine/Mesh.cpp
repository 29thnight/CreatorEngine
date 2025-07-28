#include "Mesh.h"
#include "DeviceState.h"
#include "Camera.h"
#include "MeshOptimizer.h"

// Helper to create D3D11 buffers for a given LOD
void CreateLODBuffers(
	const std::vector<Vertex>& vertices,
	const std::vector<uint32>& indices,
	Mesh::LODResource& outLODResource,
	const std::string& meshName,
	uint32_t lodIndex)
{
	outLODResource.indexCount = static_cast<uint32>(indices.size());

	// Create Vertex Buffer
	outLODResource.vertexBuffer = DirectX11::CreateBuffer(
		sizeof(Vertex) * vertices.size(),
		D3D11_BIND_VERTEX_BUFFER,
		vertices.data());
	DirectX::SetName(outLODResource.vertexBuffer.Get(), meshName + "LOD" + std::to_string(lodIndex) + "VertexBuffer");

	// Create Index Buffer
	outLODResource.indexBuffer = DirectX11::CreateBuffer(
		sizeof(uint32) * indices.size(),
		D3D11_BIND_INDEX_BUFFER,
		indices.data());
	DirectX::SetName(outLODResource.indexBuffer.Get(), meshName + "LOD" + std::to_string(lodIndex) + "IndexBuffer");
}

Mesh::Mesh(const std::string_view& _name, const std::vector<Vertex>& _vertices, const std::vector<uint32>& _indices) :
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

	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(Vertex) * m_vertices.size(), D3D11_BIND_VERTEX_BUFFER, m_vertices.data());
	DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_indices.size(), D3D11_BIND_INDEX_BUFFER, m_indices.data());
	DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
}

Mesh::Mesh(const std::string_view& _name, std::vector<Vertex>&& _vertices, std::vector<uint32>&& _indices) :
	m_name(_name), m_vertices(std::move(_vertices)), m_indices(std::move(_indices))
{
	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(Vertex) * m_vertices.size(), D3D11_BIND_VERTEX_BUFFER, m_vertices.data());
	DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_indices.size(), D3D11_BIND_INDEX_BUFFER, m_indices.data());
	DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
}

Mesh::Mesh(Mesh&& _other) noexcept :
	m_vertices(std::move(_other.m_vertices)),
	m_indices(std::move(_other.m_indices)),
	m_vertexBuffer(std::move(_other.m_vertexBuffer)),
	m_indexBuffer(std::move(_other.m_indexBuffer)),
	m_name(std::move(_other.m_name)),
	m_materialIndex(_other.m_materialIndex),
	m_boundingBox(_other.m_boundingBox),
	m_boundingSphere(_other.m_boundingSphere),
	m_shadowVertices(std::move(_other.m_shadowVertices)),
	m_shadowIndices(std::move(_other.m_shadowIndices)),
	m_shadowVertexBuffer(std::move(_other.m_shadowVertexBuffer)),
	m_shadowIndexBuffer(std::move(_other.m_shadowIndexBuffer)),
	m_isShadowOptimized(_other.m_isShadowOptimized),
	m_hashingMesh(_other.m_hashingMesh),
	m_modelName(std::move(_other.m_modelName))
{
}

Mesh::~Mesh()
{
}

void Mesh::AssetInit()
{
	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(Vertex) * m_vertices.size(), D3D11_BIND_VERTEX_BUFFER, m_vertices.data());
	DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_indices.size(), D3D11_BIND_INDEX_BUFFER, m_indices.data());
	DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
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
	lod0_resource.vertexBuffer = m_vertexBuffer; // Use existing LOD 0 buffer
	lod0_resource.indexBuffer = m_indexBuffer;   // Use existing LOD 0 buffer
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
			CreateLODBuffers(lod_data.vertices, lod_data.indices, lod_resource, m_name, i + 1); // i+1 for LOD index
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

	// Get camera's view and projection matrices
	Mathf::Matrix viewMatrix = camera->CalculateView();
	Mathf::Matrix projectionMatrix = camera->CalculateProjection();

	// Get camera's world position
	Mathf::Vector3 cameraPosition = camera->m_eyePosition;

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

	// Get screen height from camera (assuming GetScreenSize returns DirectX11::Sizef with width/height)
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

void Mesh::Draw()
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);
}

void Mesh::Draw(ID3D11DeviceContext* _deferredContext)
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(_deferredContext, 0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(_deferredContext, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(_deferredContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(_deferredContext, m_indices.size(), 0, 0);
}

void Mesh::DrawShadow()
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(0, 1, m_shadowVertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_shadowIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_shadowIndices.size(), 0, 0);
}

void Mesh::DrawShadow(ID3D11DeviceContext* _deferredContext)
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(_deferredContext, 0, 1, m_shadowVertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(_deferredContext, m_shadowIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(_deferredContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(_deferredContext, m_shadowIndices.size(), 0, 0);
}

void Mesh::DrawInstanced(ID3D11DeviceContext* _deferredContext, size_t instanceCount)
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(_deferredContext, 0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(_deferredContext, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(_deferredContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexedInstanced(_deferredContext, m_indices.size(), static_cast<UINT>(instanceCount), 0, 0, 0);
}

void Mesh::MakeShadowOptimizedBuffer()
{
	m_shadowVertexBuffer = DirectX11::CreateBuffer(sizeof(Vertex) * m_shadowVertices.size(), D3D11_BIND_VERTEX_BUFFER, m_shadowVertices.data());
	DirectX::SetName(m_shadowVertexBuffer.Get(), m_name + "ShadowVertexBuffer");
	m_shadowIndexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_shadowIndices.size(), D3D11_BIND_INDEX_BUFFER, m_shadowIndices.data());
	DirectX::SetName(m_shadowIndexBuffer.Get(), m_name + "ShadowIndexBuffer");
	m_isShadowOptimized = true;
}

void Mesh::DrawLOD(ID3D11DeviceContext* context, uint32_t lodIndex)
{
	if (lodIndex >= m_LODs.size())
	{
		lodIndex = 0; // Fallback to LOD 0 if index is out of bounds
	}

	const LODResource& currentLOD = m_LODs[lodIndex];

	UINT offset = 0;
	DirectX11::IASetVertexBuffers(context, 0, 1, currentLOD.vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(context, currentLOD.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(context, currentLOD.indexCount, 0, 0);
}

void Mesh::DrawShadowLOD(ID3D11DeviceContext* context, uint32_t lodIndex)
{
	// If shadow optimized buffers exist, use them regardless of LOD index for simplicity
	// This assumes shadow meshes are pre-generated and don't use dynamic LODs from main mesh.
	// If shadow LODs are desired, m_shadowLODs would be needed.
	if (m_isShadowOptimized && m_shadowVertexBuffer && m_shadowIndexBuffer)
	{
		UINT offset = 0;
		DirectX11::IASetVertexBuffers(context, 0, 1, m_shadowVertexBuffer.GetAddressOf(), &m_stride, &offset);
		DirectX11::IASetIndexBuffer(context, m_shadowIndexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		DirectX11::IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DirectX11::DrawIndexed(context, static_cast<uint32_t>(m_shadowIndices.size()), 0, 0);
	}
	else if (lodIndex < m_LODs.size()) // Fallback to main LOD buffers if no shadow-specific LODs
	{
		const LODResource& currentLOD = m_LODs[lodIndex];
		UINT offset = 0;
		DirectX11::IASetVertexBuffers(context, 0, 1, currentLOD.vertexBuffer.GetAddressOf(), &m_stride, &offset);
		DirectX11::IASetIndexBuffer(context, currentLOD.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		DirectX11::IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DirectX11::DrawIndexed(context, currentLOD.indexCount, 0, 0);
	}
	else // No shadow-optimized or valid LOD, use LOD 0 main buffers
	{
		UINT offset = 0;
		DirectX11::IASetVertexBuffers(context, 0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
		DirectX11::IASetIndexBuffer(context, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		DirectX11::IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DirectX11::DrawIndexed(context, static_cast<uint32_t>(m_indices.size()), 0, 0);
	}
}

void Mesh::DrawInstancedLOD(ID3D11DeviceContext* context, uint32_t lodIndex, size_t instanceCount)
{
	if (lodIndex >= m_LODs.size())
	{
		lodIndex = 0; // Fallback to LOD 0 if index is out of bounds
	}

	const LODResource& currentLOD = m_LODs[lodIndex];

	UINT offset = 0;
	DirectX11::IASetVertexBuffers(context, 0, 1, currentLOD.vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(context, currentLOD.indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexedInstanced(context, currentLOD.indexCount, static_cast<UINT>(instanceCount), 0, 0, 0);
}

UIMesh::UIMesh()
{
	m_vertices = UIQuad;
	m_indices = UIIndices;
	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(UIvertex) * m_vertices.size(), D3D11_BIND_VERTEX_BUFFER, m_vertices.data());
	DirectX::SetName(m_vertexBuffer.Get(), m_name + "VertexBuffer");
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * m_indices.size(), D3D11_BIND_INDEX_BUFFER, m_indices.data());
	DirectX::SetName(m_indexBuffer.Get(), m_name + "IndexBuffer");
}

UIMesh::~UIMesh()
{

}

void UIMesh::Draw()
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);
}

void UIMesh::Draw(ID3D11DeviceContext* _deferredContext)
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(_deferredContext, 0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(_deferredContext, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(_deferredContext, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(_deferredContext, m_indices.size(), 0, 0);
}