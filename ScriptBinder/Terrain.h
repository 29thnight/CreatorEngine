#pragma once
#include "../Utility_Framework/Core.Thread.hpp"
#include "../Utility_Framework/Core.Minimal.h"
#include "Component.h"
#include "ResourceAllocator.h"
#include "TerrainCollider.h"
#include "TerrainComponent.generated.h"
#include "ShaderSystem.h"
#include "IOnDistroy.h"
#include "IAwakable.h"
#include "TerrainMesh.h"
#include "TerrainMaterial.h"

//-----------------------------------------------------------------------------
// TerrainComponent: ApplyBrush ����ȭ ����
//-----------------------------------------------------------------------------
class ComponentFactory;
class TerrainComponent : public Component, public IAwakable, public IOnDistroy
{
public:
    ReflectTerrainComponent
    [[Serializable(Inheritance:Component)]]
    TerrainComponent();
    virtual ~TerrainComponent() = default;

    [[Property]] 
    int m_width{ 1000 };
    [[Property]] 
    int m_height{ 1000 };

    // �ʱ�ȭ
    void Initialize();
    
	// ���� ���� ũ�� ����
    void Resize(int newWidth, int newHeight);

    virtual void Awake() override;
    virtual void OnDistroy() override;

	//������ save/load
    void Save(const std::wstring& assetRoot, const std::wstring& name);
    bool Load(const std::wstring& filePath);

    //Build�� ����/��ȯ
    void BuildOutTrrain(const std::wstring& buildPath, const std::wstring& terrainName);
    bool LoadRunTimeTerrain(const std::wstring& filePath);

    // �귯�� ����
    void ApplyBrush(const TerrainBrush& brush);

    // ����� ����(minX..maxX, minY..maxY) �ֺ� ����� �ùٸ��� ���̵��� +1 �ȼ� Ȯ�� �Ͽ� �븻 ����
    void RecalculateNormalsPatch(int minX, int minY, int maxX, int maxY);

    // ���̾� ������ => splat �� ������Ʈ
    void PaintLayer(uint32_t layerId, int x, int y, float strength);

    //���̾� ���� ������Ʈ
    void UpdateLayerDesc(uint32_t layerID);

	//layer ���� �Լ� -> m_layers ���Ϳ� �߰�/���� m_pMaterial->�ٽ÷ε�
	void AddLayer(const std::wstring& path, const std::wstring& diffuseFile, float tilling);
    void RemoveLayer(uint32_t layerID);
	void ClearLayers();

    std::vector<TerrainLayer> GetLayerCount() const { return m_layers; }
	TerrainLayer* GetLayerDesc(uint32_t layerID)
	{
		for (auto& desc : m_layers)
		{
			if (desc.m_layerID == layerID)
			{
				return &desc;
			}
		}
		return nullptr; // �ش� ���̾� ID�� ���� ��� nullptr ��ȯ
		//throw std::runtime_error("Layer ID not found");
	}

	std::vector<const char*> GetLayerNames()
	{
		m_layerNames.clear(); // ���� �̸��� �ʱ�ȭ
		for (const auto& desc : m_layers)
		{
			m_layerNames.push_back(desc.layerName.c_str());
		}
		return m_layerNames;
	}
    	
    // ���� �귯�� ���� ����/��ȯ
    void SetTerrainBrush(TerrainBrush* brush) { m_currentBrush = brush; }
    TerrainBrush* GetCurrentBrush() { return m_currentBrush; }

    // Collider�� ������
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }
    float* GetHeightMap() { return m_heightMap.data(); }

    // Mesh ������
    TerrainMesh* GetMesh() const { return m_pMesh; }
	TerrainMaterial* GetMaterial() const { return m_pMaterial; }


    [[Property]]
	FileGuid m_trrainAssetGuid{};// ���� ���̵�
    std::wstring m_terrainTargetPath{};

private:
	uint32 m_terrainID{ 0 }; // ���� ID
    std::vector<float> m_heightMap;
    std::vector<DirectX::XMFLOAT3> m_vNormalMap;
    std::vector<TerrainLayer>            m_layers; // ���̾� ������
    std::vector<std::vector<float>>      m_layerHeightMap; // ���̾ ���� �� ����ġ (�� ���̾�� m_width * m_height ũ���� ���͸� ����)

    // ���� �޽ø� �� ����� �����ٸ�, �ʿ� �� ���� ���� ����
    TerrainMesh* m_pMesh{ nullptr };
    TerrainMaterial* m_pMaterial{ nullptr }; // ���� ���� layer�� => texture, ���̴� ��
    TerrainBrush* m_currentBrush{ nullptr };

    float m_minHeight{ -100.0f }; // �ּ� ���� 
    float m_maxHeight{ 500.0f }; // �ִ� ����
    //todo: �ν����� �� ���� ������ ���� ���� ���̺�ε� �۾� ���� ����

    ThreadPool<std::function<void()>> m_threadPool; //�̹��� ���̺�,�ε��� ����� ������ Ǯ//component ������ 4�� 

    //== ������ ����
    //== window �������� ��밡��
    uint32                               m_selectedLayerID{ 0xFFFFFFFF }; // ���õ� ���̾� ID (0xFFFFFFFF�� ���� �ȵ��� �ǹ�)
    //== window �������� ��� �Ұ���
	std::vector<const char*>             m_layerNames; // ���̾� �̸��� (������)
    //== ������ ����
    uint32                               m_nextLayerID{ 0 }; // ���� ���̾� ID

    void SaveEditorHeightMap(const std::wstring& pngPath, float minH, float maXH);
    bool LoadEditorHeightMap(file::path& pngPath, float dataWidth, float dataHeight, float minH, float maXH, std::vector<float>& out);

    void SaveEditorSplatMap(const std::wstring& pngPath);
    bool LoadEditorSplatMap(file::path& pngPath, float dataWidth, float dataHeight, std::vector<std::vector<float>>& out);
};
