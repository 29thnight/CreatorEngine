#include "TrailRenderModule.h"
#include "ShaderSystem.h"
#include "DataSystem.h"

TrailRenderModule::TrailRenderModule()
    : m_trailModule(nullptr)
    , m_particleSRV(nullptr)
    , m_instanceCount(0)
{
    memset(&m_constantBufferData, 0, sizeof(TrailConstantBuffer));
}

TrailRenderModule::~TrailRenderModule()
{
    Release();
}

void TrailRenderModule::Initialize()
{
    m_pso = std::make_unique<PipelineStateObject>();

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
        DirectX11::DeviceStates->g_pDevice->CreateBlendState(&blendDesc, &m_pso->m_blendState)
    );

    // 래스터라이저 스테이트
    CD3D11_RASTERIZER_DESC rasterizerDesc{ CD3D11_DEFAULT() };
    rasterizerDesc.CullMode = D3D11_CULL_NONE;
    rasterizerDesc.FillMode = D3D11_FILL_SOLID;
    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateRasterizerState(&rasterizerDesc, &m_pso->m_rasterizerState)
    );

    // 깊이 스텐실 스테이트
    CD3D11_DEPTH_STENCIL_DESC depthDesc{ CD3D11_DEFAULT() };
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthEnable = true;
    depthDesc.DepthFunc = D3D11_COMPARISON_LESS;
    DirectX11::DeviceStates->g_pDevice->CreateDepthStencilState(&depthDesc, &m_pso->m_depthStencilState);

    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // 셰이더 설정 (TrailVertex를 렌더링하는 셰이더)
    m_pso->m_vertexShader = &ShaderSystem->VertexShaders["TrailVertex"];

    // 나중에 이펙트 별로 셰이더 설정할수있게하는 함수 추가할것 (현재는 그냥 고정)
    m_pso->m_pixelShader = &ShaderSystem->PixelShaders["Dash"];

    // 입력 레이아웃 (TrailVertex 구조체)
    D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 }
    };

    DirectX11::ThrowIfFailed(
        DirectX11::DeviceStates->g_pDevice->CreateInputLayout(
            vertexLayoutDesc,
            _countof(vertexLayoutDesc),
            m_pso->m_vertexShader->GetBufferPointer(),
            m_pso->m_vertexShader->GetBufferSize(),
            &m_pso->m_inputLayout
        )
    );

    // 샘플러 설정
    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    m_pso->m_samplers.push_back(linearSampler);

    // 상수 버퍼 생성
    m_constantBuffer = DirectX11::CreateBuffer(
        sizeof(TrailConstantBuffer),
        D3D11_BIND_CONSTANT_BUFFER,
        &m_constantBufferData
    );
}

void TrailRenderModule::Release()
{
    m_constantBuffer.Reset();
    m_trailModule = nullptr;
    m_particleSRV = nullptr;
    m_instanceCount = 0;
    m_assignedTexture = nullptr;
}

void TrailRenderModule::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
    if (!m_enabled || !m_trailModule)
        return;

    // TrailGenerateModule에서 생성된 메쉬 데이터 가져오기
    ID3D11Buffer* vertexBuffer = m_trailModule->GetVertexBuffer();
    ID3D11Buffer* indexBuffer = m_trailModule->GetIndexBuffer();
    UINT maxIndices = m_trailModule->GetMaxIndexCount();
    if (maxIndices == 0) return;


    // 렌더링할 데이터가 없으면 리턴
    if (!vertexBuffer || !indexBuffer)
        return;

    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

    // 상수 버퍼 업데이트
    UpdateConstantBuffer(world, view, projection);
    deviceContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 텍스처 바인딩
    if (m_assignedTexture && m_assignedTexture->m_pSRV)
    {
        deviceContext->PSSetShaderResources(0, 1, &m_assignedTexture->m_pSRV);
    }

    // 정점/인덱스 버퍼 바인딩
    UINT stride = sizeof(TrailVertex);
    UINT offset = 0;
    deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    deviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);


    // 렌더링 실행
    deviceContext->DrawIndexed(maxIndices, 0, 0);

    // 리소스 정리
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    deviceContext->PSSetShaderResources(0, 1, nullSRV);
}

void TrailRenderModule::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
    m_particleSRV = particleSRV;
    m_instanceCount = instanceCount;
}

void TrailRenderModule::SetupRenderTarget(RenderPassData* renderData)
{
    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;
    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    deviceContext->OMSetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);
}

