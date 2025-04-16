#include "UIPass.h"
#include "ShaderSystem.h"
#include "ImGuiRegister.h"
#include "Mesh.h"
#include "Scene.h"
#include "../ScriptBinder/GameObject.h"
#include "../ScriptBinder/UIManager.h"
#include "../ScriptBinder/Canvas.h"
UIPass::UIPass()
{

	m_pso = std::make_unique<PipelineStateObject>();
	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["UI"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["UI"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	InputLayOutContainer vertexLayoutDesc =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

	CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DeviceState::g_pBlendState;

	m_UIBuffer = DirectX11::CreateBuffer(sizeof(UiInfo), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void UIPass::Initialize(Texture* renderTargetView, SpriteBatch* spriteBatch)
{
	m_renderTarget = renderTargetView;
	m_spriteBatch = spriteBatch;
}


void UIPass::Execute(RenderScene& scene, Camera& camera)
{

	auto deviceContext = DeviceState::g_pDeviceContext;
	m_pso->Apply();
	m_spriteBatch->Begin();
	ID3D11RenderTargetView* view = camera.m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(1, &view, nullptr);
	
	deviceContext->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
	deviceContext->OMSetBlendState(DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
	camera.UpdateBuffer();

	DirectX11::VSSetConstantBuffer(0,1,m_UIBuffer.GetAddressOf());

	
	std::vector<Canvas*> canvases;
	/*for (auto& Canvas2 : UIManagers->Canvases)
	{
		canvases.push_back(Canvas2->GetComponent<Canvas>());
	}
	std::sort(canvases.begin(), canvases.end(), [](Canvas* a, Canvas* b) {
		return a->CanvasOrder < b->CanvasOrder;
		});*/

	for (auto& Canvases : UIManagers->Canvases)
	{
		Canvas* canvas = Canvases->GetComponent<Canvas>();
		if (false == canvas->IsEnabled()) continue;
		for (auto& uiObj : canvas->UIObjs)
		{
			UIComponent* ui = uiObj->GetComponent<UIComponent>();
			if (ui && ui->IsEnabled())
			{
				_2DObjects.push_back(ui);
			}
			TextComponent* text = uiObj->GetComponent<TextComponent>();
			if (text && text->IsEnabled())
			{
				_TextObjects.push_back(text);
			}
		}
	}


	std::sort(_2DObjects.begin(), _2DObjects.end(), [](UIComponent* a, UIComponent* b) {
		if (a->_layerorder != b->_layerorder)
			return a->_layerorder < b->_layerorder;

		// 동일한 layer일 경우, CanvasOrder 기준으로 비교
		auto aCanvas = a->GetOwnerCanvas(); 
		auto bCanvas = b->GetOwnerCanvas();

		int aOrder = aCanvas ? aCanvas->CanvasOrder : 0;
		int bOrder = bCanvas ? bCanvas->CanvasOrder : 0;

		return aOrder < bOrder;
		});

	for (auto& Uiobject : _2DObjects)
	{
		DirectX11::PSSetShaderResources(0, 1, &Uiobject->m_curtexture->m_pSRV);
		DirectX11::UpdateBuffer(m_UIBuffer.Get(), &Uiobject->uiinfo);
		Uiobject->m_UIMesh->Draw();
	}
	for (auto& Textobject : _TextObjects)
	{
		Textobject->Draw(m_spriteBatch);
	}

	DirectX11::OMSetDepthStencilState(DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets();
	_2DObjects.clear();
	m_spriteBatch->End();
}

bool UIPass::compareLayer(int  a, int  b)
{
	return a  < b ;
}

void UIPass::ControlPanel()
{
}

void UIPass::Resize()
{
}

