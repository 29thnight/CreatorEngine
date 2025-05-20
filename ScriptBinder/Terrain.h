//Terrain Component
//���������� ��� �ִ� ������Ʈ
//�����ϴ� �� �𸣰ڰ� �ϴ� ������ ��� ����
//������ ������ �ִ� ���� ������� �����ϰ� ���������� �Ѱ��ְ�
//������ �ý���/�Ŵ������� ó���ϵ��� ����
//�׷��ٸ� Terrain�� ������ ������ �־�� �ұ�?
//������ ���̸ʰ� �ؽ��� ����

#pragma once
#include "Component.h"
#include "Mesh.h"
#include "ResourceAllocator.h"
#include "TerrainCollider.h"
#include "TerrainComponent.generated.h"

struct TerrainBrush 
{
	//�ͷ����� �����Ҽ� �ִ� �귯��
	//brush ��� / ��� / �ϰ� / ��źȭ / ������
	enum class Mode { Raise, Lower, Flatten, PaintLayer } m_mode;

	DirectX::XMFLOAT2 m_center; //�귯���� �߽� ��ǥ-->���콺�� ���Ͽ� ȭ�� ��ǥ�迡�� �޾ƿ;���
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

class Terrain : public Component
{
public:
   ReflectTerrain
	[[Serializable(Inheritance:Component)]]
   TerrainComponent() {
	   m_name = "TerrainComponent"; m_typeID = TypeTrait::GUIDCreator::GetTypeID<TerrainComponent>();
	   Initialize();
   } virtual ~TerrainComponent() = default;

	[[Property]]
	int m_width{ 1000 };
	[[Property]]
	int m_height{ 1000 };
	
	//�ʱ�ȭ
	void Initialize() 
	{
		m_heightMap.assign(m_width * m_height, 0.0f); //0.0f�� �ʱ�ȭ
		m_vNormalMap.assign(m_width * m_height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f }); //normal vector �ʱ�ȭ
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
};

//�׽�Ʈ��
//���ߺ��Ϳ� ���� �־�� ���� �ҷ����⸦ �غ���
//������Ƽ �������--> �ȵǸ� .asset ���� � ���̳ʸ��� ���� �����غ���
