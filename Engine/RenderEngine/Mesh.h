#pragma once
#include "Reflection.hpp" // CT3: was transitive via Core.Minimal.h
#include "Core.Minimal.h"
#include "MetaPolymorphic.h"
#include "EngineResourceCensus.h"
#include <mathematics/bounds.hpp>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/vector2.hpp>
#include <mathematics/vector3.hpp>
#include <mathematics/vector4.hpp>
#include <cstddef>
#include <type_traits>

struct ModelNode : public meta::polymorphic
{
	std::string m_name;
	math::matrix4x4 m_transform{ math::matrix4x4::identity() };
	uint32 m_index{};
	uint32 m_parentIndex{};
	std::vector<uint32> m_childrenIndex;
	uint32 m_numChildren{};
	uint32 m_numMeshes{};
	std::vector<uint32> m_meshes;

	ModelNode() = default;
	ModelNode(std::string_view name) : m_name(name) {}
};

struct Vertex
{
	math::vector3 position{};
	math::vector3 normal{};
	math::vector2 uv0{};
	math::vector2 uv1{};
	math::vector3 tangent{};
	math::vector3 bitangent{};
	math::vector4 boneIndices{};
	math::vector4 boneWeights{};

	Vertex() = default;
	Vertex(
		const math::vector3& _position,
		const math::vector3& _normal,
		const math::vector2& _uv0,
		const math::vector2& _uv1,
		const math::vector3& _tangent,
		const math::vector3& _bitangent,
		const math::vector4& _boneIndices,
		const math::vector4& _boneWeights
	) :
		position(_position), 
		normal(_normal), 
		uv0(_uv0),
		uv1(_uv1),
		tangent(_tangent), 
		bitangent(_bitangent), 
		boneIndices(_boneIndices), 
		boneWeights(_boneWeights) {}

	Vertex(const math::vector3& _position, const math::vector3& _normal, const math::vector2& _uv) :
		position(_position), normal(_normal), uv0(_uv) {}

};

static_assert(std::is_same_v<decltype(ModelNode::m_transform), math::matrix4x4>);
static_assert(sizeof(math::matrix4x4) == 64u);
static_assert(std::is_standard_layout_v<Vertex>);
static_assert(std::is_trivially_copyable_v<Vertex>);
static_assert(alignof(Vertex) == alignof(float));
static_assert(sizeof(Vertex) == 96u, "legacy GPU vertex stride changed");
static_assert(offsetof(Vertex, position) == 0u);
static_assert(offsetof(Vertex, normal) == 12u);
static_assert(offsetof(Vertex, uv0) == 24u);
static_assert(offsetof(Vertex, uv1) == 32u);
static_assert(offsetof(Vertex, tangent) == 40u);
static_assert(offsetof(Vertex, bitangent) == 52u);
static_assert(offsetof(Vertex, boneIndices) == 64u);
static_assert(offsetof(Vertex, boneWeights) == 80u);

