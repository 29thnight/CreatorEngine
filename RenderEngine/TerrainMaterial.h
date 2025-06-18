#pragma once
#include "TerrainBuffers.h"
#include "ShaderSystem.h"
//-----------------------------------------------------------------------------
// TerrainMaterial: 지형 재질 클래스
// 지형을 렌더링 하기 위한 텍스처와 셰이더를 포함
// todo: 빌드 시에만 사용 되는 텍스쳐정보를 따로 관리 필요
//-----------------------------------------------------------------------------


class TerrainMaterial
{
public:
	TerrainMaterial() = default;
	~TerrainMaterial() = default;

	//에디터용
	void Initialize(UINT width, UINT height);
	//터레인 레이어 정보를 받아서 터레인 마테리얼에 적용
	void AddLayer(TerrainLayer& newLayer);
	void RemoveLayer(uint32_t layerID);
	void ClearLayers();
	void InitSplatMapTexture(UINT width, UINT height);
	void UpdateSplatMapPatch(int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData);

	void UpdateBuffer(TerrainLayerBuffer layers);

	//터레인 컴포넌트에서 파일로드시 필요 데이터만 받아서 전체 업데이트
	void MateialDataUpdate(
		int width, int height,
		std::vector<TerrainLayer>& layers,
		std::vector<std::vector<float>>& layerHeightMap
	);
	//---------------------------------------------------------------------------------
	//빌드시 : todo 고정된 터레인 텍스쳐를 사용하도록 변경 로드시만 적용 하고 변경 X
	void RunTimeInitialize(UINT width, UINT height);		//사이즈 받아서 고정 크기 스플렛맵 생성
	bool BuildOutMaterial(std::wstring& dir);				//빌드 시에만 사용되는 스플렛맵 및 레이어 정보 저장
	bool LoadRunTimeMaterial(const std::wstring& filePath); //파일 패스 받아서 스플렛맵 및 layer 정보 로드
	//void SetLayerArrayTexture(Texture* srv) { m_layerSRV = srv; } // 레이어 텍스처 배열 SRV 설정
	//----------------------------------------------------------------------------------

	ID3D11ShaderResourceView** GetSplatMapSRV() { return m_splatMapSRV.GetAddressOf(); }
	ID3D11ShaderResourceView** GetLayerSRV() { return &m_layerSRV; }
	// Compute Shader 접근자
	//ComputeShader* GetComputeShader() { return m_computeShader; }

	ID3D11Buffer** GetLayerBuffer() { return m_layerBuffer.GetAddressOf(); }

	ComPtr<ID3D11Texture2D>              m_splatMapTexture; // 스플랫맵 텍스처
	ComPtr<ID3D11ShaderResourceView>     m_splatMapSRV; // 스플랫맵 SRV

	ID3D11Texture2D* m_layerTextureArray = nullptr; // 최대 4개 레이어를 위한 텍스처 배열
	ID3D11UnorderedAccessView* p_outTextureUAV = nullptr; //m_layerTextureArray에 대한 UAV
	ID3D11ShaderResourceView* m_layerSRV = nullptr; // m_layerTextureArray에 대한 SRV

	TerrainLayerBuffer m_layerBufferData; // 레이어 정보 버퍼 데이터
	ComPtr<ID3D11Buffer> m_layerBuffer; // 레이어 정보 버퍼
	ComPtr<ID3D11Buffer> m_AddLayerBuffer; // 레이어 추가용  버퍼

	ShaderPtr<ComputeShader>	         m_computeShader; // 터레인 각 텍스쳐를 -> m_layerTextureArray에 저장하는 컴퓨트 셰이더

	int m_width{ 0}; // 지형 너비
	int m_height{ 0 }; // 지형 높이

	bool useLayer{ false }; // 레이어 사용 여부
};

