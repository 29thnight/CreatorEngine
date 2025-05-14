#pragma once
#include "Component.h"
#include "Mesh.h"
#include "ResourceAllocator.h"
#include "TerrainComponent.generated.h"


struct TerrainBrush
{
	enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;

	DirectX::XMFLOAT2 m_center; 
	float m_radius; //�귯���� ������
	float m_strength; //�귯���� ����(��ȭ��)
	float m_flatTargetHeight; //��źȭ�� ����
	uint32_t m_layerID; //�������� ���̾� ���̵�

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
	
	//�ʱ�ȭ
	void Initialize() 
	{
		m_heightMap.assign(m_width * m_height, 0.0f); //0.0f�� �ʱ�ȭ
		m_vNormalMap.assign(m_width * m_height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f }); //normal vector �ʱ�ȭ

		//std::vector<Vertex> vertices;

		//vertices.reserve(m_width * m_height);
		//for (int i = 0; i < m_height; ++i)
		//{
		//	for (int j = 0; j < m_width; ++j)
		//	{
		//		//���� ����
		//		Vertex vertex;
		//		vertex.position = DirectX::XMFLOAT3(static_cast<float>(j), m_heightMap[i * m_width + j], static_cast<float>(i));
		//		vertex.normal = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f); //�ʱ� �븻 ����
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
		//		//���� �ε��� ����
		//		int topLeft = i * m_width + j;
		//		int topRight = topLeft + 1;
		//		int bottomLeft = (i + 1) * m_width + j;
		//		int bottomRight = bottomLeft + 1;
		//		//�ﰢ�� �ε��� ����
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
		//�귯���� �����ϴ� �Լ�
		
		//�귯�� ���� ����
		int minX = std::max(0, static_cast<int>(brush.m_center.x - brush.m_radius));
		int maxX = std::min(m_width - 1, static_cast<int>(brush.m_center.x + brush.m_radius));
		int minY = std::max(0, static_cast<int>(brush.m_center.y - brush.m_radius));
		int maxY = std::min(m_height - 1, static_cast<int>(brush.m_center.y + brush.m_radius));

		//���� ��ȸ
		for (int i = minY; i <= maxY; ++i) 
		{
			for (int j = minX; j <= maxX; ++j) 
			{
				//�귯���� �߽ɰ� ���� ��ǥ�� �Ÿ��� �������� ����
				float distance = sqrtf(powf(brush.m_center.x - j, 2) + powf(brush.m_center.y - i, 2));
				if (distance <= brush.m_radius) 
				{
					//�귯���� ����� �Ÿ� ������ ���� ���� ����
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
						//todo : ���̾� ������
						PaintLayer(brush.m_layerID, j, i, heightChange);
						break;
					}
				}
			}
		}
		//�븻�� ����
		ReClalculateNormalMap();
	}

	//����� ���̸��� ������� �븻���� �ٽ� ���
	void ReClalculateNormalMap() 
	{
		for (int i = 0; i < m_height; ++i) 
		{
			for (int j = 0; j < m_width; ++j) 
			{
				//�븻�� ���
				//�����¿� �������� ����Ͽ� �븻 ���͸� ����
				float heightL = (j > 0) ? m_heightMap[i * m_width + (j - 1)] : m_heightMap[i * m_width + j];
				float heightR = (j < m_width - 1) ? m_heightMap[i * m_width + (j + 1)] : m_heightMap[i * m_width + j];
				float heightD = (i > 0) ? m_heightMap[(i - 1) * m_width + j] : m_heightMap[i * m_width + j];
				float heightU = (i < m_height - 1) ? m_heightMap[(i + 1) * m_width + j] : m_heightMap[i * m_width + j];
				//�븻 ���� ���
				DirectX::XMFLOAT3 normal;
				normal.x = heightL - heightR;
				normal.y = 2.0f; //��������� ����
				normal.z = heightD - heightU;
				//����ȭ
				float length = sqrtf(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
				if (length > 0.0f) 
				{
					normal.x /= length;
					normal.y /= length;
					normal.z /= length;
				}
				//�븻�ʿ� ����
				m_vNormalMap[i * m_width + j] = normal;
			}
		}
	}

	//���̸��� ������� ���̾ ������
	void PaintLayer(uint32_t layerId,int x,int y,float strength) 
	{
		//todo : ���̾� ������
		for (size_t i = 0; i < m_layers.size(); ++i) 
		{
			if (m_layers[i].m_layerID == layerId) 
			{
				//���̾� ������
				int mapIdx = y * m_width + x;
				m_layerHeightMap[i][mapIdx] = std::clamp(m_layerHeightMap[i][mapIdx] + strength, 0.0f, 1.0f);
				break;
			}
		}
	}


	//============================================================
	//HeigtMap�� ������� Collider ������ �ʿ��� �������� ��ȯ
	int GetWidth() const { return m_width; }
	int GetHeight() const { return m_height; }

	float* GetHeightMap() { return m_heightMap.data(); }

	



private:
	//�ͷ����� �����ϴ� ���̵�
	unsigned int m_terrainID{ 0 };
	//���̳ʸ��� ����� ���� ���� ���Ͽ� ���� GUID
	FileGuid m_trrainAssetGuid{};
	//����Ʈ ��
	std::vector<float> m_heightMap;
	//���� �븻��
	std::vector<DirectX::XMFLOAT3> m_vNormalMap;

	//�ؽ��ĸ� ���� ���̾� ������
	std::vector<TerrainLayer> m_layers; 

	//�� ����Ʈ�ɿ� ����ġ�� ��� �ִ� ���̾� ��
	std::vector<std::vector<float>> m_layerHeightMap;

	//������ ���̸��� ��� �ִ� �ؽ��� -> ���� ��� �ұ�?
	float m_textureWidth;
	float m_textureHeight;

	Mesh* m_pMash{ nullptr };
};

//�׽�Ʈ��
//���ߺ��Ϳ� ���� �־�� ���� �ҷ����⸦ �غ���
//������Ƽ �������--> �ȵǸ� .asset ���� � ���̳ʸ��� ���� �����غ���
