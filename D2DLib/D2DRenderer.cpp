#include "pch.h"
#include "D2DRenderer.h"
#include "D2DBitmapScene.h"
#include "../RenderEngine/UI_DataSystem.h"
#include "../RenderEngine/DataSystem.h"
#include "../RenderEngine/ImGuiRegister.h"
#include "../World.h"
#include "../GameManager.h"

#include <memory>

DirectX11::D2DRenderer::D2DRenderer(const std::shared_ptr<DeviceResources>& deviceResources)
	: m_DeviceResources(deviceResources)
{
	{
		D3D11_TEXTURE2D_DESC editordesc = {};
		editordesc.Width = static_cast<UINT>(m_DeviceResources->GetLogicalSize().width);
		editordesc.Height = static_cast<UINT>(m_DeviceResources->GetLogicalSize().height);
		editordesc.MipLevels = 1;
		editordesc.ArraySize = 1;
		editordesc.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
		editordesc.SampleDesc.Count = 1;
		editordesc.SampleDesc.Quality = 0;
		editordesc.Usage = D3D11_USAGE_DEFAULT;
		editordesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
		editordesc.CPUAccessFlags = 0;
		editordesc.MiscFlags = 0;

		DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateTexture2D(&editordesc, nullptr, _EditorOutput.ReleaseAndGetAddressOf()));
		DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateTexture2D(&editordesc, nullptr, _EditorInput.ReleaseAndGetAddressOf()));

		CD3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc(
			D3D11_UAV_DIMENSION::D3D11_UAV_DIMENSION_TEXTURE2D,
			DirectX::NoSRGB(DXGI_FORMAT_B8G8R8A8_UNORM),
			0,
			1
		);

		CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc(
			D3D11_SRV_DIMENSION::D3D11_SRV_DIMENSION_TEXTURE2D,
			DirectX::NoSRGB(DXGI_FORMAT_B8G8R8A8_UNORM),
			0,
			1
		);

		DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateUnorderedAccessView(_EditorOutput.Get(), &uavDesc, _EditorOutputUAV.ReleaseAndGetAddressOf()));
		DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateShaderResourceView(_EditorInput.Get(), &srvDesc, _EditorInputSRV.ReleaseAndGetAddressOf()));
		DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateShaderResourceView(_EditorOutput.Get(), &srvDesc, _EditorOutputSRV.ReleaseAndGetAddressOf()));
	}

	//m_LoadScene = UIObject(UISystem->GetBitmap("loading"));
}

DirectX11::D2DRenderer::~D2DRenderer()
{
}

void DirectX11::D2DRenderer::SetEditorComputeShader()
{
	//_EditorComputeShader = AssetsSystem->ComputeShaders["canvas-edit"];
	//if (_EditorComputeShader.Get() == nullptr)
	//{
	//	Log::Error("Unable to load billboard shaders in D2DRenderer");
	//}
}

void DirectX11::D2DRenderer::UpdateDrawModel()
{
	m_CurrentCanvas = GameManagement->GetCurrWorld()->GetCurCanvas();
	if (m_CurrentCanvas == nullptr)
	{
		return;
	}

	for (auto& D2DScene : m_CurrentCanvas->getObjList())
	{
		D2DScene->Update(0.1f);
		m_drawBitmap.push_back(D2DScene);
		m_drawBitmapSceneCount++;
	}

	for (auto& D2DScene : m_CurrentCanvas->getTextList())
	{
		m_drawText.push_back(D2DScene);
		m_drawBitmapSceneCount++;
	}
}

void DirectX11::D2DRenderer::BeginDraw()
{
	auto* _renderTarget = m_DeviceResources->GetD2DTarget();
	_renderTarget->BeginDraw();
	_renderTarget->Clear();
}

void DirectX11::D2DRenderer::Render()
{
	if(UISystem->IsLoading())
	{
		return;
	}

	auto* _renderTarget = m_DeviceResources->GetD2DTarget();
	_renderTarget->BeginDraw();
	if (m_CurrentCanvas != nullptr)
	{
		for (auto& D2DScene : m_drawBitmap)
		{
			if(D2DScene->m_pBitmap || D2DScene->m_pBitmaps[0])
			{
				D2DScene->Render(_renderTarget);
			}
		}

		for (auto& D2DScene : m_drawText)
		{
			D2DScene->Render(_renderTarget);
		}
	}
	_renderTarget->EndDraw();

	m_drawBitmap.clear();
	m_drawText.clear();
}

void DirectX11::D2DRenderer::EndDraw()
{
	auto* _renderTarget = m_DeviceResources->GetD2DTarget();
	_renderTarget->EndDraw();
}

void DirectX11::D2DRenderer::LoadSceneRender(int levelIndex)
{
	if (UISystem->IsLoading())
	{
		return;
	}

	auto* _renderTarget = m_DeviceResources->GetD2DTarget();
	_renderTarget->BeginDraw();
	_renderTarget->Clear();

	if(0 != levelIndex)
	{
		std::string lv_name = "LoadingTextures (" + std::to_string(levelIndex) + ").png";
		m_LoadScene.SetBitmap(UISystem->GetBitmap(lv_name));
	}

	//UIControl->ResetLayer();
	m_LoadScene.Render(_renderTarget);
	_renderTarget->EndDraw();
}

void DirectX11::D2DRenderer::SetCanvasEditorStage()
{
	m_DeviceResources->GetD3DDeviceContext()->CopyResource(_EditorInput.Get(), m_DeviceResources->GetBackBuffer());
}

void DirectX11::D2DRenderer::ComputeCanvasEditorStage()
{
	/*ID3D11ShaderResourceView* blanksrvs[]{ nullptr };
	ID3D11UnorderedAccessView* blankuavs[]{ nullptr };

	DX::States::Context->CSSetShader(_EditorComputeShader.Get(), nullptr, NULL);

	ID3D11UnorderedAccessView* uavs[]{ _EditorOutputUAV.Get() };
	DX::States::Context->CSSetUnorderedAccessViews(0, 1, uavs, 0);

	ID3D11ShaderResourceView* srvs[]{ _EditorInputSRV.Get() };
	DX::States::Context->CSSetShaderResources(0, 1, srvs);

	uint32 threadsX = (uint32)std::ceilf(m_DeviceResources->GetLogicalSize().width / 16.0f);
	uint32 threadsY = (uint32)std::ceilf(m_DeviceResources->GetLogicalSize().height / 16.0f);

	DX::States::Context->Dispatch(threadsX, threadsY, 1);
	DX::States::Context->CopyResource(_EditorInput.Get(), _EditorOutput.Get());*/

}

void DirectX11::D2DRenderer::ImGuiRenderStage()
{
	if (_world->IsCreateCanvas())
	{
		ImGui::Begin("Canvas Editor");
		ImGui::Image((ImTextureID)_EditorInputSRV.Get(), ImVec2(720, 480));
		ImGui::End();
	}
}
