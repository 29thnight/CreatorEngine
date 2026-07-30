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

	// 위 상태 객체의 소유 여부.
	//
	// 대부분의 패스는 전역 공유 상태(DeviceStates->g_pBlendState 등)나 자신의 ComPtr
	// 멤버(.Get())를 그대로 대입한다. 이런 비소유 참조를 소멸자에서 해제하면 이중 해제가
	// 되므로, 직접 만들어 넘긴 경우에만 Adopt*()로 소유권을 표시하고 그때만 해제한다.
	bool m_ownsRasterizerState{ false };
	bool m_ownsBlendState{ false };
	bool m_ownsDepthStencilState{ false };

	D3D11_PRIMITIVE_TOPOLOGY m_primitiveTopology{ D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST };

	std::vector<std::shared_ptr<Sampler>> m_samplers;

	Core::DelegateHandle m_shaderReloadEventHandle;
	InputLayOutContainer m_inputLayoutDescContainer;
	bool m_isShaderPSO{ false };
public:
	PipelineStateObject();
	PipelineStateObject(bool isShaderPSO);
	~PipelineStateObject();

	// 상태 객체의 소유권을 인수한다. 기존에 소유하던 것이 있으면 먼저 해제한다.
	// 직접 Create*State로 만든 객체를 넘길 때 사용한다.
	void AdoptRasterizerState(ID3D11RasterizerState* state);
	void AdoptBlendState(ID3D11BlendState* state);
	void AdoptDepthStencilState(ID3D11DepthStencilState* state);

	void Apply();
	void Apply(ID3D11DeviceContext* deferredContext);
	void CreateInputLayout();
	void CreateInputLayout(InputLayOutContainer&& vertexLayoutDesc);
	void ReloadShaders();
    void Reset();
	void Reset(ID3D11DeviceContext* deferredContext);
};
#else
class PipelineStateObject {};
#endif // !DYNAMICCPP_EXPORTS