#include "WireFramePass.h"
#include "Scene.h"
#include "Camera.h"
#include "Mesh.h"
#include "Skeleton.h"
#include "ShaderSystem.h"
#include "Renderer.h"

WireFramePass::WireFramePass()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["WireFrame"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["WireFrame"];
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;

    m_Buffer = DirectX11::CreateBuffer(sizeof(CameraBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);

    InputLayOutContainer vertexLayoutDesc = {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
    rasterizerDesc.FillMode = D3D11_FILL_WIREFRAME;
    //rasterizerDesc.FrontCounterClockwise = TRUE;
    rasterizerDesc.AntialiasedLineEnable = TRUE;
    rasterizerDesc.CullMode = D3D11_CULL_NONE;

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

    m_boneBuffer = DirectX11::CreateBuffer(sizeof(Mathf::xMatrix) * Skeleton::MAX_BONES, D3D11_BIND_CONSTANT_BUFFER, nullptr);

}

WireFramePass::~WireFramePass()
{
}

void WireFramePass::Execute(RenderScene& scene, Camera& camera)
{
    m_pso->Apply();

	auto& deviceContext = DeviceState::g_pDeviceContext;
    ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

	camera.UpdateBuffer();
	scene.UseModel();

    DirectX11::VSSetConstantBuffer(3, 1, m_boneBuffer.GetAddressOf());

    m_CameraBuffer.m_CameraPosition = camera.m_eyePosition;

    DirectX11::UpdateBuffer(m_Buffer.Get(), &m_CameraBuffer);
    DirectX11::VSSetConstantBuffer(4, 1, m_Buffer.GetAddressOf());

	Animator* currentAnimator = nullptr;

	for (auto& sceneObject : scene.GetScene()->m_SceneObjects)
	{
		MeshRenderer* meshRenderer = sceneObject->GetComponent<MeshRenderer>();
		if (meshRenderer == nullptr) continue;
		if (!meshRenderer->IsEnabled()) continue;

		scene.UpdateModel(sceneObject->m_transform.GetWorldMatrix());

        Animator* animator = scene.GetScene()->m_SceneObjects[sceneObject->m_parentIndex]->GetComponent<Animator>();
        if (nullptr != animator && animator->IsEnabled())
        {
            if (animator != currentAnimator)
            {
                DirectX11::UpdateBuffer(m_boneBuffer.Get(), animator->m_FinalTransforms);
                currentAnimator = animator;
            }
        }

		meshRenderer->m_Mesh->Draw();
	}
}

void WireFramePass::ControlPanel()
{
}

void WireFramePass::ReloadShaders()
{
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["WireFrame"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["WireFrame"];
    m_pso->m_inputLayout->Release();

    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateInputLayout(
            vertexLayoutDesc,
            _countof(vertexLayoutDesc),
            m_pso->m_vertexShader->GetBufferPointer(),
            m_pso->m_vertexShader->GetBufferSize(),
            &m_pso->m_inputLayout
        )
    );
}

void WireFramePass::Resize()
{
}
