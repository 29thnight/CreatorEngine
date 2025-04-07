#pragma once
#include "Core.Minimal.h"
#include <assimp/Importer.hpp>

struct Node
{
	std::string m_name;
	Mathf::Matrix m_transform{ XMMatrixIdentity() };
	uint32 m_index{};
	uint32 m_parentIndex{};
	std::vector<uint32> m_childrenIndex;
	uint32 m_numChildren{};
	uint32 m_numMeshes{};
	std::vector<uint32> m_meshes;

	Node(const std::string_view& name) : m_name(name) {}
};

struct Vertex
{
	Mathf::Vector3 position;
	Mathf::Vector3 normal;
	Mathf::Vector2 uv;
	Mathf::Vector3 tangent;
	Mathf::Vector3 bitangent;
	Mathf::Vector4 boneIndices;
	Mathf::Vector4 boneWeights;

	Vertex() = default;
	Vertex(
		const Mathf::Vector3& _position, 
		const Mathf::Vector3& _normal, 
		const Mathf::Vector2& _uv, 
		const Mathf::Vector3& _tangent, 
		const Mathf::Vector3& _bitangent, 
		const Mathf::Vector4& _boneIndices, 
		const Mathf::Vector4& _boneWeights
	) :
		position(_position), 
		normal(_normal), 
		uv(_uv), 
		tangent(_tangent), 
		bitangent(_bitangent), 
		boneIndices(_boneIndices), 
		boneWeights(_boneWeights) {}

	Vertex(const Mathf::Vector3& _position, const Mathf::Vector3& _normal, const Mathf::Vector2& _uv) :
		position(_position), normal(_normal), uv(_uv) {}
};

class Texture;
class Material;
class ModelLoader;
class Mesh
{
public:
	Mesh() = default;
	Mesh(const std::string_view& _name, const std::vector<Vertex>& _vertices, const std::vector<uint32>& _indices);
	Mesh(Mesh&& _other) noexcept;
	~Mesh();

	void Draw();
	ID3D11CommandList* Draw(ID3D11DeviceContext* _defferedContext);

	const std::string& GetName() { return m_name; }
private:
	friend class ModelLoader;
	std::string m_name;
	std::vector<Vertex> m_vertices;
	std::vector<uint32> m_indices;
	uint32 m_materialIndex{};
	//Mathf::Matrix m_transform{ XMMatrixIdentity() };

	std::string m_nodeName;

	DirectX::BoundingBox m_boundingBox;
	DirectX::BoundingSphere m_boundingSphere;

	ComPtr<ID3D11Buffer> m_vertexBuffer{};
	ComPtr<ID3D11Buffer> m_indexBuffer{};
	static constexpr uint32 m_stride = sizeof(Vertex);
};

class PrimitiveCreator
{
public:
	static inline std::vector<Vertex> CubeVertices()
	{
		std::vector<Vertex> cube;
		cube.reserve(24);

		cube.push_back({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  0.0f, -1.0f), XMFLOAT2(0.0f, 1.0f) });

		cube.push_back({ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  0.0f,  1.0f), XMFLOAT2(0.0f, 1.0f) });

		cube.push_back({ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(-1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) });

		cube.push_back({ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(1.0f,  0.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) });

		cube.push_back({ XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f, -1.0f,  1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT3(0.0f, -1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) });

		cube.push_back({ XMFLOAT3(-1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(-1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) });
		cube.push_back({ XMFLOAT3(1.0f,  1.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) });

		return cube;
	}

	static inline std::vector<uint32> CubeIndices()
	{
		return std::vector<uint32>
		{
			0, 2, 1, 0, 3, 2,
			4, 6, 5, 4, 7, 6,
			8, 10, 9, 8, 11, 10,
			12, 14, 13, 12, 15, 14,
			16, 18, 17, 16, 19, 18,
			20, 22, 21, 20, 23, 22
		};
	}

	static inline std::vector<Vertex> QuadVertices()
	{
		std::vector<Vertex> quad;
		quad.reserve(4);
		quad.push_back({ XMFLOAT3(-1.0f,  0.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 0.0f) });
		quad.push_back({ XMFLOAT3(-1.0f,  0.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(0.0f, 1.0f) });
		quad.push_back({ XMFLOAT3(1.0f,  0.0f, -1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 1.0f) });
		quad.push_back({ XMFLOAT3(1.0f,  0.0f,  1.0f), XMFLOAT3(0.0f,  1.0f,  0.0f), XMFLOAT2(1.0f, 0.0f) });
		return quad;
	}

	static inline std::vector<uint32> QuadIndices()
	{
		return std::vector<uint32>{ 0, 2, 1, 0, 3, 2 };
	}
};