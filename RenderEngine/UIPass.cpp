#include "UIPass.h"
#include <DirectXTK/SpriteBatch.h>
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
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DeviceState::g_pBlendState;

	m_UIBuffer = DirectX11::CreateBuffer(sizeof(ImageInfo), D3D11_BIND_CONSTANT_BUFFER, nullptr);
}

void UIPass::Initialize(Texture* renderTargetView)
{
	m_renderTarget = renderTargetView;
	m_spriteBatch = std::make_unique<DirectX::SpriteBatch>(DeviceState::g_pDeviceContext);
	m_commonStates = std::make_unique<DirectX::CommonStates>(DeviceState::g_pDevice);
}

void UIPass::SortUIObjects()
{
	std::vector<Canvas*> canvases;
	size_t index = m_frame.load(std::memory_order_relaxed) % 3;
	auto& imageQueue = _ImageObjects[index];
	auto& textQueue = _TextObjects[index];

	for (auto it = UIManagers->Canvases.begin(); it != UIManagers->Canvases.end(); )
	{
		if (auto canvasObj = it->lock())
		{
			Canvas* canvas = canvasObj->GetComponent<Canvas>();
			if (false == canvas->IsEnabled()) { ++it; continue; }
			for (auto& uiObj : canvas->UIObjs)
			{
				std::vector<UIComponent*> uicom = uiObj->GetComponents<UIComponent>();
				for (auto& ui : uicom)
				{
					if (ui->IsEnabled() == false) continue;
					switch (ui->type)
					{
					case UItype::Image:
					{
						if (auto* img = dynamic_cast<ImageComponent*>(ui))
							imageQueue.push_back(img);
						break;
					}
					case UItype::Text:
					{
						if (auto* txt = dynamic_cast<TextComponent*>(ui))
							textQueue.push_back(txt);
						break;
					}
					default:
						break;
					}
				}

			}
			++it;
		}
		else
		{
			it = UIManagers->Canvases.erase(it);
		}
	}

	std::ranges::sort(imageQueue, UIComponent::CompareLayerOrder);
	std::ranges::sort(textQueue, UIComponent::CompareLayerOrder);
}

void UIPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void UIPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);

	size_t prevIndex = size_t(m_frame.load(std::memory_order_relaxed) + 1) % 3;
	auto& imageQueue = _ImageObjects[prevIndex];
	auto& textQueue = _TextObjects[prevIndex];

	if (imageQueue.empty() && textQueue.empty()) return;

	ID3D11DeviceContext* deferredPtr = deferredContext;

	m_pso->Apply(deferredPtr);

	auto spriteBatch = std::make_unique<SpriteBatch>(deferredPtr);

	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(deferredPtr, 1, &view, renderData->m_renderTarget->m_pDSV);
	DirectX11::RSSetViewports(deferredPtr, 1, &DeviceState::g_Viewport);
	DirectX11::OMSetDepthStencilState(deferredPtr, m_NoWriteDepthStencilState.Get(), 1);
	DirectX11::OMSetBlendState(deferredPtr, DeviceState::g_pBlendState, nullptr, 0xFFFFFFFF);
	camera.UpdateBuffer(deferredPtr);

	DirectX11::VSSetConstantBuffer(deferredPtr, 0, 1, m_UIBuffer.GetAddressOf());
	spriteBatch->Begin(DirectX::SpriteSortMode_FrontToBack,
		m_commonStates->NonPremultiplied(), m_commonStates->LinearClamp());

	for (auto& imageObject : imageQueue)
	{
		imageObject->Draw(spriteBatch);
	}

	for (auto& textObject : textQueue)
	{
		textObject->Draw(spriteBatch);
	}

	spriteBatch->End();
	DirectX11::OMSetDepthStencilState(deferredPtr, DeviceState::g_pDepthStencilState, 1);
	DirectX11::OMSetBlendState(deferredPtr, nullptr, nullptr, 0xFFFFFFFF);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
	DirectX11::UnbindRenderTargets(deferredPtr);

	ID3D11CommandList* commandList{};
	DirectX11::ThrowIfFailed(
		deferredPtr->FinishCommandList(FALSE, &commandList)
	);
	PushQueue(camera.m_cameraIndex, commandList);

	imageQueue.clear();
	textQueue.clear();
}

void UIPass::ClearFrameQueue()
{
	size_t prevIndex = size_t(m_frame.load(std::memory_order_relaxed) + 1) % 3;
	auto& imageQueue = _ImageObjects[prevIndex];
	auto& textQueue = _TextObjects[prevIndex];

	imageQueue.clear();
	textQueue.clear();
}

bool UIPass::compareLayer(int  a, int  b)
{
	return a  < b ;
}

void UIPass::ControlPanel()
{
}

void UIPass::Resize(uint32_t width, uint32_t height)
{
}

