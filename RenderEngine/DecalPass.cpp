#include "DecalPass.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"

cbuffer PS_CONSTANT_BUFFER
{
	XMMATRIX g_inverseViewMatrix; // 카메라 View-Projection의 역행렬
	XMMATRIX g_inverseProjectionMatrix; // 카메라 View-Projection의 역행렬
	float2 g_screenDimensions; // 화면 해상도 (너비, 높이)
};

cbuffer PS_DECAL_BUFFER
{
	XMMATRIX g_inverseDecalWorldMatrix; // 데칼 경계 상자 World의 역행렬
};

DecalPass::DecalPass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Decal"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Decal"];
	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

	CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
			&rasterizerDesc,
			&m_pso->m_rasterizerState
		)
	);

	CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	depthStencilDesc.DepthEnable = true;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
	depthStencilDesc.StencilEnable = FALSE;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);
	m_Buffer = DirectX11::CreateBuffer(sizeof(PS_CONSTANT_BUFFER), D3D11_BIND_CONSTANT_BUFFER, nullptr);
	m_decalBuffer = DirectX11::CreateBuffer(sizeof(PS_DECAL_BUFFER), D3D11_BIND_CONSTANT_BUFFER, nullptr);

	std::vector<uint32> indices =
	{
		0, 2, 1, 0, 3, 2,
		4, 6, 5, 4, 7, 6,
		8, 10, 9, 8, 11, 10,
		12, 14, 13, 12, 15, 14,
		16, 18, 17, 16, 19, 18,
		20, 22, 21, 20, 23, 22
	};
	std::vector<DecalVertex> vertics;
	vertics.reserve(24);
	vertics.push_back({Mathf::Vector3(-1.0f, -1.0f, -1.0f)});
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, 1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, 1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, -1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, 1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, 1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, -1.0f,  1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, -1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f,  1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f,  1.0f,  1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f,  1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f,  1.0f,  1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f,  1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, -1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, -1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, -1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, 1.0f, 1.0f) });
	vertics.push_back({ Mathf::Vector3(-1.0f, 1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, 1.0f, -1.0f) });
	vertics.push_back({ Mathf::Vector3(1.0f, 1.0f, 1.0f) });

	m_vertexBuffer = DirectX11::CreateBuffer(sizeof(DecalVertex) * vertics.size(), D3D11_BIND_VERTEX_BUFFER, vertics.data());
	DirectX::SetName(m_vertexBuffer.Get(), "DecalVertexBuffer");
	m_indexBuffer = DirectX11::CreateBuffer(sizeof(uint32) * indices.size(), D3D11_BIND_INDEX_BUFFER, indices.data());
	DirectX::SetName(m_indexBuffer.Get(), "DecalIndexBuffer");

	m_CopiedDepthTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"copiedDepthTexture",
		DXGI_FORMAT_R24_UNORM_X8_TYPELESS,
		D3D11_BIND_SHADER_RESOURCE
	);
	m_CopiedDepthTexture->CreateSRV(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
}

DecalPass::~DecalPass()
{
}

void DecalPass::Initialize(Texture* diffuseTexture, Texture* normalTexture)
{
	m_DiffuseTexture = diffuseTexture;
	m_NormalTexture = normalTexture;
}

void DecalPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void DecalPass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);
	
	ID3D11DeviceContext* deferredPtr = deferredContext;

	DirectX11::CopyResource(deferredPtr, m_CopiedDepthTexture->m_pTexture, renderData->m_depthStencil->m_pTexture);

	m_pso->Apply(deferredPtr);
	ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	DirectX11::OMSetRenderTargets(deferredPtr, 1, &view, renderData->m_depthStencil->m_pDSV);



	ID3D11ShaderResourceView* srv[3] = {
		m_CopiedDepthTexture->m_pSRV,
		m_DiffuseTexture->m_pSRV,
		m_NormalTexture->m_pSRV
    };
	DirectX11::PSSetShaderResources(deferredPtr, 0, 3, srv);

	PS_CONSTANT_BUFFER cBuf;
	cBuf.g_inverseProjectionMatrix = XMMatrixInverse(nullptr, renderData->m_frameCalculatedProjection);
	cBuf.g_inverseViewMatrix = XMMatrixInverse(nullptr, renderData->m_frameCalculatedView);
	cBuf.g_screenDimensions = { DirectX11::DeviceStates->g_Viewport.Width, DirectX11::DeviceStates->g_Viewport.Height };
	DirectX11::UpdateBuffer(deferredPtr, m_Buffer.Get(), &cBuf);

	camera.UpdateBuffer(deferredPtr);
	scene.UseModel(deferredPtr);
	DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);

	DirectX11::PSSetConstantBuffer(deferredPtr, 0, 1, m_Buffer.GetAddressOf());
	DirectX11::PSSetConstantBuffer(deferredPtr, 1, 1, m_decalBuffer.GetAddressOf());


	if (TestTexture) {
	
		//for(auto ) {UpdateModel(matrix, deferredPtr);}
		scene.UpdateModel(Mathf::Matrix::Identity, deferredPtr);
		PS_DECAL_BUFFER decalBuf;
		decalBuf.g_inverseDecalWorldMatrix = XMMatrixInverse(nullptr, Mathf::Matrix::Identity);
		DirectX11::UpdateBuffer(deferredPtr, m_decalBuffer.Get(), &decalBuf);
	
		ID3D11ShaderResourceView* decalTexture = TestTexture->m_pSRV;
		DirectX11::PSSetShaderResources(deferredPtr, 3, 1, &decalTexture);
	
		UINT offset = 0;
		DirectX11::IASetVertexBuffers(deferredPtr, 0, 1, m_vertexBuffer.GetAddressOf(), &m_decalstride, &offset);
		DirectX11::IASetIndexBuffer(deferredPtr, m_indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);
		DirectX11::IASetPrimitiveTopology(deferredPtr, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		DirectX11::DrawIndexed(deferredPtr, 36, 0, 0);
	}

	// Clear
	ID3D11ShaderResourceView* nullSRV[4] = { nullptr, nullptr, nullptr, nullptr };
	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	DirectX11::PSSetShaderResources(deferredPtr, 0, 4, nullSRV);
	deferredPtr->OMSetRenderTargets(1, nullRTV, nullptr);


	ID3D11CommandList* commandList{};
	deferredPtr->FinishCommandList(false, &commandList);
	PushQueue(camera.m_cameraIndex, commandList);
}

void DecalPass::ControlPanel()
{
	ImGui::PushID(this);
	if (TestTexture) {
		ImGui::Image((ImTextureID)TestTexture->m_pSRV, ImVec2(30, 30));
	}
	else {
		ImGui::Button("No texture");
	}

	ImVec2 minRect = ImGui::GetItemRectMin();
	ImVec2 maxRect = ImGui::GetItemRectMax();
	ImRect bb(minRect, maxRect);

	if (ImGui::BeginDragDropTargetCustom(bb, ImGui::GetID("MyDropTarget"))) {
		if (const ImGuiPayload* colorGradingPayload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)colorGradingPayload->Data;
			file::path filename = droppedFilePath;
			file::path filepath = PathFinder::Relative("Textures\\") / filename.filename();
			HashingString path = filepath.string();
			if (!filename.filename().empty()) {
				//SetColorGradingTexture(filepath.string());
				TestTexture.release();
				TestTexture = Texture::LoadManagedFromPath(filepath.string());
			}
			else {
				Debug->Log("Empty Texture File Name");
			}
		}
		ImGui::EndDragDropTarget();
	}

	ImGui::PopID();
}
