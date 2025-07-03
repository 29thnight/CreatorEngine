#include "GizmoLinePass.h"
#include "GizmoCbuffer.h"
#include "ShaderSystem.h"
#include "LightComponent.h"
#include "CameraComponent.h"

float GetGizmoScale(Mathf::Vector3 gizmoPosition, const Camera& camera, float targetScreenHeightRatio)
{
    Mathf::Vector3 cameraPos = camera.m_eyePosition;
    Mathf::Vector3 distance = XMVector3Length(cameraPos - gizmoPosition);
	float distanceLength = distance.Length();

    float verticalFovRadians = camera.m_fov * Mathf::Rad2Deg;
    float screenHeight = 2.0f * distanceLength * tanf(verticalFovRadians * 0.5f);

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
    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

	m_pso->Apply();

    auto activeScene = SceneManagers->GetActiveScene();

	auto selectedObject = activeScene->GetSelectSceneObject();
	if (nullptr == selectedObject) return;

    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, nullptr);

    GizmoCameraBuffer cameraBuffer{
    .VP = XMMatrixMultiply(camera.CalculateView(), camera.CalculateProjection()),
    .eyePosition = Mathf::Vector3(camera.m_eyePosition)
    };

    DirectX11::VSSetConstantBuffer(0, 1, m_gizmoCameraBuffer.GetAddressOf());
    DirectX11::UpdateBuffer(m_gizmoCameraBuffer.Get(), &cameraBuffer);

    switch (selectedObject->m_gameObjectType)
    {
	case GameObjectType::Light:
	{
		auto lightComponent = selectedObject->GetComponent<LightComponent>();
		if (nullptr == lightComponent) return;

        const Mathf::Vector3 worldPosition = selectedObject->m_transform.GetWorldPosition();
        const Mathf::Vector3 lightDirection = Mathf::Vector3(lightComponent->m_direction);

        float gizmoScale = GetGizmoScale(worldPosition, camera, 0.05f);

        switch (lightComponent->m_lightType)
        {
        case LightType::DirectionalLight:
            DrawWireCircleAndLines(worldPosition, gizmoScale, lightDirection, lightDirection, { 1, 0, 1, 1 });
			break;
        case LightType::PointLight:
            DrawWireSphere(worldPosition, lightComponent->m_range, { 1, 1, 0, 1 });
            break;
        case LightType::SpotLight:
            DrawWireCone(worldPosition, lightDirection, lightComponent->m_range, lightComponent->m_spotLightAngle, { 0, 1, 1, 1 });
            break;
        }
	}
	break;
    case GameObjectType::Camera:
    {
        auto cameraComponent = selectedObject->GetComponent<CameraComponent>();
        if(nullptr == cameraComponent) return;

        auto camera = cameraComponent->GetCamera();
		if (nullptr == camera || camera->m_isOrthographic) return; // 카메라가 orthographic일 경우나 없을 경우 throughpass
        
        DrawBoundingFrustum(cameraComponent->GetFrustum(), { 1, 0, 1, 1 });
    }
    break;
    }

    m_pso->Reset();
}

void GizmoLinePass::ControlPanel()
{
    ImGui::Text("Gizmo Line Pass");
	ImGui::Separator();
	ImGui::Checkbox("Enable Draw Physics Gizmos", &m_isShowPhysicsDebugInfo);
}

void GizmoLinePass::Resize(uint32_t width, uint32_t height)
{
}