void TrailRenderModule::SetTexture(Texture* texture)
{
    m_assignedTexture = texture;
}

void TrailRenderModule::SetTrailGenerateModule(TrailGenerateModule* trailModule)
{
    m_trailModule = trailModule;
}

void TrailRenderModule::SetCameraPosition(const Mathf::Vector3& position)
{
    m_constantBufferData.cameraPosition = position;
}

void TrailRenderModule::UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view, const Mathf::Matrix& projection)
{
    m_constantBufferData.world = world;
    m_constantBufferData.view = view;
    m_constantBufferData.projection = projection;
    m_constantBufferData.time = Time->GetTotalSeconds();

    DirectX11::UpdateBuffer(m_constantBuffer.Get(), &m_constantBufferData);
}

void TrailRenderModule::ResetForReuse()
{
    if (!m_enabled)
        return;

    m_instanceCount = 0;
    m_isRendering = false;
    m_gpuWorkPending = false;
    m_particleSRV = nullptr;
    m_assignedTexture = nullptr;

    // 상수 버퍼 초기화
    if (m_constantBuffer)
    {
        m_constantBufferData = {};
        m_constantBufferData.world = Mathf::Matrix::Identity;
        m_constantBufferData.view = Mathf::Matrix::Identity;
        m_constantBufferData.projection = Mathf::Matrix::Identity;
    }
}

bool TrailRenderModule::IsReadyForReuse() const
{
    bool ready = !m_isRendering && !m_gpuWorkPending.load();
    bool resourcesValid = m_constantBuffer != nullptr && m_pso != nullptr;

    return ready && resourcesValid;
}

void TrailRenderModule::WaitForGPUCompletion()
{
    m_gpuWorkPending = false;
}

nlohmann::json TrailRenderModule::SerializeData() const
{
    nlohmann::json json;

    // 텍스처 정보
    json["texture"] = {
        {"hasTexture", m_assignedTexture != nullptr}
    };

    if (m_assignedTexture)
    {
        json["texture"]["name"] = m_assignedTexture->m_name;
        json["texture"]["assigned"] = true;
    }

    // 상수 버퍼 데이터
    json["constantBuffer"] = {
        {"cameraPosition", {
            {"x", m_constantBufferData.cameraPosition.x},
            {"y", m_constantBufferData.cameraPosition.y},
            {"z", m_constantBufferData.cameraPosition.z}
        }},
        {"time", m_constantBufferData.time}
    };

    // 렌더링 상태
    json["renderState"] = {
        {"instanceCount", m_instanceCount}
    };

    // Trail 모듈 연결 정보
    json["trailModule"] = {
        {"connected", m_trailModule != nullptr}
    };

    return json;
}

void TrailRenderModule::DeserializeData(const nlohmann::json& json)
{
    // 텍스처 정보 복원
    if (json.contains("texture"))
    {
        const auto& textureJson = json["texture"];

        if (textureJson.contains("name"))
        {
            std::string textureName = textureJson["name"];

            if (textureName.find('.') == std::string::npos)
            {
                textureName += ".png";
            }

            m_assignedTexture = DataSystems->LoadTexture(textureName);

            if (m_assignedTexture) {
                std::string nameWithoutExtension = file::path(textureName).stem().string();
                m_assignedTexture->m_name = nameWithoutExtension;
            }
        }
    }

    // 상수 버퍼 데이터 복원
    if (json.contains("constantBuffer"))
    {
        const auto& cbJson = json["constantBuffer"];

        if (cbJson.contains("cameraPosition"))
        {
            const auto& camPosJson = cbJson["cameraPosition"];
            Mathf::Vector3 cameraPosition(
                camPosJson.value("x", 0.0f),
                camPosJson.value("y", 0.0f),
                camPosJson.value("z", 0.0f)
            );
            SetCameraPosition(cameraPosition);
        }

        if (cbJson.contains("time"))
        {
            m_constantBufferData.time = cbJson["time"];
        }
    }

    // 렌더링 상태 복원
    if (json.contains("renderState"))
    {
        const auto& renderJson = json["renderState"];

        if (renderJson.contains("instanceCount"))
        {
            m_instanceCount = renderJson["instanceCount"];
        }
    }

    if (!m_pso) {
        Initialize();
    }
}

std::string TrailRenderModule::GetModuleType() const
{
    return "TrailRenderModule";
}