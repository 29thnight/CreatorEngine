#pragma once
#include "TerrainBuffers.h"
#include "ShaderSystem.h"
#include <vector>

//-----------------------------------------------------------------------------
// TerrainMaterial: ���� ���� Ŭ����
// ������ ������ �ϱ� ���� �ؽ�ó�� ���̴��� ����
// todo: ���� �ÿ��� ��� �Ǵ� �ؽ��������� ���� ���� �ʿ�
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

	// GPU ���ҽ��� ���� ����ϴ� �Լ���
	void InitSplatMapTextureArray(UINT width, UINT height, UINT layerCount);
	void UpdateSplatMapPatch(UINT layerIndex, int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData);
	void UpdateBuffer(const TerrainLayerBuffer& layers);

	// CPU �����ͷκ��� ��� GPU ���ҽ��� �籸���ϴ� ���� �Լ�
	void MateialDataUpdate(
		int width, int height,
		std::vector<TerrainLayer>& layers,
		std::vector<std::vector<float>>& layerHeightMap
	);

	//---------------------------------------------------------------------------------
	//����� : todo ������ �ͷ��� �ؽ��ĸ� ����ϵ��� ���� �ε�ø� ���� �ϰ� ���� X
	void RunTimeInitialize(UINT width, UINT height);		//������ �޾Ƽ� ���� ũ�� ���÷��� ����
	bool BuildOutMaterial(std::wstring& dir);				//���� �ÿ��� ���Ǵ� ���÷��� �� ���̾� ���� ����
	bool LoadRunTimeMaterial(const std::wstring& filePath); //���� �н� �޾Ƽ� ���÷��� �� layer ���� �ε�
	//void SetLayerArrayTexture(Texture* srv) { m_layerSRV = srv; } // ���̾� �ؽ�ó �迭 SRV ����
	//----------------------------------------------------------------------------------
	
	// --- Getter �Լ��� ---
	ID3D11ShaderResourceView** GetSplatMapSRV() { return m_splatMapSRV.GetAddressOf(); }
	ID3D11ShaderResourceView** GetLayerSRV() { return &m_layerSRV; }
	ID3D11Buffer** GetLayerBuffer() { return m_layerBuffer.GetAddressOf(); }
	
	// ���÷����� ���� �ؽ�ó �迭�� ����
	ComPtr<ID3D11Texture2D>          m_splatMapTextureArray;
	ComPtr<ID3D11ShaderResourceView> m_splatMapSRV;

	// ���̾� Albedo �ؽ�ó �迭 ���� ���ҽ�
	ID3D11Texture2D* m_layerTextureArray = nullptr;
	ID3D11UnorderedAccessView* p_outTextureUAV = nullptr;
	ID3D11ShaderResourceView* m_layerSRV = nullptr;

	// ��� ����
	TerrainLayerBuffer m_layerBufferData;
	ComPtr<ID3D11Buffer> m_layerBuffer;
	ComPtr<ID3D11Buffer> m_AddLayerBuffer; // ��ǻƮ ���̴���

	// ������¡�� ���� ��ǻƮ ���̴�
	ShaderPtr<ComputeShader> m_computeShader;

	int m_width{ 0 };
	int m_height{ 0 };
};

