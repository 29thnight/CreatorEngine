#include "../ShaderSystem.h"
#include "LifeModuleCS.h"


void LifeModuleCS::Initialize()
{
	m_computeShader = ShaderSystem->ComputeShaders["LifeModule"].GetShader();
	InitializeCompute();
}

void LifeModuleCS::Update(float delta, std::vector<ParticleData>& particles)
{
    DirectX11::BeginEvent(L"LifeModuleCS");

    // ��ƼŬ �迭 ũ�� ����
    m_particlesCapacity = particles.size();

    // ��� ���� ������Ʈ
    UpdateConstantBuffers(delta);

    // ��ǻƮ ���̴� ����
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // ��� ���� ����
    ID3D11Buffer* constantBuffers[] = { m_lifeParamsBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // �Է� ���� ����
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // ��� ���� ���� - ��ƼŬ ������, ��Ȱ�� �ε���, ��Ȱ�� ī����, Ȱ�� ī����
    ID3D11UnorderedAccessView* uavs[] = {
     m_outputUAV,           // ��ƼŬ ������ ���
     m_inactiveIndicesUAV,  // ��Ȱ�� ��ƼŬ �ε��� (Life ��⿡�� ������ ���� ��ƼŬ�� ���⿡ �߰�)
     m_inactiveCountUAV,    // ��Ȱ�� ��ƼŬ ī���� (��Ȱ�� ��ƼŬ�� ���� ����)
     m_activeCountUAV       // Ȱ�� ��ƼŬ ī���� (Ȱ�� ��ƼŬ�� ���� ����)
    };
    UINT initCounts[] = { 0, 0, 0, 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, uavs, initCounts);

    // ��ǻƮ ���̴� ����
    UINT numThreadGroups = (std::max<UINT>)(1, (static_cast<UINT>(particles.size()) + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE);
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // ���ҽ� ����
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr, nullptr, nullptr, nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 4, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();

}

void LifeModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particlesCapacity)
    {
        m_particlesCapacity = max;
        m_paramsDirty = true;
    }
}

bool LifeModuleCS::InitializeCompute()
{
    // ��� ���� ����
    D3D11_BUFFER_DESC lifeParamsDesc = {};
    lifeParamsDesc.ByteWidth = sizeof(LifeParams);
    lifeParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    lifeParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    lifeParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&lifeParamsDesc, nullptr, &m_lifeParamsBuffer);
    if (FAILED(hr))
        return false;

    // CPU �б�� ������¡ ���� ����
    D3D11_BUFFER_DESC stagingDesc = {};
    stagingDesc.ByteWidth = sizeof(UINT);
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    stagingDesc.MiscFlags = 0;

    hr = DeviceState::g_pDevice->CreateBuffer(&stagingDesc, nullptr, &m_activeCountStagingBuffer);
    if (FAILED(hr))
        return false;

    m_isInitialized = true;
    return true;
}

UINT LifeModuleCS::GetActiveParticleCount()
{
    // ���� �ʱ�ȭ���� �ʾҰų� Ȱ�� ī���� UAV�� ���� ���
    if (!m_isInitialized || !m_activeCountUAV)
        return 0;

    // ������¡ ���۰� ������ ����
    if (!m_activeCountStagingBuffer)
    {
        D3D11_BUFFER_DESC stagingDesc = {};
        stagingDesc.ByteWidth = sizeof(UINT);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&stagingDesc, nullptr, &m_activeCountStagingBuffer);
        if (FAILED(hr))
            return 0;
    }

    // ī���� ���ۿ��� ������¡ ���۷� ����
    // �� �κ��� ParticleSystem���� �����ϴ� m_activeCountBuffer�� ���
    // Ȱ�� ī���� UAV�� ����� �Ǵ� ���۸� �����;� ��
    ID3D11Resource* activeCountResource = nullptr;
    m_activeCountUAV->GetResource(&activeCountResource);

    if (activeCountResource)
    {
        DeviceState::g_pDeviceContext->CopyResource(m_activeCountStagingBuffer, activeCountResource);
        activeCountResource->Release();

        // ������¡ ���ۿ��� CPU�� ������ �б�
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_activeCountStagingBuffer, 0, D3D11_MAP_READ, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            UINT count = *reinterpret_cast<UINT*>(mappedResource.pData);
            DeviceState::g_pDeviceContext->Unmap(m_activeCountStagingBuffer, 0);
            return count;
        }
    }

    return 0;
}

void LifeModuleCS::UpdateConstantBuffers(float delta)
{
    // �Ķ���� ������Ʈ�� �ʿ��� ��쿡�� ��� ���� ������Ʈ
    if (m_paramsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_lifeParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            LifeParams* params = reinterpret_cast<LifeParams*>(mappedResource.pData);
            params->deltaTime = delta;
            params->maxParticles = m_particlesCapacity;
            params->resetInactiveCounter = 1; // �����Ӹ��� ��Ȱ�� ī���� ����

            DeviceState::g_pDeviceContext->Unmap(m_lifeParamsBuffer, 0);
            m_paramsDirty = false;
        }
    }
}

void LifeModuleCS::Release()
{
    // ���ҽ� ����
    if (m_computeShader) m_computeShader->Release();
    if (m_lifeParamsBuffer) m_lifeParamsBuffer->Release();

    // ������ �ʱ�ȭ
    m_computeShader = nullptr;
    m_lifeParamsBuffer = nullptr;
    m_inputSRV = nullptr;
    m_outputUAV = nullptr;
    m_inactiveIndicesUAV = nullptr;
    m_inactiveCountUAV = nullptr;
    m_activeCountUAV = nullptr;

    m_isInitialized = false;
}
