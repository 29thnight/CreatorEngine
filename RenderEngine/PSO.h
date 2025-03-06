#pragma once
#include "Core.Minimal.h"
#include "Shader.h"

class PipelineStateObject
{
public:
	VertexShader*	m_vertexShader;
	PixelShader*	m_pixelShader;
	GeometryShader* m_geometryShader;
	HullShader*		m_hullShader;
	DomainShader*   m_domainShader;
	ComputeShader*	m_computeShader;

	ID3D11InputLayout*		 m_inputLayout{ nullptr };
	ID3D11RasterizerState*	 m_rasterizerState{ nullptr };
	ID3D11BlendState*		 m_blendState{ nullptr };
	ID3D11DepthStencilState* m_depthStencilState{ nullptr };

	D3D11_PRIMITIVE_TOPOLOGY m_primitiveTopology{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };

public:
	void Apply();
};

//how to apply change to deffered context -> command list -> immediate context logic
//1. create a PSO
//2. set the PSO
//3. apply the PSO
//4. draw
//5. reset the PSO
//6. repeat 2-5
// �̰� ���ϰ� �Ϸ��� Ŀ��� ����Ʈ�� ������ Ŭ������ �ʿ��ϰ�, imidiate context�� ������� ���� ������ Ŭ������ �ʿ��ϴ�.
// ���ҰŴ�. �׳� �������� ������Ƿ� �ּ����� �ۼ��Ѵ�.
// �ᱹ �� ������ 1�����Ӿ� ������ �������� �ϰ� �ȴ�.