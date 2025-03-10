#pragma once
#include "Core.Minimal.h"
#include "DeviceResources.h"

namespace DeviceState
{
	extern ID3D11Device3* g_pDevice;
	extern ID3D11DeviceContext3* g_pDeviceContext;
	extern ID3D11DepthStencilView* g_pDepthStencilView;
	extern DirectX11::Sizef g_ClientRect;
	extern float g_aspectRatio;
}

namespace DirectX11
{
	inline ID3D11Buffer* CreateBuffer(uint32 size, D3D11_BIND_FLAG bindFlag, const void* data = nullptr)
	{
		if (!DeviceState::g_pDevice)
		{
			Log::Error("[RenderEngine] -> Device is not initialized");
			return nullptr;
		}

		CD3D11_BUFFER_DESC bufferDesc{
			size,
			(uint32)bindFlag
		};
		ID3D11Buffer* buffer{};
		if (data)
		{
			D3D11_SUBRESOURCE_DATA initData{};
			initData.pSysMem = data;
			DirectX11::ThrowIfFailed(
				DeviceState::g_pDevice->CreateBuffer(
					&bufferDesc,
					&initData,
					&buffer
				)
			);
		}
		else
		{
			DirectX11::ThrowIfFailed(
				DeviceState::g_pDevice->CreateBuffer(
					&bufferDesc,
					nullptr,
					&buffer
				)
			);
		}
		return buffer;
	}

	inline void CreateSamplerState(D3D11_SAMPLER_DESC* pSamplerDesc, ID3D11SamplerState** ppSamplerState)
	{
		if (!DeviceState::g_pDevice)
		{
			Log::Error("[RenderEngine] -> Device is not initialized");
			return;
		}
		DirectX11::ThrowIfFailed(
			DeviceState::g_pDevice->CreateSamplerState(
				pSamplerDesc,
				ppSamplerState
			)
		);
	}

	inline float GetAspectRatio()
	{
		return DeviceState::g_aspectRatio;
	}

	inline float GetWidth()
	{
		return DeviceState::g_ClientRect.width;
	}

	inline float GetHeight()
	{
		return DeviceState::g_ClientRect.height;
	}

	//[unsafe]
	inline void UpdateBuffer(ID3D11Buffer* buffer, const void* data)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->UpdateSubresource(buffer, 0, nullptr, data, 0, 0);
	}

	//[unsafe]
	inline void VSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const * buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->VSSetConstantBuffers(slot, 1, buffer);
	}

	//[unsafe]
	inline void IASetVertexBuffers(uint32 startSlot, uint32 numBuffers, ID3D11Buffer* const* buffer, const uint32* stride, const uint32* offset)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->IASetVertexBuffers(startSlot, numBuffers, buffer, stride, offset);
	}

	//[unsafe]
	inline void IASetIndexBuffer(ID3D11Buffer* buffer, DXGI_FORMAT format, uint32 offset)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->IASetIndexBuffer(buffer, format, offset);
	}

	//[unsafe]
	inline void DrawIndexed(uint32 indexCount, uint32 startIndexLocation, int baseVertexLocation)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
	}

	//[unsafe]
	inline void ClearRenderTargetView(ID3D11RenderTargetView* renderTargetView, const float color[4])
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->ClearRenderTargetView(renderTargetView, color);
	}

	//[unsafe]
	inline void ClearDepthStencilView(ID3D11DepthStencilView* depthStencilView, uint32 clearFlags, float depth, uint8 stencil)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->ClearDepthStencilView(depthStencilView, clearFlags, depth, stencil);
	}

	//[unsafe]
	inline void OMSetRenderTargets(uint32 numViews, ID3D11RenderTargetView* const* renderTargetViews, ID3D11DepthStencilView* depthStencilView)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->OMSetRenderTargets(numViews, renderTargetViews, depthStencilView);
	}

	//[unsafe]
	inline void PSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->PSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[unsafe]
	inline void PSSetShaderResources(uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Log::Error("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->PSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}

    //[unsafe]
    inline void Draw(uint32 vertexCount, uint32 startVertexLocation)
    {
        if (!DeviceState::g_pDeviceContext)
        {
            Log::Error("[RenderEngine] -> DeviceContext is not initialized");
            return;
        }
        DeviceState::g_pDeviceContext->Draw(vertexCount, startVertexLocation);
    }

    //[unsafe]
    inline void UnbindRenderTargets()
    {
        if (!DeviceState::g_pDeviceContext)
        {
            Log::Error("[RenderEngine] -> DeviceContext is not initialized");
            return;
        }
        ID3D11RenderTargetView* nullRTV = nullptr;
        DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

}
