#include "SpritePass.h"
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

void SpritePass::CreateRenderCommandList(ID3D11DeviceContext* deferredContext, RenderScene& scene, Camera& camera)
{
    if (camera.m_avoidRenderPass.Test((flag)RenderPipelinePass::SpritePass))
    {
        return;
    }

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);
    ID3D11DeviceContext* deferredPtr = deferredContext;

    m_pso->Apply(deferredPtr);
    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(deferredPtr, 1, &rtv, renderData->m_depthStencil->m_pDSV);

    deferredPtr->OMSetDepthStencilState(m_NoWriteDepthStencilState.Get(), 1);
    deferredPtr->OMSetBlendState(DirectX11::DeviceStates->g_pBlendState, nullptr, 0xFFFFFFFF);
    DirectX11::RSSetViewports(deferredPtr, 1, &DirectX11::DeviceStates->g_Viewport);

    camera.UpdateBuffer(deferredPtr);
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

        auto world = proxy->m_worldMatrix;
        if (proxy->m_billboardType != BillboardType::None)
        {
            const auto& pos = proxy->m_worldPosition;
            if (proxy->m_billboardType == BillboardType::Spherical)
            {
				Mathf::Vector3 forward = camera.m_forward;
                world = Mathf::Matrix::CreateBillboard(pos, 
                    camera.m_eyePosition, proxy->m_billboardAxis, &forward);
            }
            else if (proxy->m_billboardType == BillboardType::Cylindrical)
            {
                // 0) 축(롤 고정용)과 스케일/위치만 기존 월드에서 유지
                Mathf::Vector3 axis = proxy->m_billboardAxis; // 예: (0,1,0)
                axis.Normalize();

                Mathf::Vector3 scl; Mathf::Quaternion q; Mathf::Vector3 t;
                Mathf::Matrix(world).Decompose(scl, q, t);   // t가 pos라면 pos로 바꿔 쓰세요
                const Mathf::Vector3 pos = t;                // 기존 위치 유지

                // 1) "카메라의 방향의 역방향"을 정면으로 사용 (위치 무관, 항상 -Forward)
                Mathf::Vector3 facing = -camera.m_forward;   // 핵심: look-at 금지, -forward 고정
                facing.Normalize();

                // 2) Cylindrical 제약: 축에 수직한 평면으로 투영(롤 고정)
                facing = facing - axis * facing.Dot(axis);
                float len2 = facing.LengthSquared();

                // 3) 축과 거의 평행해 생기는 특이상황 보정(카메라 Right를 권장)
                if (len2 < 1e-8f)
                {
                    // camera.m_right가 있다면 우선 사용
                    Mathf::Vector3 alt = Mathf::Vector3(camera.m_right).LengthSquared() > 0 ? Mathf::Vector3(camera.m_right) : Mathf::Vector3::Right;
                    alt.Normalize();
                    facing = alt - axis * alt.Dot(axis);
                    if (facing.LengthSquared() < 1e-8f)
                    {
                        // 최후 보정
                        alt = Mathf::Vector3::Forward;
                        facing = alt - axis * alt.Dot(axis);
                    }
                }
                facing.Normalize();

                // 4) 직교기저 구성: Y=axis(롤 고정), Z=facing(정면), X=Y×Z
                const Mathf::Vector3 yAxis = axis;
                Mathf::Vector3 zAxis = facing;               // 쿼드가 바라볼 로컬 정면
                Mathf::Vector3 xAxis = yAxis.Cross(zAxis);
                xAxis.Normalize();
                // 수치 안정화를 위해 Z 재정규화
                zAxis = xAxis.Cross(yAxis);
                zAxis.Normalize();

                // 5) 회전 행렬(컬럼 메이저 가정). 엔진 행렬 규약에 맞게 필요시 전치/배치 변경
                const Mathf::Matrix R(
                    xAxis.x, yAxis.x, zAxis.x, 0.0f,
                    xAxis.y, yAxis.y, zAxis.y, 0.0f,
                    xAxis.z, yAxis.z, zAxis.z, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );

                //const Mathf::Matrix S = Mathf::Matrix::CreateScale(scl);
                //const Mathf::Matrix Tm = Mathf::Matrix::CreateTranslation(pos);

                //// 쿼드 로컬 전면축이 엔진과 다르면 여기서 보정 회전 곱해도 됨 (예: Z->Y 보정 등)
                //world = S * R * Tm;
                //------------------------------------------------------------
                // 1) 깊이 z (카메라 전방 방향 성분)
                float z = (pos - Mathf::Vector3(camera.m_eyePosition)).Dot(camera.m_forward);
                z = std::max(z, 1e-3f); // 0/음수 방지

                // 2) 원하는 화면 높이(px) -> 필요한 월드 높이
                const float targetPx = std::max(1.0f, proxy->m_spriteTexture->GetSize().y);
                const float vpH = std::max(1.0f, (float)DirectX11::GetHeight());

                float requiredWorldHeight = 0.0f;
                // 퍼스펙티브: 수직 프러스텀 높이 = 2*z*tan(fovY/2)
                const float frustumH = 2.0f * z * tanf(camera.m_fov * 0.5f);
                requiredWorldHeight = frustumH * (targetPx / vpH);

                // 3) 로컬 기준 높이로 나눠서 스케일 인자 도출
                const float baseHeight = std::max(1e-6f, 1.f); // 유닛 쿼드면 1.0
                const float s = requiredWorldHeight / baseHeight;

                // (선택) 기존 scl의 비율(가로세로 비)만 유지하고 크기 자체는 s로 맞추고 싶다면:
                // 가장 큰 축을 기준으로 정규화해서 비율만 남김
                 float maxc = std::max({scl.x, scl.y, scl.z, 1e-6f});
                 Mathf::Vector3 scaleVec = s * (scl / maxc);

                //// 단순/권장: 균등 스케일로 덮어써 픽셀 고정 효과 극대화
                //Mathf::Vector3 scaleVec(s, s, s);

                // 4) 최종 행렬 조립 (회전은 네가 구성한 R을 그대로 사용)
                const Mathf::Matrix S = Mathf::Matrix::CreateScale(scaleVec);
                const Mathf::Matrix Tm = Mathf::Matrix::CreateTranslation(pos);

                world = S * R * Tm; //------------------------------------
            }
        }
        scene.UpdateModel(world, deferredPtr);
        ID3D11ShaderResourceView* srv = proxy->m_spriteTexture->m_pSRV;
        DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &srv);
        proxy->m_quadMesh->Draw(deferredPtr);
    }

    ID3D11ShaderResourceView* nullSRV = nullptr;
    DirectX11::PSSetShaderResources(deferredPtr, 0, 1, &nullSRV);
    ID3D11RenderTargetView* nullRTV = nullptr;
    deferredPtr->OMSetRenderTargets(1, &nullRTV, nullptr);
    ID3D11BlendState* nullBlend = nullptr;
    DirectX11::OMSetBlendState(deferredPtr, nullBlend, nullptr, 0xFFFFFFFF);
    DirectX11::OMSetDepthStencilState(deferredPtr, DirectX11::DeviceStates->g_pDepthStencilState, 1);

    ID3D11CommandList* commandList{};
    deferredPtr->FinishCommandList(false, &commandList);
    PushQueue(camera.m_cameraIndex, commandList);
}

void SpritePass::Resize(uint32_t width, uint32_t height)
{
}
