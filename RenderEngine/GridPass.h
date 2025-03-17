#pragma once
#include "IRenderPass.h"
#include "Texture.h"

struct alignas(16) GridConstantBuffer
{
    Mathf::Matrix m_viewProj;
};

// Vertex 구조체 정의
struct alignas(16) GridVertex
{
    XMFLOAT3 pos;
};

class GridPass final : public IRenderPass
{
public:
	GridPass();
	~GridPass();
	void Initialize(Texture* color, Texture* grid);
	void Execute(Scene& scene) override;

    ID3D11ShaderResourceView* GetGridSRV() const { return m_gridTexture->m_pSRV; }

private:
    Texture* m_colorTexture;
    Texture* m_gridTexture;
    GridConstantBuffer m_gridConstant;
    ComPtr<ID3D11Buffer> m_pGridConstantBuffer;
    ComPtr<ID3D11Buffer> m_pVertexBuffer;
};
