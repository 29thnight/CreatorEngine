#include "ShaderSystem.h"
#include "MovementModuleCS.h"

void MovementModuleCS::Initialize()
{
    // 컴퓨트 셰이더 로드 (ShaderSystem에서 관리되는 컴퓨트 셰이더 가져오기)
    m_computeShader = ShaderSystem->ComputeShaders["MovementModule"].GetShader();
    InitializeCompute();
}

void MovementModuleCS::Update(float delta, std::vector<ParticleData>& particles)
{
    DirectX11::BeginEvent(L"MovementModuleCS");
    // 파티클 배열 크기 저장
    m_particlesCapacity = particles.size();

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(delta);

    // 컴퓨트 셰이더 설정
    DeviceState::g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 설정
    ID3D11Buffer* constantBuffers[] = { m_movementParamsBuffer };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // 셰이더에서 입력으로 사용할 버퍼 설정
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // 셰이더에서 출력으로 사용할 버퍼 설정
    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
    UINT initCounts[] = { 0 };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    // 컴퓨트 셰이더 실행
    // 각 스레드 그룹은 최대 256개 스레드를 포함 (일반적인 DirectX 구현)
    UINT numThreadGroups = (std::max<UINT>)(1, (static_cast<UINT>(particles.size()) + THREAD_GROUP_SIZE - 1) / THREAD_GROUP_SIZE);
    DeviceState::g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 해제
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetShaderResources(0, 1, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DeviceState::g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DeviceState::g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void MovementModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particlesCapacity)
    {
        m_particlesCapacity = max;
        m_paramsDirty = true;
    }
}

bool MovementModuleCS::InitializeCompute()
{
    // 상수 버퍼 생성
    D3D11_BUFFER_DESC movementParamsDesc = {};
    movementParamsDesc.ByteWidth = sizeof(MovementParams);
    movementParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    movementParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    movementParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&movementParamsDesc, nullptr, &m_movementParamsBuffer);
    if (FAILED(hr))
        return false;

    m_isInitialized = true;
    return true;
}

void MovementModuleCS::UpdateConstantBuffers(float delta)
{
    // 파라미터 업데이트가 필요한 경우에만 상수 버퍼 업데이트
    if (m_paramsDirty)
    {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = DeviceState::g_pDeviceContext->Map(m_movementParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

        if (SUCCEEDED(hr))
        {
            MovementParams* params = reinterpret_cast<MovementParams*>(mappedResource.pData);
            params->deltaTime = delta;
            params->gravityStrength = m_gravityStrength;
            params->useGravity = m_gravity ? 1 : 0;
            params->easingEnabled = m_easingEnabled ? 1 : 0;
            params->easingType = m_easingType;

            DeviceState::g_pDeviceContext->Unmap(m_movementParamsBuffer, 0);
            m_paramsDirty = false;
        }
    }
}

void MovementModuleCS::Release()
{
    // 리소스 해제
    if (m_computeShader) m_computeShader->Release();
    if (m_movementParamsBuffer) m_movementParamsBuffer->Release();

    // 포인터 초기화
    m_computeShader = nullptr;
    m_movementParamsBuffer = nullptr;
    m_inputSRV = nullptr;
    m_outputUAV = nullptr;

    m_isInitialized = false;
}