class Texture;
class Material;
class ModelLoader;
class MeshOptimizer;
class Camera;
class Mesh : public meta::polymorphic, public std::enable_shared_from_this<Mesh>,
	private Diagnostics::CountedResource<Diagnostics::EngineResource::Mesh>
{
   public:
   static consteval auto reflect()
   {
       using Self = Mesh;
       return meta::schema<Self>(
           meta::field<&Self::m_name>,
           meta::field<&Self::m_materialIndex>,
           meta::field<&Self::m_LODThresholds>);
   }
public:
	// 각 LOD 레벨의 GPU 리소스를 관리하는 구조체
	struct LODResource
	{
		uint32 indexCount;
	};

public:
	Mesh() = default;
	Mesh(std::string_view _name, const std::vector<Vertex>& _vertices, const std::vector<uint32>& _indices);
	Mesh(std::string_view _name, std::vector<Vertex>&& _vertices, std::vector<uint32>&& _indices);
	Mesh(Mesh&& _other) noexcept;
	~Mesh();

	bool operator==(const Mesh& _other) const
	{
		return m_hashingMesh == _other.m_hashingMesh;
	}

	void AssetInit();
	// DX11 드로우 8종은 T5에서, 정점·인덱스 버퍼는 D4에서, 그림자 최적화
	// 계통은 여기(A-③)에서 걷었다 — .cpp 주석 참고.
	// 이제 Mesh는 CPU 배열이 전부다. GPU 올리기는 DX12 쪽 소유다.

	// LOD 생성 함수
	bool HasLODs() const;
	void GenerateLODs(const std::vector<float>& lodThresholds);
	const std::vector<float>& GetLODThresholds() const { return m_LODThresholds; }

	std::string GetName() const { return m_name; }
	std::string GetModelName() const { return m_modelName; }
	uint32 GetMaterialIndex() const { return m_materialIndex; }

	const std::vector<Vertex>& GetVertices() { return m_vertices; }
	const std::vector<uint32>& GetIndices() { return m_indices; }

	const math::aabb& GetBoundingBox() const noexcept { return m_boundingBox; }
	const math::sphere& GetBoundingSphere() const noexcept { return m_boundingSphere; }

	/// 정점에서 로컬 바운드를 다시 계산한다. ModelLoader가 임포트 때 하는
	/// 계산과 같다 — 로더를 거치지 않는 절차 생성 메시는 바운드가 기본값
	/// (empty AABB/반지름 0)으로 남아 그림자 캐스터 컬링 같은 판정이 조용히 틀어진다.
	void RecalculateBounds();

	std::vector<Vertex> GetVertices() const{ return m_vertices; }
	std::vector<uint32> GetIndices() const { return m_indices; }
	uint32 GetStride()  { return m_stride; }

	HashedGuid m_hashingMesh{ make_guid() };

private:
	friend class MeshOptimizer;

	std::string m_name;

	uint32 m_materialIndex{};

	std::string m_modelName;

	std::vector<Vertex> m_vertices;
	std::vector<uint32> m_indices;

	math::aabb m_boundingBox{};
	math::sphere m_boundingSphere{};
	static_assert(std::is_same_v<decltype(m_boundingBox), math::aabb>);
	static_assert(std::is_same_v<decltype(m_boundingSphere), math::sphere>);
	static_assert(sizeof(math::aabb) == 24u);
	static_assert(sizeof(math::sphere) == 16u);

	// --- LOD 관련 멤버 변수 ---
	// 인덱스 0: 원본(LOD0), 1: LOD1, ...
	std::vector<LODResource> m_LODs;
	// 화면 공간 크기 기반의 LOD 전환 임계값
	std::vector<float> m_LODThresholds;
	// ---

	static constexpr const uint32 m_stride = sizeof(Vertex);
};

