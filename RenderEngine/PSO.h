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
// 이걸 편리하게 하려면 커멘드 리스트를 관리할 클래스가 필요하고, imidiate context를 스레드로 빼서 관리할 클래스가 필요하다.
// 안할거다. 그냥 깨달음을 얻었으므로 주석으로 작성한다.
// 결국 이 로직은 1프레임씩 지연된 렌더링을 하게 된다.