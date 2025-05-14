#pragma once
#include "Component.h"
#include "Mesh.h"
#include "ResourceAllocator.h"
#include "TerrainComponent.generated.h"


struct TerrainBrush
{
	enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;

	DirectX::XMFLOAT2 m_center; 
	float m_radius; //브러쉬의 반지름
	float m_strength; //브러쉬의 세기(변화량)
	float m_flatTargetHeight; //평탄화할 높이
	uint32_t m_layerID; //페인팅할 레이어 아이디

	void SetBrushMode(Mode mode) { m_mode = mode; }
};

struct TerrainLayer
{
	uint32_t m_layerID{ 0 };
	ID3D11ShaderResourceView* diffuseTexture{ nullptr };
	ID3D11ShaderResourceView* normalTexture{ nullptr };
	float fileFactor{ 1.0f };
};

class TerrainComponent : public Component
{
public:
   ReflectTerrainComponent
	[[Serializable(Inheritance:Component)]]
	GENERATED_BODY(TerrainComponent)

	[[Property]]
	int m_width{ 1000 };
	[[Property]]
	int m_height{ 1000 };
	
	//초기화
	void Initialize() 
	{
		m_heightMap.assign(m_width * m_height, 0.0f); //0.0f로 초기화
		m_vNormalMap.assign(m_width * m_height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f }); //normal vector 초기화

		//std::vector<Vertex> vertices;

		//vertices.reserve(m_width * m_height);
		//for (int i = 0; i < m_height; ++i)
		//{
		//	for (int j = 0; j < m_width; ++j)
		//	{
		//		//정점 생성
		//		Vertex vertex;
		//		vertex.position = DirectX::XMFLOAT3(static_cast<float>(j), m_heightMap[i * m_width + j], static_cast<float>(i));
		//		vertex.normal = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f); //초기 노말 벡터
		//		vertex.uv0 = DirectX::XMFLOAT2(static_cast<float>(j) / (m_width - 1), static_cast<float>(i) / (m_height - 1));
		//		vertices.push_back(vertex);
		//	}
		//}
		//
		//std::vector<uint32_t> indices;
		//indices.reserve((m_width - 1) * (m_height - 1) * 6);
		//for (int i = 0; i < m_height - 1; ++i)
		//{
		//	for (int j = 0; j < m_width - 1; ++j)
		//	{
		//		//정점 인덱스 생성
		//		int topLeft = i * m_width + j;
		//		int topRight = topLeft + 1;
		//		int bottomLeft = (i + 1) * m_width + j;
		//		int bottomRight = bottomLeft + 1;
		//		//삼각형 인덱스 생성
		//		indices.push_back(topLeft);
		//		indices.push_back(bottomLeft);
		//		indices.push_back(topRight);
		//		indices.push_back(topRight);
		//		indices.push_back(bottomLeft);
		//		indices.push_back(bottomRight);
		//	}
		//}

		//std::string name = "TerrainMesh"+ std::to_string(m_terrainID);

