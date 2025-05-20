#include "../ShaderSystem.h"
#include "SpawnModuleCS.h"

void SpawnModuleCS::Initialize()
{
    m_uniform = std::uniform_real_distribution<float>(0.0f, 1.0f);

    // ��ƼŬ ���ø� �� ���� �ʱ�ȭ
    m_particleTemplate.age = 0.0f;
    m_particleTemplate.lifeTime = 15.0f;
    m_particleTemplate.rotation = 0.0f;
    m_particleTemplate.rotatespeed = 0.0f;
    m_particleTemplate.size = float2(0.3f, 0.3f);
    m_particleTemplate.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    m_particleTemplate.velocity = float3(0.0f, 0.0f, 0.0f);
    m_particleTemplate.acceleration = float3(0.0f, -9.8f, 0.0f);

    // �߰� �Ӽ� �ʱ�ȭ (���ø����� ���� �Ӽ�)
    m_minVerticalVelocity = 0.0f;
    m_maxVerticalVelocity = 0.0f;
    m_horizontalVelocityRange = 0.0f;
    m_emitterSize = float3(1, 1, 1);
    m_emitterRadius = 1.0f;
    m_templateDirty = true;
    m_paramsDirty = true;

    m_computeShader = ShaderSystem->ComputeShaders["SpawnModule"].GetShader();
    InitializeCompute();
}

void SpawnModuleCS::Update(float delta, std::vector<ParticleData>& particles)
{
    DirectX11::BeginEvent(L"SpawnModuleCS");
    m_particlesCapacity = particles.size();

    // ��� ���� ������Ʈ
    UpdateConstantBuffers(delta);

    // ��ǻƮ ���̴� ����
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // ��� ���� ����
    ID3D11Buffer* constantBuffers[] = { m_spawnParamsBuffer, m_templateBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, constantBuffers);

    // ���̴� �Է� ���ҽ� ����
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // ���̴� ��� ���ҽ� ����
    ID3D11UnorderedAccessView* uavs[] = {
        m_outputUAV,           // ��ƼŬ ������ ��� (u0)
        m_randomCounterUAV,    // ���� ī���� (u1)
        m_timeUAV,             // �ð� ���� (u2)
        m_spawnCounterUAV,     // ���� ī���� (u3)
        m_inactiveIndicesUAV,  // ��Ȱ�� �ε��� (u4)
        m_inactiveCountUAV     // ��Ȱ�� ī���� (u5)
    };

    // UAV �ʱ� ī���� ��
    UINT initCounts[] = { 0, 0, 0, 0, 0, 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 6, uavs, initCounts);

    // ��ǻƮ ���̴� ���� (������ �׷� ���)
    UINT numThreadGroups = (std::max<UINT>)(1, (static_cast<UINT>(particles.size()) + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE);
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // ���ҽ� ����
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 6, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 2, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void SpawnModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particlesCapacity)
    {
        m_particlesCapacity = max;
        m_paramsDirty = true;  // �Ķ���� ������Ʈ�� �ʿ��ϴٰ� ǥ��
    }
}

bool SpawnModuleCS::InitializeCompute()
{
    // ��� ���� ����
    D3D11_BUFFER_DESC spawnParamsDesc = {};
    spawnParamsDesc.ByteWidth = sizeof(SpawnParams);
    spawnParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    spawnParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    spawnParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&spawnParamsDesc, nullptr, &m_spawnParamsBuffer);
    if (FAILED(hr))
        return false;

    D3D11_BUFFER_DESC templateDesc = {};
    templateDesc.ByteWidth = sizeof(ParticleTemplateParams);
    templateDesc.Usage = D3D11_USAGE_DYNAMIC;
    templateDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    templateDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    hr = DeviceState::g_pDevice->CreateBuffer(&templateDesc, nullptr, &m_templateBuffer);
    if (FAILED(hr))
        return false;

    // ���� ī���� ���� ����
    D3D11_BUFFER_DESC counterDesc = {};
    counterDesc.ByteWidth = sizeof(UINT);
    counterDesc.Usage = D3D11_USAGE_DEFAULT;
    counterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    counterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    counterDesc.StructureByteStride = sizeof(UINT);

    std::random_device rd;
    UINT initialSeed = rd();
    D3D11_SUBRESOURCE_DATA initialData = {};
    initialData.pSysMem = &initialSeed;

    hr = DeviceState::g_pDevice->CreateBuffer(&counterDesc, &initialData, &m_randomCounterBuffer);
    if (FAILED(hr))
        return false;

    // ���� ī���� UAV ����
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = 1;
    uavDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_randomCounterBuffer, &uavDesc, &m_randomCounterUAV);
    if (FAILED(hr))
        return false;

    // �ð� ���� ���� (���� �ð� �����)
    D3D11_BUFFER_DESC timeDesc = {};
    timeDesc.ByteWidth = sizeof(float);
    timeDesc.Usage = D3D11_USAGE_DEFAULT;
    timeDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    timeDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    timeDesc.StructureByteStride = sizeof(float);

    // �ʱ� ������ ���� m_Time ����
    float initialTime = 0.0f; // �ʱ� ���� 0���� ����
    D3D11_SUBRESOURCE_DATA timeData = {};
    timeData.pSysMem = &initialTime;

    hr = DeviceState::g_pDevice->CreateBuffer(&timeDesc, &timeData, &m_timeBuffer);
    if (FAILED(hr))
        return false;

    // �ð� ������ UAV ����
    D3D11_UNORDERED_ACCESS_VIEW_DESC timeUAVDesc = {};
    timeUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    timeUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    timeUAVDesc.Buffer.FirstElement = 0;
    timeUAVDesc.Buffer.NumElements = 1;
    timeUAVDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_timeBuffer, &timeUAVDesc, &m_timeUAV);
    if (FAILED(hr))
        return false;

    // ���� ī���� ���� ���� - ���� ������ ����
    D3D11_BUFFER_DESC spawnCounterDesc = {};
    spawnCounterDesc.ByteWidth = sizeof(UINT);  // ���� ������ ����
    spawnCounterDesc.Usage = D3D11_USAGE_DEFAULT;
    spawnCounterDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    spawnCounterDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    spawnCounterDesc.StructureByteStride = sizeof(UINT);

    UINT initialSpawnCounter = 0;
    D3D11_SUBRESOURCE_DATA spawnCounterData = {};
    spawnCounterData.pSysMem = &initialSpawnCounter;

    hr = DeviceState::g_pDevice->CreateBuffer(&spawnCounterDesc, &spawnCounterData, &m_spawnCounterBuffer);
    if (FAILED(hr))
        return false;

    // ���� ī���� UAV ����
    D3D11_UNORDERED_ACCESS_VIEW_DESC spawnCounterUAVDesc = {};
    spawnCounterUAVDesc.Format = DXGI_FORMAT_UNKNOWN;
    spawnCounterUAVDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    spawnCounterUAVDesc.Buffer.FirstElement = 0;
    spawnCounterUAVDesc.Buffer.NumElements = 1;  // ���� ������ ����
    spawnCounterUAVDesc.Buffer.Flags = 0;

    hr = DeviceState::g_pDevice->CreateUnorderedAccessView(m_spawnCounterBuffer, &spawnCounterUAVDesc, &m_spawnCounterUAV);
    if (FAILED(hr))
        return false;

    // �ð� ���� �ʱ�ȭ
    float zeroTime = 0.0f;
    DeviceState::g_pDeviceContext->UpdateSubresource(m_timeBuffer, 0, nullptr, &zeroTime, 0, 0);

    // ���� ī���� �ʱ�ȭ
    UINT zeroCounter = 0;
    DeviceState::g_pDeviceContext->UpdateSubresource(m_spawnCounterBuffer, 0, nullptr, &zeroCounter, 0, 0);

    m_isInitialized = true;
    return true;
}

