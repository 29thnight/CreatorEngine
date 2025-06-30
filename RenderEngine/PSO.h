#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"
#include "Shader.h"
#include "Sampler.h"

using InputLayOutContainer = std::vector<D3D11_INPUT_ELEMENT_DESC>;

class PipelineStateObject
{
public:
	ShaderPtr<VertexShader>		m_vertexShader;
	ShaderPtr<PixelShader>		m_pixelShader;
	ShaderPtr<GeometryShader>	m_geometryShader;
	ShaderPtr<HullShader>		m_hullShader;
	ShaderPtr<DomainShader>		m_domainShader;
	ShaderPtr<ComputeShader>	m_computeShader;

	ID3D11InputLayout*		 m_inputLayout{ nullptr };
	ID3D11RasterizerState*	 m_rasterizerState{ nullptr };
	ID3D11BlendState*		 m_blendState{ nullptr };
	ID3D11DepthStencilState* m_depthStencilState{ nullptr };

	D3D11_PRIMITIVE_TOPOLOGY m_primitiveTopology{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };

	std::vector<std::shared_ptr<Sampler>> m_samplers;

	Core::DelegateHandle m_shaderReloadEventHandle;
	InputLayOutContainer m_inputLayoutDescContainer;
public:
	PipelineStateObject();
	~PipelineStateObject();

	void Apply();
	void Apply(ID3D11DeviceContext* defferdContext);
	void CreateInputLayout();
	void CreateInputLayout(InputLayOutContainer&& vertexLayoutDesc);
	void ReloadShaders();
    void Reset();
};
#endif // !DYNAMICCPP_EXPORTS