		//m_pMash = AllocateResource<Mesh>(name, vertices, indices);
	}

	void ApplyBrush(const TerrainBrush& brush)
	{
		//브러쉬를 적용하는 함수
		
		//브러쉬 적용 범위
		int minX = std::max(0, static_cast<int>(brush.m_center.x - brush.m_radius));
		int maxX = std::min(m_width - 1, static_cast<int>(brush.m_center.x + brush.m_radius));
		int minY = std::max(0, static_cast<int>(brush.m_center.y - brush.m_radius));
		int maxY = std::min(m_height - 1, static_cast<int>(brush.m_center.y + brush.m_radius));

		//범위 순회
		for (int i = minY; i <= maxY; ++i) 
		{
			for (int j = minX; j <= maxX; ++j) 
			{
				//브러쉬의 중심과 현재 좌표의 거리로 원형으로 적용
				float distance = sqrtf(powf(brush.m_center.x - j, 2) + powf(brush.m_center.y - i, 2));
				if (distance <= brush.m_radius) 
				{
					//브러쉬의 세기와 거리 비율에 따라 높이 변경
					float heightChange = brush.m_strength * (1.0f - (distance / brush.m_radius));
					switch (brush.m_mode) 
					{
					case TerrainBrush::Mode::Raise:
						m_heightMap[i * m_width + j] += heightChange;
						break;
					case TerrainBrush::Mode::Lower:
						m_heightMap[i * m_width + j] -= heightChange;
						break;
					case TerrainBrush::Mode::Flatten:
						m_heightMap[i * m_width + j] = brush.m_flatTargetHeight;
						break;
					case TerrainBrush::Mode::PaintLayer:
						//todo : 레이어 페인팅
						PaintLayer(brush.m_layerID, j, i, heightChange);
						break;
					}
				}
			}
		}
		//노말맵 재계산
		ReClalculateNormalMap();
	}

	//변경된 높이맵을 기반으로 노말맵을 다시 계산
	void ReClalculateNormalMap() 
	{
		for (int i = 0; i < m_height; ++i) 
		{
			for (int j = 0; j < m_width; ++j) 
			{
				//노말맵 계산
				//상하좌우 높이차를 계산하여 노말 벡터를 구함
				float heightL = (j > 0) ? m_heightMap[i * m_width + (j - 1)] : m_heightMap[i * m_width + j];
				float heightR = (j < m_width - 1) ? m_heightMap[i * m_width + (j + 1)] : m_heightMap[i * m_width + j];
				float heightD = (i > 0) ? m_heightMap[(i - 1) * m_width + j] : m_heightMap[i * m_width + j];
				float heightU = (i < m_height - 1) ? m_heightMap[(i + 1) * m_width + j] : m_heightMap[i * m_width + j];
				//노말 벡터 계산
				DirectX::XMFLOAT3 normal;
				normal.x = heightL - heightR;
				normal.y = 2.0f; //상수값으로 설정
				normal.z = heightD - heightU;
				//정규화
				float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
				if (length > 0.0f) 
				{
					normal.x /= length;
					normal.y /= length;
					normal.z /= length;
				}
				//노말맵에 저장
				m_vNormalMap[i * m_width + j] = normal;
			}
		}
	}

	//높이맵을 기반으로 레이어를 페인팅
	void PaintLayer(uint32_t layerId,int x,int y,float strength) 
	{
		//todo : 레이어 페인팅
		for (size_t i = 0; i < m_layers.size(); ++i) 
		{
			if (m_layers[i].m_layerID == layerId) 
			{
				//레이어 페인팅
				int mapIdx = y * m_width + x;
				m_layerHeightMap[i][mapIdx] = std::clamp(m_layerHeightMap[i][mapIdx] + strength, 0.0f, 1.0f);
				break;
			}
		}
	}


	//============================================================
	//HeigtMap을 기반으로 Collider 생성에 필요한 정보들을 반환
	int GetWidth() const { return m_width; }
	int GetHeight() const { return m_height; }

	float* GetHeightMap() { return m_heightMap.data(); }

	



private:
	//터레인을 구분하는 아이디
	unsigned int m_terrainID{ 0 };
	//바이너리로 저장된 지형 정보 파일에 대한 GUID
	FileGuid m_trrainAssetGuid{};
	//헤이트 맵
	std::vector<float> m_heightMap;
	//계산된 노말맵
	std::vector<DirectX::XMFLOAT3> m_vNormalMap;

	//텍스쳐를 가진 레이어 인포고
	std::vector<TerrainLayer> m_layers; 

	//각 헤이트맴에 가중치를 들고 있는 레이업 맵
	std::vector<std::vector<float>> m_layerHeightMap;

	//지형의 높이맵을 담고 있는 텍스쳐 -> 예는 어떻게 할까?
	float m_textureWidth;
	float m_textureHeight;

	Mesh* m_pMash{ nullptr };
};

//테스트로
//이중벡터에 값을 넣어보고 저장 불러오기를 해보자
//프로퍼티 등록으로--> 안되면 .asset 파일 등에 바이너리로 따로 저장해보자
