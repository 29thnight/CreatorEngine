#include "DecalPass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "../EngineEntry/RenderPassSettings.h"

cbuffer PS_CONSTANT_BUFFER
{
	XMMATRIX g_inverseViewMatrix; // ī�޶� View-Projection�� �����
	XMMATRIX g_inverseProjectionMatrix; // ī�޶� View-Projection�� �����
	float2 g_screenDimensions; // ȭ�� �ػ� (�ʺ�, ����)
};

cbuffer PS_DECAL_BUFFER
{
	XMMATRIX g_inverseDecalWorldMatrix; // ��Į ��� ���� World�� �����
	uint32 g_UseTextures;
	uint32 sliceX = 1;
	uint32 sliceY = 1;
	uint32 sliceNum = 0;
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
	vertics.push_back({Mathf::Vector3(-0.5f, -0.5f, -0.5f)});
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, 0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, 0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, -0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, 0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, 0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, -0.5f,  0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, -0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f,  0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f,  0.5f,  0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f,  0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f,  0.5f,  0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f,  0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, -0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, -0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, -0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, 0.5f, 0.5f) });
	vertics.push_back({ Mathf::Vector3(-0.5f, 0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, 0.5f, -0.5f) });
	vertics.push_back({ Mathf::Vector3(0.5f, 0.5f, 0.5f) });

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

	m_CopiedDiffuseTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"copiedDiffuseTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);
	m_CopiedDiffuseTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_CopiedNormalTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"copiedNormalTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);
	m_CopiedNormalTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	m_CopiedORMTexture = Texture::Create(
		DirectX11::DeviceStates->g_ClientRect.width,
		DirectX11::DeviceStates->g_ClientRect.height,
		"copiedORMTexture",
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		D3D11_BIND_SHADER_RESOURCE
	);
	m_CopiedORMTexture->CreateSRV(DXGI_FORMAT_R16G16B16A16_FLOAT);

	for (int i = 0; i < DecalChannel::MAX; i++)
	{
		CD3D11_BLEND_DESC1 blendDesc = CD3D11_BLEND_DESC1(CD3D11_DEFAULT());
		blendDesc.IndependentBlendEnable = TRUE;

		// --- Diffuse G-Buffer (RT 0) ---
		if (i & DecalChannel::dDiffuse)
		{
			blendDesc.RenderTarget[0].BlendEnable = TRUE; 
			blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
			// ���� ä�� ������ ���� (���� G-Buffer�� ���Ĵ� ������� �����Ƿ� �����ϰ� ����)
			blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		else
		{
			blendDesc.RenderTarget[0].BlendEnable = FALSE;
			blendDesc.RenderTarget[0].RenderTargetWriteMask = 0;
		}

		// --- Normal G-Buffer (RT 1) ---
		if (i & DecalChannel::dNormal)
		{
			blendDesc.RenderTarget[1].BlendEnable = TRUE; 
			blendDesc.RenderTarget[1].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[1].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[1].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[1].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[1].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[1].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[1].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		else
		{
			blendDesc.RenderTarget[1].BlendEnable = FALSE;
			blendDesc.RenderTarget[1].RenderTargetWriteMask = 0;
		}

		// --- ORM G-Buffer (RT 2) ---
		if (i & DecalChannel::dORM)
		{
			blendDesc.RenderTarget[2].BlendEnable = TRUE; 
			blendDesc.RenderTarget[2].SrcBlend = D3D11_BLEND_SRC_ALPHA;
			blendDesc.RenderTarget[2].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
			blendDesc.RenderTarget[2].BlendOp = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[2].SrcBlendAlpha = D3D11_BLEND_ONE;
			blendDesc.RenderTarget[2].DestBlendAlpha = D3D11_BLEND_ZERO;
			blendDesc.RenderTarget[2].BlendOpAlpha = D3D11_BLEND_OP_ADD;
			blendDesc.RenderTarget[2].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		}
		else
		{
			blendDesc.RenderTarget[2].BlendEnable = FALSE;
			blendDesc.RenderTarget[2].RenderTargetWriteMask = 0;
		}

		DirectX11::ThrowIfFailed(
			DirectX11::DeviceStates->g_pDevice->CreateBlendState1(
				&blendDesc,
				&m_pBlendStates[i]
			)
		);
	}
}

DecalPass::~DecalPass()
{
}

void DecalPass::Initialize(Texture* diffuseTexture, Texture* normalTexture, Texture* ormTexture)
{
	m_DiffuseTexture = diffuseTexture;
	m_NormalTexture = normalTexture;
	m_OccluRoughMetalTexture = ormTexture;
}

void DecalPass::Execute(RenderScene& scene, Camera& camera)
{
	ExecuteCommandList(scene, camera);
}

void DecalPass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
	if (!RenderPassData::VaildCheck(&camera)) return;
	auto renderData = RenderPassData::GetData(&camera);
	
	ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

	context.CopyResource(m_CopiedDepthTexture->m_pTexture, renderData->m_depthStencil->m_pTexture);
	context.CopyResource(m_CopiedDiffuseTexture->m_pTexture, m_DiffuseTexture->m_pTexture);
	context.CopyResource(m_CopiedNormalTexture->m_pTexture, m_NormalTexture->m_pTexture);
	context.CopyResource(m_CopiedORMTexture->m_pTexture, m_OccluRoughMetalTexture->m_pTexture);

	m_pso->Apply(deferredPtr);
	//ID3D11RenderTargetView* view = renderData->m_renderTarget->GetRTV();
	//context.SetRenderTarget(view, renderData->m_depthStencil->m_pDSV);
	ID3D11RenderTargetView* view[3] = {
		m_DiffuseTexture->GetRTV(),
		m_NormalTexture->GetRTV(),
		m_OccluRoughMetalTexture->GetRTV()
	};
	context.SetRenderTargets(3, reinterpret_cast<RHINativeRenderTarget const*>(view), renderData->m_depthStencil->m_pDSV);


	ID3D11ShaderResourceView* srv[4] = {
		m_CopiedDepthTexture->m_pSRV,
		m_CopiedDiffuseTexture->m_pSRV,
		m_CopiedNormalTexture->m_pSRV,
		m_CopiedORMTexture->m_pSRV
    };
	context.SetPixelShaderResources(0, 4, reinterpret_cast<RHINativeShaderResource const*>(srv));

	PS_CONSTANT_BUFFER cBuf;
	cBuf.g_inverseProjectionMatrix = XMMatrixInverse(nullptr, renderData->m_frameCalculatedProjection);
	cBuf.g_inverseViewMatrix = XMMatrixInverse(nullptr, renderData->m_frameCalculatedView);
	const RHIViewport fullVp = RHI::Device().GetFullViewport();
	cBuf.g_screenDimensions = { fullVp.width, fullVp.height };
	context.UpdateBuffer(m_Buffer.Get(), &cBuf);

	camera.DeferredUpdateBuffer(deferredPtr, renderData->m_frameCalculatedView, renderData->m_frameCalculatedProjection);
	scene.UseModel(deferredPtr);
	{
		const RHIViewport rhiVp = RHI::Device().GetFullViewport();
		context.SetViewports(1, &rhiVp);
	}

	context.SetPixelShaderConstantBuffer(0, m_Buffer.Get());
	context.SetPixelShaderConstantBuffer(1, m_decalBuffer.Get());

	for (auto& proxy : renderData->m_decalQueue) {

		scene.UpdateModel(proxy->m_worldMatrix, deferredPtr);
		PS_DECAL_BUFFER decalBuf;
		decalBuf.g_inverseDecalWorldMatrix = XMMatrixInverse(nullptr, proxy->m_worldMatrix);
		decalBuf.g_UseTextures = 
			(proxy->m_diffuseTexture != nullptr ? DecalChannel::dDiffuse : 0) |
			(proxy->m_normalTexture != nullptr ? DecalChannel::dNormal : 0) |
			(proxy->m_occluroughmetalTexture != nullptr ? DecalChannel::dORM : 0);
		decalBuf.sliceX = proxy->m_sliceX;
		decalBuf.sliceY = proxy->m_sliceY;
		decalBuf.sliceNum = proxy->m_sliceNum;
		context.UpdateBuffer(m_decalBuffer.Get(), &decalBuf);

		uint32 decalChannel = DecalChannel::None;
		decalChannel |= proxy->m_diffuseTexture ? DecalChannel::dDiffuse : 0;
		decalChannel |= proxy->m_normalTexture ? DecalChannel::dNormal : 0;
		decalChannel |= proxy->m_occluroughmetalTexture ? DecalChannel::dORM : 0;
		context.SetBlendState(m_pBlendStates[decalChannel].Get(), nullptr, 0xFFFFFFFF);

		ID3D11ShaderResourceView* decalTextures[3] = {
			proxy->m_diffuseTexture ? proxy->m_diffuseTexture->m_pSRV : nullptr,
			proxy->m_normalTexture ? proxy->m_normalTexture->m_pSRV : nullptr,
			proxy->m_occluroughmetalTexture ? proxy->m_occluroughmetalTexture->m_pSRV : nullptr
		};
		context.SetPixelShaderResources(4, 3, reinterpret_cast<RHINativeShaderResource const*>(decalTextures));

		UINT offset = 0;
		context.SetVertexBuffer(0, m_vertexBuffer.Get(), m_decalstride, offset);
		context.SetIndexBuffer(m_indexBuffer.Get(), true, 0);
		context.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
		context.DrawIndexed(36, 0, 0);
	}

	// Clear
	ID3D11ShaderResourceView* nullSRV[7] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
	ID3D11RenderTargetView* nullRTV[3] = { nullptr, nullptr, nullptr };
	context.SetPixelShaderResources(0, 7, reinterpret_cast<RHINativeShaderResource const*>(nullSRV));
	deferredPtr->OMSetRenderTargets(3, nullRTV, nullptr);
	ID3D11BlendState* nullBlendState = nullptr;
	context.SetBlendState(nullBlendState, nullptr, 0xFFFFFFFF);


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
		if (const ImGuiPayload* decalPayload = ImGui::AcceptDragDropPayload("Texture"))
		{
			const char* droppedFilePath = (const char*)decalPayload->Data;
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
