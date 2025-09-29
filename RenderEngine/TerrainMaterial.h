#pragma once
#include "TerrainBuffers.h"
#include "ShaderSystem.h"
#include <vector>

//-----------------------------------------------------------------------------
// TerrainMaterial: 지형 재질 클래스
// 지형을 렌더링 하기 위한 텍스처와 셰이더를 포함
// todo: 빌드 시에만 사용 되는 텍스쳐정보를 따로 관리 필요
//-----------------------------------------------------------------------------

class Texture;
class TerrainMaterial
{
public:
	TerrainMaterial() = default;
	~TerrainMaterial() = default;

	void Initialize(UINT width, UINT height);
	void UpdateBuffer();
	void ClearLayers();

	// GPU 리소스와 직접 통신하는 함수들
	void InitSplatMapTextureArray(UINT width, UINT height, UINT layerCount);
	void UpdateSplatMapPatch(UINT layerIndex, int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData);
	void UpdateBuffer(const TerrainLayerBuffer& layers);

	// CPU 데이터로부터 모든 GPU 리소스를 재구성하는 메인 함수
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
	
	// --- Getter 함수들 ---
	ID3D11ShaderResourceView** GetSplatMapSRV() { return m_splatMapSRV.GetAddressOf(); }
	ID3D11ShaderResourceView** GetLayerSRV() { return &m_layerSRV; }
	ID3D11Buffer** GetLayerBuffer() { return m_layerBuffer.GetAddressOf(); }
	
	// 스플랫맵을 단일 텍스처 배열로 관리
	ComPtr<ID3D11Texture2D>          m_splatMapTextureArray;
	ComPtr<ID3D11ShaderResourceView> m_splatMapSRV;

	// 레이어 Albedo 텍스처 배열 관련 리소스
	ID3D11Texture2D* m_layerTextureArray = nullptr;
	ID3D11UnorderedAccessView* p_outTextureUAV = nullptr;
	ID3D11ShaderResourceView* m_layerSRV = nullptr;

	// 상수 버퍼
	TerrainLayerBuffer m_layerBufferData;
	ComPtr<ID3D11Buffer> m_layerBuffer;
	ComPtr<ID3D11Buffer> m_AddLayerBuffer; // 컴퓨트 셰이더용

	// 리사이징을 위한 컴퓨트 셰이더
	ShaderPtr<ComputeShader> m_computeShader;

	int m_width{ 0 };
	int m_height{ 0 };
};

