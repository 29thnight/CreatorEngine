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

    // 셰이더 이름 설정
    if (m_vertexShaderName == "None") {
        m_vertexShaderName = "TrailVertex";
    }

    if (m_pixelShaderName == "None")
    {
        m_pixelShaderName = "Dash";
    }

    // 렌더 상태 프리셋 설정 (PSO 생성 후에)
    if (m_blendPreset == BlendPreset::None) {
        m_blendPreset = BlendPreset::Alpha;
    }
    if (m_depthPreset == DepthPreset::None)
    {
        m_depthPreset = DepthPreset::ReadOnly;
    }
    if (m_rasterizerPreset == RasterizerPreset::None)
    {
        m_rasterizerPreset = RasterizerPreset::Default;
    }

    // 프리미티브 토폴로지 설정
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // 부모의 함수들로 모든 상태 업데이트
    UpdatePSOShaders();
    UpdatePSORenderStates();

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

    // 다중 텍스처 정리
    ClearTextures();
}

void TrailRenderModule::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
    if (!m_enabled || !m_trailModule)
        return;

    ID3D11Buffer* vertexBuffer = m_trailModule->GetVertexBuffer();
    ID3D11Buffer* indexBuffer = m_trailModule->GetIndexBuffer();
    UINT maxIndices = m_trailModule->GetMaxIndexCount();
    if (maxIndices == 0) return;

    if (!vertexBuffer || !indexBuffer)
        return;

    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

    // 상수 버퍼 업데이트
    UpdateConstantBuffer(world, view, projection);
    deviceContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 다중 텍스처 바인딩 사용
    if (GetTextureCount() > 0) {
        BindTextures();
    }

    // 정점/인덱스 버퍼 바인딩
    UINT stride = sizeof(CTrailVertex);
    UINT offset = 0;
    deviceContext->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
    deviceContext->IASetIndexBuffer(indexBuffer, DXGI_FORMAT_R32_UINT, 0);

    // 렌더링 실행
    deviceContext->DrawIndexed(maxIndices, 0, 0);

    // 리소스 정리 (다중 텍스처 대응)
    if (GetTextureCount() > 0) {
        std::vector<ID3D11ShaderResourceView*> nullSRVs(GetTextureCount(), nullptr);
        deviceContext->PSSetShaderResources(0, nullSRVs.size(), nullSRVs.data());
    }
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

    // 다중 텍스처 초기화
    ClearTextures();

    UpdatePSOShaders();
    UpdatePSORenderStates();

    if (m_constantBuffer) {
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

void TrailRenderModule::UpdatePSOShaders()
{
    RenderModules::UpdatePSOShaders();

    if (m_pso && m_pso->m_vertexShader) {
        if (m_pso->m_inputLayout) {
            m_pso->m_inputLayout = nullptr;
        }

        D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] = {
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
    }
}

nlohmann::json TrailRenderModule::SerializeData() const
{
    nlohmann::json json;

    // 부모의 렌더 상태 직렬화 사용
    json.merge_patch(SerializeRenderStates());

    // 다중 텍스처 직렬화
    json["textures"] = SerializeTextures();

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
    // 부모의 렌더 상태 역직렬화 사용
    DeserializeRenderStates(json);

    // 다중 텍스처 역직렬화
    DeserializeTextures(json);

    // 상수 버퍼 데이터 복원
    if (json.contains("constantBuffer")) {
        const auto& cbJson = json["constantBuffer"];

        if (cbJson.contains("cameraPosition")) {
            const auto& camPosJson = cbJson["cameraPosition"];
            Mathf::Vector3 cameraPosition(
                camPosJson.value("x", 0.0f),
                camPosJson.value("y", 0.0f),
                camPosJson.value("z", 0.0f)
            );
            SetCameraPosition(cameraPosition);
        }

        if (cbJson.contains("time")) {
            m_constantBufferData.time = cbJson["time"];
        }
    }

    // 렌더링 상태 복원
    if (json.contains("renderState")) {
        const auto& renderJson = json["renderState"];

        if (renderJson.contains("instanceCount")) {
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