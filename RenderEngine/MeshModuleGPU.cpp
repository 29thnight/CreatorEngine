#include "MeshModuleGPU.h"

void MeshModuleGPU::Initialize()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_meshType = MeshType::None;
    m_instanceCount = 0;
    m_particleSRV = nullptr;
    m_currentMesh = nullptr;
    m_ownsMesh = false;

    // 블렌드 스테이트 (알파 블렌딩)
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateBlendState(&blendDesc, &m_pso->m_blendState)
    );

    // 래스터라이저 스테이트
    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
    rasterizerDesc.CullMode = D3D11_CULL_BACK;
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    DirectX11::ThrowIfFailed(
        DeviceState::g_pDevice->CreateRasterizerState(&rasterizerDesc, &m_pso->m_rasterizerState)
    );

    // 깊이 스텐실 스테이트
    CD3D11_DEPTH_STENCIL_DESC depthDesc{ CD3D11_DEFAULT() };
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthEnable = true;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
    DeviceState::g_pDevice->CreateDepthStencilState(&depthDesc, &m_pso->m_depthStencilState);

    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // 셰이더 설정
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["MeshParticle"];
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["MeshParticle"];

    // 입력 레이아웃 (기존 Vertex 구조체 사용)
    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BINORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDINDICES", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BLENDWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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

    // 샘플러 설정
    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto pointSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(pointSampler);

    // 상수 버퍼 생성
    m_constantBuffer = DirectX11::CreateBuffer(
        sizeof(MeshConstantBuffer),
        D3D11_BIND_CONSTANT_BUFFER,
        &m_constantBufferData
    );

    // 기본 큐브 메시 설정
    SetMeshType(MeshType::Cube);
}

void MeshModuleGPU::CreateCubeMesh()
{
    auto vertices = PrimitiveCreator::CubeVertices();
    auto indices = PrimitiveCreator::CubeIndices();

    m_currentMesh = new Mesh("CubeParticle", vertices, indices);
    m_ownsMesh = true;
}

void MeshModuleGPU::CreateSphereMesh()
{
    // 현재는 큐브만 지원
}

void MeshModuleGPU::SetMeshType(MeshType type)
{
    if (m_meshType == type) return;

    // 기존 메시 정리 (소유권이 있는 경우만)
    if (m_ownsMesh && m_currentMesh)
    {
        delete m_currentMesh;
        m_currentMesh = nullptr;
        m_ownsMesh = false;
    }

    m_meshType = type;

    switch (type)
    {
    case MeshType::Cube:
        CreateCubeMesh();
        break;
    case MeshType::Sphere:
        CreateSphereMesh();
        break;
    case MeshType::Custom:
        // SetCustomMesh에서 설정됨
        break;
    }
}

void MeshModuleGPU::SetCustomMesh(Mesh* customMesh)
{
    // 기존 메시 정리 (소유권이 있는 경우만)
    if (m_ownsMesh && m_currentMesh)
    {
        delete m_currentMesh;
    }

    m_meshType = MeshType::Custom;
    m_currentMesh = customMesh;
    m_ownsMesh = false;  // 외부에서 받은 메시이므로 소유권 없음
}

void MeshModuleGPU::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
    m_particleSRV = particleSRV;
    m_instanceCount = instanceCount;
}

void MeshModuleGPU::SetCameraPosition(const Mathf::Vector3& position)
{
    m_constantBufferData.cameraPosition = position;
}

void MeshModuleGPU::UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view,
    const Mathf::Matrix& projection)
{
    m_constantBufferData.world = world;
    m_constantBufferData.view = view;
    m_constantBufferData.projection = projection;

    DirectX11::UpdateBuffer(m_constantBuffer.Get(), &m_constantBufferData);
}

void MeshModuleGPU::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
    if (!m_currentMesh || !m_particleSRV || m_instanceCount == 0)
        return;

    auto& deviceContext = DeviceState::g_pDeviceContext;

    // 렌더 상태 저장
    SaveRenderState();

    // 상수 버퍼 업데이트
    UpdateConstantBuffer(world, view, projection);
    deviceContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 파티클 SRV 바인딩
    deviceContext->VSSetShaderResources(0, 1, &m_particleSRV);

    // 메시의 버텍스/인덱스 버퍼 바인딩
    UINT stride = m_currentMesh->GetStride();
    UINT offset = 0;
    auto vertexBuffer = m_currentMesh->GetVertexBuffer();
    auto indexBuffer = m_currentMesh->GetIndexBuffer();

    deviceContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    deviceContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    // 인스턴싱 렌더링
    const auto& indices = m_currentMesh->GetIndices();
    deviceContext->DrawIndexedInstanced(indices.size(), m_instanceCount, 0, 0, 0);

    // 리소스 정리
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    deviceContext->VSSetShaderResources(0, 1, nullSRV);

    // 렌더 상태 복원
    RestoreRenderState();
}

void MeshModuleGPU::SetTexture(Texture* texture)
{
}

void MeshModuleGPU::Release()
{
    // 소유권이 있는 메시만 삭제
    if (m_ownsMesh && m_currentMesh)
    {
        delete m_currentMesh;
    }

    m_constantBuffer.Reset();
    m_currentMesh = nullptr;
    m_ownsMesh = false;
    m_particleSRV = nullptr;
    m_instanceCount = 0;
}