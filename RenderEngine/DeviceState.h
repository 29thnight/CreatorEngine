#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "DeviceResources.h"
#include "LogSystem.h"

namespace DeviceState
{
	extern ID3D11Device3* g_pDevice;
	extern ID3D11DeviceContext3* g_pDeviceContext;
	extern ID3D11DepthStencilView* g_pDepthStencilView;
	extern ID3D11DepthStencilView* g_pEditorDepthStencilView;
	extern ID3D11DepthStencilState* g_pDepthStencilState;
	extern ID3D11RasterizerState* g_pRasterizerState;
	extern ID3D11BlendState* g_pBlendState;
	extern D3D11_VIEWPORT g_Viewport;
	extern ID3D11RenderTargetView* g_backBufferRTV;
	extern ID3D11ShaderResourceView* g_depthStancilSRV;
	extern ID3D11ShaderResourceView* g_editorDepthStancilSRV;
	extern ID3DUserDefinedAnnotation* g_annotation;
	extern DirectX11::Sizef g_ClientRect;
	extern float g_aspectRatio;
	extern std::atomic<int> g_renderCallCount;
}

namespace DirectX11
{
	inline void ResetCallCount()
	{
		DeviceState::g_renderCallCount = 0;
	}

	inline int GetDrawCallCount()
	{
		return DeviceState::g_renderCallCount.load();
	}

	inline ID3D11Buffer* CreateBuffer(uint32 size, D3D11_BIND_FLAG bindFlag, const void* data = nullptr)
	{
		if (!DeviceState::g_pDevice)
		{
			Debug->LogError("[RenderEngine] -> Device is not initialized");
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
			Debug->LogError("[RenderEngine] -> Device is not initialized");
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
#ifndef BUILD_FLAG
	//[unsafe]
	inline void BeginEvent(const std::wstring_view& name)
	{
		if (!DeviceState::g_annotation)
		{
			Debug->LogError("[RenderEngine] -> Annotation is not initialized");
			return;
		}
		DeviceState::g_annotation->BeginEvent(name.data());
	}

	//[unsafe]
	inline void EndEvent()
	{
		if (!DeviceState::g_annotation)
		{
			Debug->LogError("[RenderEngine] -> Annotation is not initialized");
			return;
		}
		DeviceState::g_annotation->EndEvent();
	}
#else
	inline void BeginEvent(const std::wstring_view&) {}
	inline void EndEvent() {}
#endif // !BUILD_FLAG

	//[unsafe]
	inline void UpdateBuffer(ID3D11Buffer* buffer, const void* data)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->UpdateSubresource(buffer, 0, nullptr, data, 0, 0);
	}

	//[unsafe]
	inline void VSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const * buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->VSSetConstantBuffers(slot, 1, buffer);
	}

