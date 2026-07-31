#include "SpritePass.h"
#include "RHI/RHI.h"
#include "ShaderSystem.h"
#include "RenderScene.h"
#include "BillboardType.h"

SpritePass::SpritePass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["VertexShader"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Sprite"];

	InputLayOutContainer vertexLayoutDesc = {
		{ "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",       0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     0, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",     1, DXGI_FORMAT_R32G32_FLOAT,       0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BINORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "BLENDWEIGHT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_pso->CreateInputLayout(std::move(vertexLayoutDesc));

    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };

    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(
            &rasterizerDesc,
            &m_pso->m_rasterizerState
        )
    );

	auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
	auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);

	m_pso->m_samplers.push_back(linearSampler);
	m_pso->m_samplers.push_back(pointSampler);

    CD3D11_DEPTH_STENCIL_DESC depthStencilDesc{ CD3D11_DEFAULT() };
    depthStencilDesc.DepthEnable = false;
    depthStencilDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;

	DirectX11::ThrowIfFailed(
		DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(
			&depthStencilDesc,
			&m_NoWriteDepthStencilState
		)
	);

	m_pso->m_depthStencilState = m_NoWriteDepthStencilState.Get();
	m_pso->m_blendState = DirectX11::DeviceStates->g_pBlendState;
}

SpritePass::~SpritePass()
{
}

void SpritePass::Execute(RenderScene& scene, Camera& camera)
{
    ExecuteCommandList(scene, camera);
}

void SpritePass::ControlPanel()
{
}

