#include "GizmoLinePass.h"
#include "GizmoCbuffer.h"
#include "ShaderSystem.h"
#include "LightComponent.h"

float GetGizmoScale(Mathf::Vector3 gizmoPosition, const Camera& camera, float targetScreenHeightRatio)
{
    Mathf::Vector3 cameraPos = camera.m_eyePosition;
    Mathf::Vector3 distance = XMVector3Length(cameraPos - gizmoPosition);
	float distanceLength = distance.Length();

    float verticalFovRadians = camera.m_fov * Mathf::Rad2Deg;
    float screenHeight = 2.0f * distanceLength * tanf(verticalFovRadians * 0.5f);

    // 화면 전체 높이 대비 몇 %로 Gizmo를 보이게 할건지 (ex: 0.05 = 5%)
    float gizmoSizeInWorld = screenHeight * targetScreenHeightRatio;

    return gizmoSizeInWorld;
}

GizmoLinePass::GizmoLinePass()
{
	m_pso = std::make_unique<PipelineStateObject>();

	m_pso->m_vertexShader = &ShaderSystem->VertexShaders["Gizmo_Line"];
	m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Gizmo_Line"];

	m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_LINELIST;

    InputLayOutContainer vertexLayoutDesc = 
    {
        { "POSITION",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",        0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
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

    m_gizmoCameraBuffer = DirectX11::CreateBuffer(sizeof(GizmoCameraBuffer), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    DirectX::SetName(m_gizmoCameraBuffer.Get(), "GizmoCameraBuffer");
}

void GizmoLinePass::Execute(RenderScene& scene, Camera& camera)
{
    auto deviceContext = DeviceState::g_pDeviceContext;
	m_pso->Apply();

	auto selectedObject = scene.GetSelectSceneObject();
	if (nullptr == selectedObject) return;

    ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, camera.m_depthStencil->m_pDSV);

    GizmoCameraBuffer cameraBuffer{
    .VP = XMMatrixMultiply(camera.CalculateView(), camera.CalculateProjection()),
    .eyePosition = Mathf::Vector3(camera.m_eyePosition)
    };

    DirectX11::VSSetConstantBuffer(0, 1, m_gizmoCameraBuffer.GetAddressOf());
    DirectX11::UpdateBuffer(m_gizmoCameraBuffer.Get(), &cameraBuffer);



    switch (selectedObject->m_gameObjectType)
    {
	case GameObject::Type::Light:
	{
		auto lightComponent = selectedObject->GetComponent<LightComponent>();
		if (nullptr == lightComponent) return;
		// Directional Light
		if (lightComponent->m_lightType == LightType::DirectionalLight)
		{
			Mathf::Vector3 UpVec = Mathf::Vector3(0, 1, 0);
			float gizmoScale = GetGizmoScale(selectedObject->m_transform.GetWorldPosition(), camera, 0.05f);
			DrawWireCircle(selectedObject->m_transform.GetWorldPosition(), gizmoScale, UpVec, { 1, 1, 1, 1 });
            DrawDirectionalLightGizmo(selectedObject->m_transform.GetWorldPosition(), Mathf::Vector3(lightComponent->m_direction), gizmoScale, { 1, 1, 1, 1 });
		}
	}
	break;
    }

    m_pso->Reset();
}

void GizmoLinePass::ControlPanel()
{
}

void GizmoLinePass::Resize()
{
}

void GizmoLinePass::DrawDirectionalLightGizmo(const Mathf::Vector3& center, const Mathf::Vector3& direction, float radius, const Mathf::Color4& color)
{
    const int segmentCount = 9;
    const float lineLength = radius * 3.f;

    std::vector<LineVertex> vertices;
    vertices.reserve(segmentCount * 2); // 선 하나당 2개의 버텍스

    for (int i = 0; i < segmentCount; ++i)
    {
        float theta = XM_2PI * (i / (float)segmentCount);
        float x = radius * cosf(theta);
        float z = radius * sinf(theta);

        Mathf::Vector3 pointOnCircle = center + Mathf::Vector3(x, 0, z);
        Mathf::Vector3 normalizedDirection = direction;	normalizedDirection.Normalize();
        Mathf::Vector3 endPoint = pointOnCircle + normalizedDirection * lineLength;

        // 시작점
        vertices.push_back(LineVertex{ pointOnCircle, color });

        // 끝점
        vertices.push_back(LineVertex{ endPoint, color });
    }

    DrawLines(vertices.data(), (uint32_t)vertices.size());
}

void GizmoLinePass::DrawWireCircle(const Mathf::Vector3& center, float radius, const Mathf::Vector3& up, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 64;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    std::vector<LineVertex> circleVertices;
    circleVertices.reserve(segmentCount * 2);

    for (int i = 0; i < segmentCount; ++i)
    {
        float angle0 = XM_2PI * (i / (float)segmentCount);
        float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        circleVertices.push_back(LineVertex{ p0, color });
        circleVertices.push_back(LineVertex{ p1, color });
    }

    DrawLines(circleVertices.data(), (uint32_t)circleVertices.size());
}

void GizmoLinePass::DrawLines(LineVertex* vertices, uint32_t vertexCount)
{
    // 1. 버퍼 생성 (혹은 재사용)
    if (!m_lineVertexBuffer || m_lineVertexCount < vertexCount)
    {
        // 새 버퍼 만들어야 함
        if (m_lineVertexBuffer)
            m_lineVertexBuffer.Reset();

        D3D11_BUFFER_DESC desc = {};
        desc.Usage = D3D11_USAGE_DYNAMIC;
        desc.ByteWidth = sizeof(LineVertex) * vertexCount;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = vertices;

        DeviceState::g_pDevice->CreateBuffer(&desc, &initData, m_lineVertexBuffer.GetAddressOf());
        m_lineVertexCount = vertexCount;
    }
    else
    {
        // 버퍼가 충분하면 Map/Unmap만
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        DeviceState::g_pDeviceContext->Map(m_lineVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, vertices, sizeof(LineVertex) * vertexCount);
        DeviceState::g_pDeviceContext->Unmap(m_lineVertexBuffer.Get(), 0);
    }

    // 3. Set vertex buffer
    UINT stride = sizeof(LineVertex);
    UINT offset = 0;
    ID3D11Buffer* buffers[] = { m_lineVertexBuffer.Get() };
    DeviceState::g_pDeviceContext->IASetVertexBuffers(0, 1, buffers, &stride, &offset);

    // 5. Draw
    DeviceState::g_pDeviceContext->Draw(vertexCount, 0);
}