class PrimitiveCreator
{
public:
	static inline std::vector<Vertex> CubeVertices()
	{
		std::vector<Vertex> cube;
		cube.reserve(24);

		cube.push_back({ { -1.0f, -1.0f, -1.0f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 0.0f } });
		cube.push_back({ {  1.0f, -1.0f, -1.0f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 0.0f } });
		cube.push_back({ {  1.0f,  1.0f, -1.0f }, {  0.0f,  0.0f, -1.0f }, { 1.0f, 1.0f } });
		cube.push_back({ { -1.0f,  1.0f, -1.0f }, {  0.0f,  0.0f, -1.0f }, { 0.0f, 1.0f } });

		cube.push_back({ {  1.0f, -1.0f,  1.0f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 0.0f } });
		cube.push_back({ { -1.0f, -1.0f,  1.0f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 0.0f } });
		cube.push_back({ { -1.0f,  1.0f,  1.0f }, {  0.0f,  0.0f,  1.0f }, { 1.0f, 1.0f } });
		cube.push_back({ {  1.0f,  1.0f,  1.0f }, {  0.0f,  0.0f,  1.0f }, { 0.0f, 1.0f } });

		cube.push_back({ { -1.0f, -1.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } });
		cube.push_back({ { -1.0f, -1.0f, -1.0f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } });
		cube.push_back({ { -1.0f,  1.0f, -1.0f }, { -1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } });
		cube.push_back({ { -1.0f,  1.0f,  1.0f }, { -1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } });

		cube.push_back({ {  1.0f, -1.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 0.0f } });
		cube.push_back({ {  1.0f, -1.0f,  1.0f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 0.0f } });
		cube.push_back({ {  1.0f,  1.0f,  1.0f }, {  1.0f,  0.0f,  0.0f }, { 1.0f, 1.0f } });
		cube.push_back({ {  1.0f,  1.0f, -1.0f }, {  1.0f,  0.0f,  0.0f }, { 0.0f, 1.0f } });

		cube.push_back({ { -1.0f, -1.0f, -1.0f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 0.0f } });
		cube.push_back({ { -1.0f, -1.0f,  1.0f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 0.0f } });
		cube.push_back({ {  1.0f, -1.0f,  1.0f }, {  0.0f, -1.0f,  0.0f }, { 1.0f, 1.0f } });
		cube.push_back({ {  1.0f, -1.0f, -1.0f }, {  0.0f, -1.0f,  0.0f }, { 0.0f, 1.0f } });

		cube.push_back({ { -1.0f,  1.0f,  1.0f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 0.0f } });
		cube.push_back({ { -1.0f,  1.0f, -1.0f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 0.0f } });
		cube.push_back({ {  1.0f,  1.0f, -1.0f }, {  0.0f,  1.0f,  0.0f }, { 1.0f, 1.0f } });
		cube.push_back({ {  1.0f,  1.0f,  1.0f }, {  0.0f,  1.0f,  0.0f }, { 0.0f, 1.0f } });

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
		quad.push_back({ { -1.0f, 0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } });
		quad.push_back({ { -1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } });
		quad.push_back({ {  1.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } });
		quad.push_back({ {  1.0f, 0.0f,  1.0f }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } });
		return quad;
	}

	static inline std::vector<uint32> QuadIndices()
	{
		return std::vector<uint32>{ 0, 2, 1, 0, 3, 2 };
	}
};

struct UIvertex
{
	math::vector3 position{};
	math::vector2 texCoord{};
};

static_assert(std::is_standard_layout_v<UIvertex>);
static_assert(std::is_trivially_copyable_v<UIvertex>);
static_assert(sizeof(UIvertex) == 20u);
static_assert(offsetof(UIvertex, position) == 0u);
static_assert(offsetof(UIvertex, texCoord) == 12u);

class UIMesh
{
public:
	UIMesh();
	~UIMesh();


	const std::string& GetName() { return m_name; }
private:
	friend class ModelLoader;
	std::string m_name;
	std::vector<UIvertex> m_vertices;
	std::vector<uint32> m_indices;
	uint32 m_materialIndex{};

	std::string m_nodeName;

	// UIMesh는 아직 bounds를 계산하거나 직렬화하지 않는다. 미계산 AABB는
	// Mathematics의 empty sentinel로, sphere는 radius 0으로 명시한다.
	math::aabb m_boundingBox{};
	math::sphere m_boundingSphere{};

	static constexpr uint32 m_stride = sizeof(UIvertex);

	std::vector<UIvertex> UIQuad
	{
		{ {-1.0f,  1.0f, 0.0f}, { 0.0f, 0.0f} },  // 좌상단
		{ { 1.0f,  1.0f, 0.0f}, { 1.0f, 0.0f} },   // 우상단
		{ { 1.0f, -1.0f, 0.0f}, { 1.0f, 1.0f} },   // 우하단
		{ {-1.0f, -1.0f, 0.0f}, { 0.0f, 1.0f} },    // 좌하단
	};
	std::vector<uint32> UIIndices = { 0, 1, 2, 0, 2, 3 };
};
