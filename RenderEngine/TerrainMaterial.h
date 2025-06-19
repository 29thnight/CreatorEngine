#pragma once
#include "TerrainBuffers.h"
#include "ShaderSystem.h"
//-----------------------------------------------------------------------------
// TerrainMaterial: ���� ���� Ŭ����
// ������ ������ �ϱ� ���� �ؽ�ó�� ���̴��� ����
// todo: ���� �ÿ��� ��� �Ǵ� �ؽ��������� ���� ���� �ʿ�
//-----------------------------------------------------------------------------


class TerrainMaterial
{
public:
	TerrainMaterial() = default;
	~TerrainMaterial() = default;

	//�����Ϳ�
	void Initialize(UINT width, UINT height);
	//�ͷ��� ���̾� ������ �޾Ƽ� �ͷ��� ���׸��� ����
	void AddLayer(TerrainLayer& newLayer);
	void RemoveLayer(uint32_t layerID);
	void ClearLayers();
	void InitSplatMapTexture(UINT width, UINT height);
	void UpdateSplatMapPatch(int offsetX, int offsetY, int patchW, int patchH, std::vector<BYTE>& patchData);

	void UpdateBuffer(TerrainLayerBuffer layers);

	//�ͷ��� ������Ʈ���� ���Ϸε�� �ʿ� �����͸� �޾Ƽ� ��ü ������Ʈ
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

	ID3D11ShaderResourceView** GetSplatMapSRV() { return m_splatMapSRV.GetAddressOf(); }
	ID3D11ShaderResourceView** GetLayerSRV() { return &m_layerSRV; }
	// Compute Shader ������
	//ComputeShader* GetComputeShader() { return m_computeShader; }

	ID3D11Buffer** GetLayerBuffer() { return m_layerBuffer.GetAddressOf(); }

	ComPtr<ID3D11Texture2D>              m_splatMapTexture; // ���÷��� �ؽ�ó
	ComPtr<ID3D11ShaderResourceView>     m_splatMapSRV; // ���÷��� SRV

	ID3D11Texture2D* m_layerTextureArray = nullptr; // �ִ� 4�� ���̾ ���� �ؽ�ó �迭
	ID3D11UnorderedAccessView* p_outTextureUAV = nullptr; //m_layerTextureArray�� ���� UAV
	ID3D11ShaderResourceView* m_layerSRV = nullptr; // m_layerTextureArray�� ���� SRV

	TerrainLayerBuffer m_layerBufferData; // ���̾� ���� ���� ������
	ComPtr<ID3D11Buffer> m_layerBuffer; // ���̾� ���� ����
	ComPtr<ID3D11Buffer> m_AddLayerBuffer; // ���̾� �߰���  ����

	ShaderPtr<ComputeShader>	         m_computeShader; // �ͷ��� �� �ؽ��ĸ� -> m_layerTextureArray�� �����ϴ� ��ǻƮ ���̴�

	int m_width{ 0}; // ���� �ʺ�
	int m_height{ 0 }; // ���� ����

	bool useLayer{ false }; // ���̾� ��� ����
};

