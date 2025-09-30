#include "TrailModuleCS.h"
#include "ShaderSystem.h"
#include "DeviceState.h"
#include "EffectSerializer.h"

TrailModuleCS::TrailModuleCS()
    : m_computeShader(nullptr)
    , m_paramsBuffer(nullptr)
    , m_trailDataBuffer(nullptr)
    , m_trailDataUAV(nullptr)
    , m_sourceParticlesSRV(nullptr)
    , m_paramsDirty(true)
    , m_particleCapacity(0)
    , m_sourceParticleCount(0)
{
    m_stage = ModuleStage::SPAWN;

    m_params.deltaTime = 0.0f;
    m_params.currentTime = 0.0f;
    m_params.maxTrailParticles = 0;
    m_params.trailLifetime = 2.0f;
    m_params.sourceParticleCount = 0;
    m_params.minDistance = 0.1f;
    m_params.size = XMFLOAT2(1.0f, 1.0f);
    m_params.color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
}

TrailModuleCS::~TrailModuleCS()
{
    Release();
}

void TrailModuleCS::Initialize()
{
    if (m_isInitialized)
        return;

    if (!InitializeComputeShader())
    {
        OutputDebugStringA("Failed to initialize trail spawn compute shader\n");
        return;
    }

    if (!CreateConstantBuffers())
    {
        OutputDebugStringA("Failed to create trail spawn constant buffers\n");
        return;
    }

    if (!CreateUtilityBuffers())
    {
        OutputDebugStringA("Failed to create trail spawn utility buffers\n");
        return;
    }

    m_isInitialized = true;
}

void TrailModuleCS::Update(float deltaTime)
{
    if (!m_enabled || !m_isInitialized)
        return;

    DirectX11::BeginEvent(L"TrailModuleCS Update");

    m_params.deltaTime = deltaTime;
    m_params.currentTime += deltaTime;
    m_params.maxTrailParticles = m_particleCapacity;
    m_params.sourceParticleCount = m_sourceParticleCount;
    m_paramsDirty = true;

    UpdateConstantBuffers(deltaTime);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    ID3D11Buffer* constantBuffers[] = { m_paramsBuffer };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    ID3D11ShaderResourceView* srvs[] = { m_sourceParticlesSRV };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV, m_trailDataUAV };
    UINT initCounts[] = { 0, 0 };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

    UINT numThreadGroups = (m_particleCapacity + 255) / 256;
    DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void TrailModuleCS::Release()
{
    ReleaseResources();
    m_isInitialized = false;
}

bool TrailModuleCS::InitializeComputeShader()
{
    m_computeShader = ShaderSystem->ComputeShaders["TrailModule"].GetShader();
    return m_computeShader != nullptr;
}

bool TrailModuleCS::CreateConstantBuffers()
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TrailParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_paramsBuffer);
    return SUCCEEDED(hr);
}

bool TrailModuleCS::CreateUtilityBuffers()
{
    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(TrailSegment) * m_particleCapacity;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    bufferDesc.StructureByteStride = sizeof(TrailSegment);

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_trailDataBuffer);
    if (FAILED(hr))
        return false;

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = m_particleCapacity;

    hr = DirectX11::DeviceStates->g_pDevice->CreateUnorderedAccessView(m_trailDataBuffer, &uavDesc, &m_trailDataUAV);
    return SUCCEEDED(hr);
}

void TrailModuleCS::UpdateConstantBuffers(float deltaTime)
{
    if (!m_paramsDirty)
        return;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_paramsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

    if (SUCCEEDED(hr))
    {
        memcpy(mappedResource.pData, &m_params, sizeof(TrailParams));
        DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_paramsBuffer, 0);
        m_paramsDirty = false;
    }
}

void TrailModuleCS::ReleaseResources()
{
    if (m_computeShader) { m_computeShader->Release(); m_computeShader = nullptr; }
    if (m_paramsBuffer) { m_paramsBuffer->Release(); m_paramsBuffer = nullptr; }
    if (m_trailDataBuffer) { m_trailDataBuffer->Release(); m_trailDataBuffer = nullptr; }
    if (m_trailDataUAV) { m_trailDataUAV->Release(); m_trailDataUAV = nullptr; }
}

void TrailModuleCS::ResetForReuse()
{
    if (!m_enabled)
        return;

    std::lock_guard<std::mutex> lock(m_resetMutex);

    m_params.currentTime = 0.0f;
    m_params.deltaTime = 0.0f;
    m_paramsDirty = true;

    if (m_trailDataBuffer)
    {
        std::vector<TrailSegment> zeroData(m_particleCapacity, TrailSegment());
        DirectX11::DeviceStates->g_pDeviceContext->UpdateSubresource(m_trailDataBuffer, 0, nullptr, zeroData.data(), 0, 0);
    }
}

bool TrailModuleCS::IsReadyForReuse() const
{
    return m_isInitialized && m_paramsBuffer != nullptr && m_trailDataBuffer != nullptr;
}

void TrailModuleCS::OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition)
{
    m_paramsDirty = true;
}

void TrailModuleCS::OnSystemResized(UINT maxParticles)
{
    if (maxParticles != m_particleCapacity)
    {
        m_particleCapacity = maxParticles;
        m_params.maxTrailParticles = maxParticles;
        m_paramsDirty = true;

        ReleaseResources();
        CreateUtilityBuffers();
    }
}

void TrailModuleCS::SetSourceParticleCount(UINT count)
{
    m_sourceParticleCount = count;
    m_params.sourceParticleCount = count;
    m_paramsDirty = true;
}

void TrailModuleCS::SetSourceParticlesSRV(ID3D11ShaderResourceView* srv)
{
    m_sourceParticlesSRV = srv;
}

nlohmann::json TrailModuleCS::SerializeData() const
{
    nlohmann::json json;

    json["trailParams"] = {
        {"trailLifetime", m_params.trailLifetime},
        {"minDistance", m_params.minDistance},
        {"size", {m_params.size.x, m_params.size.y}},
        {"color", EffectSerializer::SerializeXMFLOAT4(m_params.color)}
    };

    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}

void TrailModuleCS::DeserializeData(const nlohmann::json& json)
{
    try
    {
        if (json.contains("trailParams"))
        {
            const auto& params = json["trailParams"];
            if (params.contains("trailLifetime"))
                m_params.trailLifetime = params["trailLifetime"];
            if (params.contains("minDistance"))
                m_params.minDistance = params["minDistance"];
            if (params.contains("size"))
            {
                m_params.size.x = params["size"][0];
                m_params.size.y = params["size"][1];
            }
            if (params.contains("color"))
                m_params.color = EffectSerializer::DeserializeXMFLOAT4(params["color"]);
        }

        if (json.contains("state") && json["state"].contains("particleCapacity"))
        {
            m_particleCapacity = json["state"]["particleCapacity"];
        }

        if (!m_isInitialized)
            Initialize();

        m_paramsDirty = true;
    }
    catch (const std::exception& e)
    {
        OutputDebugStringA(("TrailModuleCS deserialization error: " + std::string(e.what()) + "\n").c_str());
    }
}

std::string TrailModuleCS::GetModuleType() const
{
    return "TrailModuleCS";
}