void GizmoLinePass::DrawWireCircleAndLines(const Mathf::Vector3& center, float radius, const Mathf::Vector3& up, const Mathf::Vector3& direction, const Mathf::Color4& color)
{
    using namespace Mathf;
    const int segmentCount = 9;
    const float lineLength = radius * 3.f;

    Vector3 right = XMVector3Normalize(XMVector3Cross(up, Vector3(0, 1, 0)));
    if (XMVectorGetX(XMVector3LengthSq(right)) < 1e-5f)
        right = XMVector3Normalize(XMVector3Cross(up, Vector3(1, 0, 0)));
    Vector3 forward = XMVector3Normalize(XMVector3Cross(right, up));

    std::vector<LineVertex> vertices;
    vertices.reserve(segmentCount * 4);

    for (int i = 0; i < segmentCount; ++i)
    {
        float angle0 = XM_2PI * (i / (float)segmentCount);
        float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        Vector3 p0 = center + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        Vector3 p1 = center + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        vertices.push_back(LineVertex{ p0, color });
        vertices.push_back(LineVertex{ p1, color });

        Vector3 dirNormalized = direction;
        dirNormalized.Normalize();
        Vector3 endPoint = p0 + dirNormalized * lineLength;

        vertices.push_back(LineVertex{ p0, color });
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
    if (!m_lineVertexBuffer || m_lineVertexCount < vertexCount)
    {
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
        D3D11_MAPPED_SUBRESOURCE mapped = {};
        DeviceState::g_pDeviceContext->Map(m_lineVertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        memcpy(mapped.pData, vertices, sizeof(LineVertex) * vertexCount);
        DeviceState::g_pDeviceContext->Unmap(m_lineVertexBuffer.Get(), 0);
    }

    UINT stride = sizeof(LineVertex);
    UINT offset = 0;
    ID3D11Buffer* buffers[] = { m_lineVertexBuffer.Get() };
    DeviceState::g_pDeviceContext->IASetVertexBuffers(0, 1, buffers, &stride, &offset);

    DeviceState::g_pDeviceContext->Draw(vertexCount, 0);
}

void GizmoLinePass::DrawWireSphere(const Mathf::Vector3& center, float radius, const Mathf::Color4& color)
{
    // XY, YZ, XZ 평면에 각각 원을 그려서 구처럼 보이게
    DrawWireCircle(center, radius, Mathf::Vector3(0, 1, 0), color); // XZ plane
    DrawWireCircle(center, radius, Mathf::Vector3(1, 0, 0), color); // YZ plane
    DrawWireCircle(center, radius, Mathf::Vector3(0, 0, 1), color); // XY plane
}

void GizmoLinePass::DrawWireCone(const Mathf::Vector3& apex, const Mathf::Vector3& direction, float height, float outerConeAngle, const Mathf::Color4& color)
{
    using namespace Mathf;

    const int segmentCount = 32;
    std::vector<LineVertex> coneVertices;
    coneVertices.reserve(segmentCount * 2);

    Vector3 dir = direction;
    dir.Normalize();

    Vector3 up = Vector3(0, 1, 0);
    if (fabs(XMVectorGetX(XMVector3Dot(up, dir))) > 0.99f)
        up = Vector3(1, 0, 0);

    Vector3 right = XMVector3Normalize(XMVector3Cross(dir, up));
    Vector3 forward = XMVector3Normalize(XMVector3Cross(right, dir));

    float radius = height * tanf(XMConvertToRadians(outerConeAngle) * 0.5f);

    for (int i = 0; i < segmentCount; ++i)
    {
        float angle0 = XM_2PI * (i / (float)segmentCount);
        float angle1 = XM_2PI * ((i + 1) / (float)segmentCount);

        Vector3 p0 = apex + dir * height + radius * (cosf(angle0) * right + sinf(angle0) * forward);
        Vector3 p1 = apex + dir * height + radius * (cosf(angle1) * right + sinf(angle1) * forward);

        coneVertices.push_back(LineVertex{ p0, color });
        coneVertices.push_back(LineVertex{ p1, color });

        coneVertices.push_back(LineVertex{ apex, color });
        coneVertices.push_back(LineVertex{ p0, color });
    }

    DrawLines(coneVertices.data(), (uint32_t)coneVertices.size());
}

void GizmoLinePass::DrawBoundingFrustum(const DirectX::BoundingFrustum& frustum, const Mathf::Color4& color)
{
    using namespace DirectX;
    using namespace Mathf;

    XMFLOAT3 corners[BoundingFrustum::CORNER_COUNT];
    frustum.GetCorners(reinterpret_cast<XMFLOAT3*>(corners));

    LineVertex vertices[24] = {};

    vertices[0] = { corners[0], color };
    vertices[1] = { corners[1], color };
    vertices[2] = { corners[1], color };
    vertices[3] = { corners[2], color };
    vertices[4] = { corners[2], color };
    vertices[5] = { corners[3], color };
    vertices[6] = { corners[3], color };
    vertices[7] = { corners[0], color };

    vertices[8] = { corners[0], color };
    vertices[9] = { corners[4], color };
    vertices[10] = { corners[1], color };
    vertices[11] = { corners[5], color };
    vertices[12] = { corners[2], color };
    vertices[13] = { corners[6], color };
    vertices[14] = { corners[3], color };
    vertices[15] = { corners[7], color };

    vertices[16] = { corners[4], color };
    vertices[17] = { corners[5], color };
    vertices[18] = { corners[5], color };
    vertices[19] = { corners[6], color };
    vertices[20] = { corners[6], color };
    vertices[21] = { corners[7], color };
    vertices[22] = { corners[7], color };
    vertices[23] = { corners[4], color };

    DrawLines(vertices, 24);
}
