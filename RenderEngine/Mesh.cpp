#include "Mesh.h"
#include "DeviceState.h"

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

Mesh::Mesh(Mesh&& _other) noexcept :
	m_vertices(std::move(_other.m_vertices)),
	m_indices(std::move(_other.m_indices)),
	m_vertexBuffer(std::move(_other.m_vertexBuffer)),
	m_indexBuffer(std::move(_other.m_indexBuffer))
{
}

Mesh::~Mesh()
{
}

void Mesh::Draw()
{
	UINT offset = 0;
	DirectX11::IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	DirectX11::IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);

}

ID3D11CommandList* Mesh::Draw(ID3D11DeviceContext* _defferedContext)
{
	UINT offset = 0;
	_defferedContext->IASetVertexBuffers(0, 1, m_vertexBuffer.GetAddressOf(), &m_stride, &offset);
	_defferedContext->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
	_defferedContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_defferedContext->DrawIndexed(m_indices.size(), 0, 0);

	ID3D11CommandList* commandList;
	_defferedContext->FinishCommandList(false, &commandList);

	return commandList;
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
	//DirectX11::IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	DirectX11::DrawIndexed(m_indices.size(), 0, 0);
}