void SpawnModuleCS::UpdateConstantBuffers(float delta)
{
    // ���� �Ķ���� ������Ʈ
    if (m_paramsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_spawnParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            SpawnParams* params = reinterpret_cast<SpawnParams*>(mappedResource.pData);
            params->spawnRate = m_spawnRate;
            params->deltaTime = delta;
            params->emitterType = static_cast<int>(m_emitterType);

            // �̹��� ũ�� �Ķ���� ���� 
            params->emitterSize = m_emitterSize; // ���� ���� �Ҵ� (��� ������ �߰� �ʿ�)
            params->emitterRadius = m_emitterRadius; // ��� ������ �߰� �ʿ�
            params->maxParticles = static_cast<UINT>(m_particlesCapacity);

            DeviceState::g_pDeviceContext->Unmap(m_spawnParamsBuffer, 0);
            m_paramsDirty = false;
        }
    }

    // ��ƼŬ ���ø� ������Ʈ
    if (m_templateDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_templateBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            ParticleTemplateParams* templateParams = reinterpret_cast<ParticleTemplateParams*>(mappedResource.pData);

            // ��ƼŬ ���ø����� �� ���� ����
            templateParams->lifeTime = m_particleTemplate.lifeTime;
            templateParams->rotateSpeed = m_particleTemplate.rotatespeed;
            templateParams->size = m_particleTemplate.size;
            templateParams->color = m_particleTemplate.color;
            templateParams->velocity = m_particleTemplate.velocity;
            templateParams->acceleration = m_particleTemplate.acceleration;

            // �߰� �Ӽ�
            templateParams->minVerticalVelocity = m_minVerticalVelocity;
            templateParams->maxVerticalVelocity = m_maxVerticalVelocity;
            templateParams->horizontalVelocityRange = m_horizontalVelocityRange;

            DeviceState::g_pDeviceContext->Unmap(m_templateBuffer, 0);
            m_templateDirty = false;
        }
    }
}

void SpawnModuleCS::Release()
{
    // ���� ��� ���� ���ҽ� ����
    if (m_computeShader) m_computeShader->Release();
    if (m_spawnParamsBuffer) m_spawnParamsBuffer->Release();
    if (m_templateBuffer) m_templateBuffer->Release();
    if (m_randomCounterBuffer) m_randomCounterBuffer->Release();
    if (m_randomCounterUAV) m_randomCounterUAV->Release();
    if (m_timeBuffer) m_timeBuffer->Release();
    if (m_timeUAV) m_timeUAV->Release();
    if (m_spawnCounterBuffer) m_spawnCounterBuffer->Release();
    if (m_spawnCounterUAV) m_spawnCounterUAV->Release();
    if (m_activeCountStagingBuffer) m_activeCountStagingBuffer->Release();

    // ��� ������ �ʱ�ȭ
    m_computeShader = nullptr;
    m_spawnParamsBuffer = nullptr;
    m_templateBuffer = nullptr;
    m_randomCounterBuffer = nullptr;
    m_randomCounterUAV = nullptr;
    m_timeBuffer = nullptr;
    m_timeUAV = nullptr;
    m_spawnCounterBuffer = nullptr;
    m_spawnCounterUAV = nullptr;
    m_activeCountStagingBuffer = nullptr;

    // �ܺ� ���� ���� �ʱ�ȭ (���� ���۴� ���⼭ �������� ����)
    m_inputSRV = nullptr;
    m_outputUAV = nullptr;
    m_inactiveIndicesUAV = nullptr;
    m_inactiveCountUAV = nullptr;

    m_isInitialized = false;
} 