void SpritePass::CreateRenderCommandList(RHICommandContext& context, RenderScene& scene, Camera& camera)
{
    if (camera.m_avoidRenderPass.Test((flag)RenderPipelinePass::SpritePass))
    {
        return;
    }

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);
    ID3D11DeviceContext* deferredPtr = static_cast<ID3D11DeviceContext*>(context.GetNativeHandle()); // 전환기 탈출구(잔존 네이티브 경로용)

    // RHI 이식(PHASE 3-1). 원래 이 패스는 자유 함수와 deferredPtr-> 직접 호출이
    // 섞여 있었다 — 직접 호출은 드로우콜 카운트·어노테이션을 우회한다.
    // RHI 경유로 통일하면서 그 우회도 함께 사라진다.

    m_pso->Apply(deferredPtr);
    RHINativeRenderTarget rtv = renderData->m_renderTarget->GetRTV();
    context.SetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);

    context.SetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
    context.SetBlendState(RHI::Device().GetDefaultBlendState(), nullptr, 0xFFFFFFFF);

    const RHIViewport viewport = RHI::Device().GetFullViewport();
    context.SetViewports(1, &viewport);

    renderData->BindFrameCameraBuffers(context);
    scene.UseModel(deferredPtr);

    for (auto& proxy : renderData->m_spriteRenderQueue)
    {
        if (!proxy || !proxy->m_quadMesh || !proxy->m_spriteTexture) continue;
        if (proxy->m_customPSO)
        {
            proxy->m_customPSO->Apply(deferredPtr);
        }
        else
        {
            m_pso->Apply(deferredPtr);
        }

        if (proxy->m_enableDepth)
        {
            context.SetDepthStencilState(RHI::Device().GetDefaultDepthStencilState(), 1);
        }
        else
        {
            context.SetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
        }

        auto world = proxy->m_worldMatrix;
        if (proxy->m_billboardType != BillboardType::None)
        {
            const auto& pos = proxy->m_worldPosition;
            if (proxy->m_billboardType == BillboardType::Spherical)
            {
				Mathf::Vector3 forward = renderData->m_frameForward;
                world = Mathf::Matrix::CreateBillboard(pos, 
                    renderData->m_frameEyePosition, proxy->m_billboardAxis, &forward);
            }
            else if (proxy->m_billboardType == BillboardType::Cylindrical)
            {
                // 0) ��(�� ������)�� ������/��ġ�� ���� ���忡�� ����
                Mathf::Vector3 axis = proxy->m_billboardAxis; // ��: (0,1,0)
                axis.Normalize();

                Mathf::Vector3 scl; Mathf::Quaternion q; Mathf::Vector3 t;
                Mathf::Matrix(world).Decompose(scl, q, t);   // t�� pos��� pos�� �ٲ� ������
                const Mathf::Vector3 pos = t;                // ���� ��ġ ����

                // 1) "ī�޶��� ������ ������"�� �������� ��� (��ġ ����, �׻� -Forward)
                Mathf::Vector3 facing = -renderData->m_frameForward;   // �ٽ�: look-at ����, -forward ����
                facing.Normalize();

                // 2) Cylindrical ����: �࿡ ������ ������� ����(�� ����)
                facing = facing - axis * facing.Dot(axis);
                float len2 = facing.LengthSquared();

                // 3) ��� ���� ������ ����� Ư�̻�Ȳ ����(ī�޶� Right�� ����)
                if (len2 < 1e-8f)
                {
                    // renderData->m_frameRight�� �ִٸ� �켱 ���
                    Mathf::Vector3 alt = Mathf::Vector3(renderData->m_frameRight).LengthSquared() > 0 ? Mathf::Vector3(renderData->m_frameRight) : Mathf::Vector3::Right;
                    alt.Normalize();
                    facing = alt - axis * alt.Dot(axis);
                    if (facing.LengthSquared() < 1e-8f)
                    {
                        // ���� ����
                        alt = Mathf::Vector3::Forward;
                        facing = alt - axis * alt.Dot(axis);
                    }
                }
                facing.Normalize();

                // 4) �������� ����: Y=axis(�� ����), Z=facing(����), X=Y��Z
                const Mathf::Vector3 yAxis = axis;
                Mathf::Vector3 zAxis = facing;               // ���尡 �ٶ� ���� ����
                Mathf::Vector3 xAxis = yAxis.Cross(zAxis);
                xAxis.Normalize();
                // ��ġ ����ȭ�� ���� Z ������ȭ
                zAxis = xAxis.Cross(yAxis);
                zAxis.Normalize();

                // 5) ȸ�� ���(�÷� ������ ����). ���� ��� �Ծ࿡ �°� �ʿ�� ��ġ/��ġ ����
                const Mathf::Matrix R(
                    xAxis.x, yAxis.x, zAxis.x, 0.0f,
                    xAxis.y, yAxis.y, zAxis.y, 0.0f,
                    xAxis.z, yAxis.z, zAxis.z, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );

                //const Mathf::Matrix S = Mathf::Matrix::CreateScale(scl);
                //const Mathf::Matrix Tm = Mathf::Matrix::CreateTranslation(pos);

                //// ���� ���� �������� ������ �ٸ��� ���⼭ ���� ȸ�� ���ص� �� (��: Z->Y ���� ��)
                //world = S * R * Tm;
                //------------------------------------------------------------
                // 1) ���� z (ī�޶� ���� ���� ����)
                float z = (pos - Mathf::Vector3(renderData->m_frameEyePosition)).Dot(renderData->m_frameForward);
                z = std::max(z, 1e-3f); // 0/���� ����

                // 2) ���ϴ� ȭ�� ����(px) -> �ʿ��� ���� ����
                const float targetPx = std::max(1.0f, proxy->m_spriteTexture->GetSize().y);
                const float vpH = std::max(1.0f, (float)DirectX11::GetHeight());

                float requiredWorldHeight = 0.0f;
                // �۽���Ƽ��: ���� �������� ���� = 2*z*tan(fovY/2)
                const float frustumH = 2.0f * z * tanf(renderData->m_frameFov * 0.5f);
                requiredWorldHeight = frustumH * (targetPx / vpH);

                // 3) ���� ���� ���̷� ������ ������ ���� ����
                const float baseHeight = std::max(1e-6f, 1.f); // ���� ����� 1.0
                const float s = requiredWorldHeight / baseHeight;

                // (����) ���� scl�� ����(���μ��� ��)�� �����ϰ� ũ�� ��ü�� s�� ���߰� �ʹٸ�:
                // ���� ū ���� �������� ����ȭ�ؼ� ������ ����
                 float maxc = std::max({scl.x, scl.y, scl.z, 1e-6f});
                 Mathf::Vector3 scaleVec = s * (scl / maxc);

                //// �ܼ�/����: �յ� �����Ϸ� ����� �ȼ� ���� ȿ�� �ش�ȭ
                //Mathf::Vector3 scaleVec(s, s, s);

                // 4) ���� ��� ���� (ȸ���� �װ� ������ R�� �״�� ���)
                const Mathf::Matrix S = Mathf::Matrix::CreateScale(scaleVec);
                const Mathf::Matrix Tm = Mathf::Matrix::CreateTranslation(pos);

                world = S * R * Tm; //------------------------------------
            }
        }
        scene.UpdateModel(world, deferredPtr);
        RHINativeShaderResource srv = proxy->m_spriteTexture->m_pSRV;
        context.SetPixelShaderResources(0, 1, &srv);
        proxy->m_quadMesh->Draw(deferredPtr);
    }

    RHINativeShaderResource nullSRV = nullptr;
    context.SetPixelShaderResources(0, 1, &nullSRV);
    RHINativeRenderTarget nullRTV = nullptr;
    context.SetRenderTargets(1, &nullRTV, nullptr);
    context.SetBlendState(nullptr, nullptr, 0xFFFFFFFF);
    context.SetDepthStencilState(RHI::Device().GetDefaultDepthStencilState(), 1);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void SpritePass::Resize(uint32_t width, uint32_t height)
{
}