	//[unsafe]
	inline void IASetVertexBuffers(uint32 startSlot, uint32 numBuffers, ID3D11Buffer* const* buffer, const uint32* stride, const uint32* offset)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		DeviceState::g_pDeviceContext->IASetVertexBuffers(startSlot, numBuffers, buffer, stride, offset);
	}

	//[unsafe]
	inline void IASetIndexBuffer(ID3D11Buffer* buffer, DXGI_FORMAT format, uint32 offset)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->IASetIndexBuffer(buffer, format, offset);
	}

	//[unsafe]
	inline void DrawIndexed(uint32 indexCount, uint32 startIndexLocation, int baseVertexLocation)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_renderCallCount++;
		DeviceState::g_pDeviceContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
	}

	//[unsafe]
	inline void DrawIndexedInstanced(uint32 indexCountPerInstance, uint32 instanceCount, uint32 startIndexLocation, int baseVertexLocation, uint32 startInstanceLocation)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
	}

	//[unsafe]
	inline void ClearRenderTargetView(ID3D11RenderTargetView* renderTargetView, const float color[4])
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->ClearRenderTargetView(renderTargetView, color);
	}

	//[unsafe]
	inline void ClearDepthStencilView(ID3D11DepthStencilView* depthStencilView, uint32 clearFlags, float depth, uint8 stencil)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->ClearDepthStencilView(depthStencilView, clearFlags, depth, stencil);
	}

	//[unsafe]
	inline void OMSetRenderTargets(uint32 numViews, ID3D11RenderTargetView* const* renderTargetViews, ID3D11DepthStencilView* depthStencilView)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->OMSetRenderTargets(numViews, renderTargetViews, depthStencilView);
	}

	//[unsafe]
	inline void PSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->PSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[unsafe]
	inline void GSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->GSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[unsafe]
	inline void CSSetConstantBuffer(uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[unsafe]
	inline void Dispatch(uint32 threadGroupCountX, uint32 threadGroupCountY, uint32 threadGroupCountZ)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->Dispatch(threadGroupCountX, threadGroupCountY, threadGroupCountZ);
	}

	//[unsafe]
	inline void PSSetShaderResources(uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->PSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}

	//[unsafe]
	inline void CSSetShaderResources(uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}

	//[unsafe]
	inline void CSSetUnorderedAccessViews(uint32 startSlot, uint32 numUAVs, ID3D11UnorderedAccessView* const* unorderedAccessViews, const uint32* initialCounts)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(startSlot, numUAVs, unorderedAccessViews, initialCounts);
	}

    //[unsafe]
    inline void Draw(uint32 vertexCount, uint32 startVertexLocation)
    {
        if (!DeviceState::g_pDeviceContext)
        {
           Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
            return;
        }
		DeviceState::g_renderCallCount++;
        DeviceState::g_pDeviceContext->Draw(vertexCount, startVertexLocation);
    }

	//[unsafe]
	inline void ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->ExecuteCommandList(pCommandList, RestoreContextState);
	}

    //[unsafe]
    inline void UnbindRenderTargets()
    {
        if (!DeviceState::g_pDeviceContext)
        {
            Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
            return;
        }
        ID3D11RenderTargetView* nullRTV = nullptr;
        DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &nullRTV, nullptr);
    }

	//[unsafe]
	inline void VSSetShader(ID3D11VertexShader* vertexShader, ID3D11ClassInstance* const* classInstances, uint32 numClassInstances)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->VSSetShader(vertexShader, classInstances, numClassInstances);
	}

	//[unsafe]
	inline void PSSetShader(ID3D11PixelShader* pixelShader, ID3D11ClassInstance* const* classInstances, uint32 numClassInstances)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->PSSetShader(pixelShader, classInstances, numClassInstances);
	}

	//[unsafe]
	inline void CSSetShader(ID3D11ComputeShader* computeShader, ID3D11ClassInstance* const* classInstances, uint32 numClassInstances)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CSSetShader(computeShader, classInstances, numClassInstances);
	}

	//[unsafe]
	inline void IASetInputLayout(ID3D11InputLayout* inputLayout)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->IASetInputLayout(inputLayout);
	}

	//[unsafe]
	inline void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY topology)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->IASetPrimitiveTopology(topology);
	}

	//[unsafe]
	inline void OMSetDepthStencilState(ID3D11DepthStencilState* depthStencilState, uint32 stencilRef)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->OMSetDepthStencilState(depthStencilState, stencilRef);
	}

	//[unsafe]
	inline void OMSetBlendState(ID3D11BlendState* blendState, const float blendFactor[4], uint32 sampleMask)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->OMSetBlendState(blendState, blendFactor, sampleMask);
	}

	//[unsafe]
	inline void RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->RSSetViewports(NumViewports, pViewports);
	}

	//[unsafe]
	inline void InitSetUp()
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->RSSetState(DeviceState::g_pRasterizerState);
		DeviceState::g_pDeviceContext->RSSetViewports(1, &DeviceState::g_Viewport);
		ID3D11RenderTargetView* rtv = DeviceState::g_backBufferRTV;
		DeviceState::g_pDeviceContext->OMSetRenderTargets(1, &rtv, DeviceState::g_pDepthStencilView);
		DeviceState::g_pDeviceContext->OMSetDepthStencilState(DeviceState::g_pDepthStencilState, 1);
		DeviceState::g_pDeviceContext->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	}

	//[unsafe]
	inline void CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CopyResource(pDstResource, pSrcResource);
	}

	//[safe]
	inline void UpdateBuffer(ID3D11DeviceContext* deferredContext, ID3D11Buffer* buffer, const void* data)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}

		deferredContext->UpdateSubresource(buffer, 0, nullptr, data, 0, 0);
	}

	//[safe]
	inline void OMSetRenderTargets(ID3D11DeviceContext* deferredContext, uint32 numViews, ID3D11RenderTargetView* const* renderTargetViews, ID3D11DepthStencilView* depthStencilView)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->OMSetRenderTargets(numViews, renderTargetViews, depthStencilView);
	}

	//[safe]
	inline void DrawIndexed(ID3D11DeviceContext* deferredContext, uint32 indexCount, uint32 startIndexLocation, int baseVertexLocation)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		DeviceState::g_renderCallCount++;
		deferredContext->DrawIndexed(indexCount, startIndexLocation, baseVertexLocation);
	}

	//[safe]
	inline void DrawIndexedInstanced(ID3D11DeviceContext* deferredContext, uint32 indexCountPerInstance, uint32 instanceCount, uint32 startIndexLocation, int baseVertexLocation, uint32 startInstanceLocation)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->DrawIndexedInstanced(indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
	}

	//[safe]
	inline void ClearRenderTargetView(ID3D11DeviceContext* deferredContext, ID3D11RenderTargetView* rtv, const float color[4])
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->ClearRenderTargetView(rtv, color);
	}

	//[safe]
	inline void ClearDepthStencilView(ID3D11DeviceContext* deferredContext, ID3D11DepthStencilView* dsv, uint32 clearFlags, float depth, uint8 stencil)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->ClearDepthStencilView(dsv, clearFlags, depth, stencil);
	}

	//[safe]
	inline void VSSetConstantBuffer(ID3D11DeviceContext* deferredContext, uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->VSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[safe]
	inline void PSSetConstantBuffer(ID3D11DeviceContext* deferredContext, uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->PSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[safe]
	inline void IASetVertexBuffers(ID3D11DeviceContext* deferredContext, uint32 startSlot, uint32 numBuffers, ID3D11Buffer* const* buffers, const uint32* strides, const uint32* offsets)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->IASetVertexBuffers(startSlot, numBuffers, buffers, strides, offsets);
	}

	//[safe]
	inline void IASetIndexBuffer(ID3D11DeviceContext* deferredContext, ID3D11Buffer* buffer, DXGI_FORMAT format, uint32 offset)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->IASetIndexBuffer(buffer, format, offset);
	}

	//[safe]
	inline void VSSetShader(ID3D11DeviceContext* deferredContext, ID3D11VertexShader* shader, ID3D11ClassInstance* const* classes, uint32 count)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->VSSetShader(shader, classes, count);
	}

	//[safe]
	inline void PSSetShader(ID3D11DeviceContext* deferredContext, ID3D11PixelShader* shader, ID3D11ClassInstance* const* classes, uint32 count)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->PSSetShader(shader, classes, count);
	}

	//[safe]
	inline void Draw(ID3D11DeviceContext* deferredContext, uint32 vertexCount, uint32 startVertexLocation)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		DeviceState::g_renderCallCount++;
		deferredContext->Draw(vertexCount, startVertexLocation);
	}

	//[safe]
	inline void Dispatch(ID3D11DeviceContext* deferredContext, uint32 x, uint32 y, uint32 z)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->Dispatch(x, y, z);
	}

	//[safe]
	inline void CopyResource(ID3D11DeviceContext* deferredContext, ID3D11Resource* dst, ID3D11Resource* src)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> deferredContext is null");
			return;
		}
		deferredContext->CopyResource(dst, src);
	}

	//[safe]
	inline void RSSetViewports(ID3D11DeviceContext* deferredContext, UINT NumViewports, const D3D11_VIEWPORT* pViewports)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->RSSetViewports(NumViewports, pViewports);
	}

	//[safe]
	inline void UnbindRenderTargets(ID3D11DeviceContext* deferredContext)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		ID3D11RenderTargetView* nullRTV = nullptr;
		deferredContext->OMSetRenderTargets(1, &nullRTV, nullptr);
	}

	//[safe]
	inline void FinishCommandList(ID3D11DeviceContext* deferredContext, BOOL RestoreDeferredContextState, ID3D11CommandList** ppCommandList)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->FinishCommandList(RestoreDeferredContextState, ppCommandList);
	}

	//[safe]
	inline void VSSetShaderResources(ID3D11DeviceContext* deferredContext, uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->VSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}

	//[safe]
	inline void PSSetShaderResources(ID3D11DeviceContext* deferredContext, uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->PSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}


	//[safe]
	inline void IASetPrimitiveTopology(ID3D11DeviceContext* deferredContext, D3D11_PRIMITIVE_TOPOLOGY topology)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->IASetPrimitiveTopology(topology);
	}

	//[safe]
	inline void CSSetShaderResources(ID3D11DeviceContext* deferredContext, uint32 startSlot, uint32 numViews, ID3D11ShaderResourceView* const* shaderResourceViews)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->CSSetShaderResources(startSlot, numViews, shaderResourceViews);
	}

	//[safe]
	inline void CSSetConstantBuffer(ID3D11DeviceContext* deferredContext, uint32 slot, uint32 numBuffers, ID3D11Buffer* const* buffer)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->CSSetConstantBuffers(slot, numBuffers, buffer);
	}

	//[safe]
	inline void CSSetUnorderedAccessViews(ID3D11DeviceContext* deferredContext, uint32 startSlot, uint32 numUAVs, ID3D11UnorderedAccessView* const* unorderedAccessViews, const uint32* initialCounts)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->CSSetUnorderedAccessViews(startSlot, numUAVs, unorderedAccessViews, initialCounts);
	}

	//[safe]
	inline void CSSetShader(ID3D11DeviceContext* deferredContext, ID3D11ComputeShader* computeShader, ID3D11ClassInstance* const* classInstances, uint32 numClassInstances)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->CSSetShader(computeShader, classInstances, numClassInstances);
	}

	//[unsafe]
	inline void CSSetSamplers(UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
	{
		if (!DeviceState::g_pDeviceContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		DeviceState::g_pDeviceContext->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
	}

	//[safe]
	inline void CSSetSamplers(ID3D11DeviceContext* deferredContext, UINT StartSlot, UINT NumSamplers, ID3D11SamplerState* const* ppSamplers)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->CSSetSamplers(StartSlot, NumSamplers, ppSamplers);
	}

	//[safe]
	inline void OMSetDepthStencilState(ID3D11DeviceContext* deferredContext, ID3D11DepthStencilState* depthStencilState, uint32 stencilRef)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->OMSetDepthStencilState(depthStencilState, stencilRef);
	}

	//[safe]
	inline void OMSetBlendState(ID3D11DeviceContext* deferredContext, ID3D11BlendState* blendState, const float blendFactor[4], uint32 sampleMask)
	{
		if (!deferredContext)
		{
			Debug->LogError("[RenderEngine] -> DeviceContext is not initialized");
			return;
		}
		deferredContext->OMSetBlendState(blendState, blendFactor, sampleMask);
	}
}
#endif // !DYNAMICCPP_EXPORTS