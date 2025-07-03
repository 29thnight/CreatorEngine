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
// TerrainComponent: ApplyBrush 최적화 버전
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

    // 초기화
    void Initialize();
    
	// 가로 세로 크기 변경
    void Resize(int newWidth, int newHeight);

    virtual void Awake() override;
    virtual void OnDistroy() override;

	//에디터 save/load
    void Save(const std::wstring& assetRoot, const std::wstring& name);
    bool Load(const std::wstring& filePath);

    //Build시 저장/반환
    void BuildOutTrrain(const std::wstring& buildPath, const std::wstring& terrainName);
    bool LoadRunTimeTerrain(const std::wstring& filePath);

    // 브러시 적용
    void ApplyBrush(const TerrainBrush& brush);

    // 변경된 영역(minX..maxX, minY..maxY) 주변 노멀이 올바르게 보이도록 +1 픽셀 확장 하여 노말 재계산
    void RecalculateNormalsPatch(int minX, int minY, int maxX, int maxY);

    // 레이어 페인팅 => splat 맵 업데이트
    void PaintLayer(uint32_t layerId, int x, int y, float strength);

    //레이어 정보 업데이트
    void UpdateLayerDesc(uint32_t layerID);

	//layer 관련 함수 -> m_layers 벡터에 추가/삭제 m_pMaterial->다시로드
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
		return nullptr; // 해당 레이어 ID가 없을 경우 nullptr 반환
		//throw std::runtime_error("Layer ID not found");
	}

	std::vector<const char*> GetLayerNames()
	{
		m_layerNames.clear(); // 이전 이름들 초기화
		for (const auto& desc : m_layers)
		{
			m_layerNames.push_back(desc.layerName.c_str());
		}
		return m_layerNames;
	}
    	
    // 현재 브러시 정보 저장/반환
    void SetTerrainBrush(TerrainBrush* brush) { m_currentBrush = brush; }
    TerrainBrush* GetCurrentBrush() { return m_currentBrush; }

    // Collider용 접근자
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }
    float* GetHeightMap() { return m_heightMap.data(); }

    // Mesh 접근자
    TerrainMesh* GetMesh() const { return m_pMesh; }
	TerrainMaterial* GetMaterial() const { return m_pMaterial; }


    [[Property]]
	FileGuid m_trrainAssetGuid{};// 에셋 가이드
    std::wstring m_terrainTargetPath{};

private:
	uint32 m_terrainID{ 0 }; // 지형 ID
    std::vector<float> m_heightMap;
    std::vector<DirectX::XMFLOAT3> m_vNormalMap;
    std::vector<TerrainLayer>            m_layers; // 레이어 정보들
    std::vector<std::vector<float>>      m_layerHeightMap; // 레이어별 높이 맵 가중치 (각 레이어마다 m_width * m_height 크기의 벡터를 가짐)

    // 지형 메시를 한 덩어리로 가진다면, 필요 시 분할 대응 가능
    TerrainMesh* m_pMesh{ nullptr };
    TerrainMaterial* m_pMaterial{ nullptr }; // 지형 재질 layer의 => texture, 셰이더 등
    TerrainBrush* m_currentBrush{ nullptr };

    float m_minHeight{ -100.0f }; // 최소 높이 
    float m_maxHeight{ 500.0f }; // 최대 높이
    //todo: 인스펙터 및 높이 수정에 적용 안함 세이브로드 작업 이후 적용

    ThreadPool<std::function<void()>> m_threadPool; //이미지 세이브,로딩시 사용할 쓰레드 풀//component 생성시 4개 

    //== 에디터 전용
    //== window 공용으로 사용가능
    uint32                               m_selectedLayerID{ 0xFFFFFFFF }; // 선택된 레이어 ID (0xFFFFFFFF는 선택 안됨을 의미)
    //== window 공용으로 사용 불가능
	std::vector<const char*>             m_layerNames; // 레이어 이름들 (디버깅용)
    //== 에디터 전용
    uint32                               m_nextLayerID{ 0 }; // 다음 레이어 ID

    void SaveEditorHeightMap(const std::wstring& pngPath, float minH, float maXH);
    bool LoadEditorHeightMap(file::path& pngPath, float dataWidth, float dataHeight, float minH, float maXH, std::vector<float>& out);

    void SaveEditorSplatMap(const std::wstring& pngPath);
    bool LoadEditorSplatMap(file::path& pngPath, float dataWidth, float dataHeight, std::vector<std::vector<float>>& out);
};
