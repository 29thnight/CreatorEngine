#include "MeshMovementModuleCS.h"
#include "ShaderSystem.h"
#include "EffectSerializer.h"

MeshMovementModuleCS::MeshMovementModuleCS()
	: m_computeShader(nullptr), m_movementParamsBuffer(nullptr),
	m_velocityCurveBuffer(nullptr), m_velocityCurveSRV(nullptr),
	m_impulsesBuffer(nullptr), m_impulsesSRV(nullptr),
	m_paramsDirty(true), m_easingEnable(false), m_currentTime(0.0f)
{
    m_velocityMode = VelocityMode::Constant;

    // 기본값 설정
    m_windData.direction = Mathf::Vector3(1, 0, 0);
    m_windData.baseStrength = 1.0f;
    m_windData.turbulence = 0.5f;
    m_windData.frequency = 1.0f;

    m_orbitalData.center = Mathf::Vector3(0, 0, 0);
    m_orbitalData.radius = 5.0f;
    m_orbitalData.speed = 1.0f;
    m_orbitalData.axis = Mathf::Vector3(0, 1, 0);

    m_explosiveData.initialSpeed = 50.0f;
    m_explosiveData.speedDecay = 2.0f;
    m_explosiveData.randomFactor = 0.4f;
    m_explosiveData.sphereRadius = 1.0f;

    // Size 기본값 추가
    memset(&m_movementParams, 0, sizeof(MeshMovementParams));
}

MeshMovementModuleCS::~MeshMovementModuleCS()
{
	Release();
}

void MeshMovementModuleCS::Initialize()
{
    m_currentTime = 0.0f;
    m_computeShader = ShaderSystem->ComputeShaders["MeshMovementModule"].GetShader();
    InitializeCompute();
}

void MeshMovementModuleCS::Update(float delta)
{
    if (!m_enabled) return;

    if (!m_isInitialized) return;

    DirectX11::BeginEvent(L"MeshMovementModuleCS");

    // 시간 업데이트
    float processedDeltaTime = delta;
    if (m_easingEnable)
    {
        float easingValue = m_easingModule.Update(delta);
        processedDeltaTime = delta * easingValue;
    }

    m_currentTime += processedDeltaTime;

    // Structured Buffer 업데이트 (매번 체크)
    UpdateStructuredBuffers();

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(processedDeltaTime);

    // 컴퓨트 셰이더 설정
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(m_computeShader, nullptr, 0);

    // 상수 버퍼 설정
    ID3D11Buffer* constantBuffers[] = { m_movementParamsBuffer };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, constantBuffers);

    // 셰이더에서 입력으로 사용할 버퍼 설정
    ID3D11ShaderResourceView* srvs[] = { m_inputSRV };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 1, srvs);

    // Velocity Curve와 Impulse 버퍼 항상 바인드 (null이어도)
    ID3D11ShaderResourceView* curveSrvs[] = { m_velocityCurveSRV };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(1, 1, curveSrvs);

    ID3D11ShaderResourceView* impulseSrvs[] = { m_impulsesSRV };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(2, 1, impulseSrvs);

    // 셰이더에서 출력으로 사용할 버퍼 설정
    ID3D11UnorderedAccessView* uavs[] = { m_outputUAV };
    UINT initCounts[] = { 0 };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, uavs, initCounts);

    // 컴퓨트 셰이더 실행
    UINT numThreadGroups = (m_particleCapacity + (THREAD_GROUP_SIZE - 1)) / THREAD_GROUP_SIZE;
    DirectX11::DeviceStates->g_pDeviceContext->Dispatch(numThreadGroups, 1, 1);

    // 리소스 해제
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    ID3D11ShaderResourceView* nullSRVs[] = { nullptr, nullptr, nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetShaderResources(0, 3, nullSRVs);

    ID3D11Buffer* nullBuffers[] = { nullptr };
    DirectX11::DeviceStates->g_pDeviceContext->CSSetConstantBuffers(0, 1, nullBuffers);

    DirectX11::DeviceStates->g_pDeviceContext->CSSetShader(nullptr, nullptr, 0);

    DirectX11::EndEvent();
}

void MeshMovementModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particleCapacity)
    {
        m_particleCapacity = max;
        m_paramsDirty = true;
    }
}

void MeshMovementModuleCS::ResetForReuse()
{
    if (!m_enabled) return;

    // 시간 초기화
    m_currentTime = 0.0f;

    // 더티 플래그 설정 (상수 버퍼 재업데이트 강제)
    m_paramsDirty = true;
}

bool MeshMovementModuleCS::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_movementParamsBuffer != nullptr;
}

void MeshMovementModuleCS::SetEmitterTransform(const Mathf::Vector3& position, const Mathf::Vector3& rotation)
{
}

void MeshMovementModuleCS::Release()
{
    // 리소스 해제
    if (m_computeShader) m_computeShader->Release();
    if (m_movementParamsBuffer) m_movementParamsBuffer->Release();
    if (m_velocityCurveBuffer) m_velocityCurveBuffer->Release();
    if (m_velocityCurveSRV) m_velocityCurveSRV->Release();
    if (m_impulsesBuffer) m_impulsesBuffer->Release();
    if (m_impulsesSRV) m_impulsesSRV->Release();

    // 포인터 초기화
    m_computeShader = nullptr;
    m_movementParamsBuffer = nullptr;
    m_velocityCurveBuffer = nullptr;
    m_velocityCurveSRV = nullptr;
    m_impulsesBuffer = nullptr;
    m_impulsesSRV = nullptr;

    m_isInitialized = false;
}

bool MeshMovementModuleCS::InitializeCompute()
{
    // 상수 버퍼 생성
    D3D11_BUFFER_DESC movementParamsDesc = {};
    movementParamsDesc.ByteWidth = sizeof(MeshMovementParams);
    movementParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    movementParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    movementParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&movementParamsDesc, nullptr, &m_movementParamsBuffer);
    if (FAILED(hr))
        return false;

    m_isInitialized = true;
    return true;
}

void MeshMovementModuleCS::UpdateConstantBuffers(float delta)
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_movementParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        MeshMovementParams* params = reinterpret_cast<MeshMovementParams*>(mappedResource.pData);
        params->deltaTime = delta;
        params->currentTime = m_currentTime;
        params->velocityMode = static_cast<int>(m_velocityMode);
        params->maxParticles = m_particleCapacity;

        // Wind, Orbital, Explosive 데이터 복사
        params->windDirection = m_windData.direction;
        params->windStrength = m_windData.baseStrength;
        params->turbulence = m_windData.turbulence;
        params->frequency = m_windData.frequency;

        params->orbitalCenter = m_orbitalData.center;
        params->orbitalRadius = m_orbitalData.radius;
        params->orbitalSpeed = m_orbitalData.speed;
        params->orbitalAxis = m_orbitalData.axis;

        params->explosiveSpeed = m_explosiveData.initialSpeed;
        params->explosiveDecay = m_explosiveData.speedDecay;
        params->explosiveRandom = m_explosiveData.randomFactor;
        params->explosiveSphere = m_explosiveData.sphereRadius;

        params->velocityCurveSize = static_cast<int>(m_velocityCurve.size());
        params->impulseCount = static_cast<int>(m_impulses.size());

        params->useGravity = m_movementParams.useGravity;
        params->gravityStrength = m_movementParams.gravityStrength;

        params->emitterPosition = m_movementParams.emitterPosition;
        params->emitterRotation = m_movementParams.emitterRotation;

        DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_movementParamsBuffer, 0);

        m_paramsDirty = false;
    }
}

void MeshMovementModuleCS::UpdateStructuredBuffers()
{
    // Velocity Curve 버퍼 업데이트
    if (!m_velocityCurve.empty())
    {
        if (m_velocityCurveBuffer)
        {
            m_velocityCurveBuffer->Release();
            m_velocityCurveBuffer = nullptr;
        }
        if (m_velocityCurveSRV)
        {
            m_velocityCurveSRV->Release();
            m_velocityCurveSRV = nullptr;
        }

        // 버퍼 생성
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(VelocityPoint) * m_velocityCurve.size();
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(VelocityPoint);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = m_velocityCurve.data();

        DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_velocityCurveBuffer);

        // SRV 생성
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = m_velocityCurve.size();

        DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_velocityCurveBuffer, &srvDesc, &m_velocityCurveSRV);
    }

    // Impulse 버퍼 업데이트
    if (!m_impulses.empty())
    {
        if (m_impulsesBuffer)
        {
            m_impulsesBuffer->Release();
            m_impulsesBuffer = nullptr;
        }
        if (m_impulsesSRV)
        {
            m_impulsesSRV->Release();
            m_impulsesSRV = nullptr;
        }

        // 버퍼 생성
        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = sizeof(ImpulseData) * m_impulses.size();
        bufferDesc.Usage = D3D11_USAGE_DEFAULT;
        bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        bufferDesc.StructureByteStride = sizeof(ImpulseData);
        bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = m_impulses.data();

        DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, &initData, &m_impulsesBuffer);

        // SRV 생성
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.NumElements = m_impulses.size();

        DirectX11::DeviceStates->g_pDevice->CreateShaderResourceView(m_impulsesBuffer, &srvDesc, &m_impulsesSRV);
    }
}


void MeshMovementModuleCS::SetUseGravity(bool use)
{
    m_movementParams.useGravity = use; 
    m_paramsDirty = true;
}

bool MeshMovementModuleCS::GetUseGravity() const
{
	return m_movementParams.useGravity;
}

void MeshMovementModuleCS::SetGravityStrength(float strength)
{
    m_movementParams.gravityStrength = strength;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetEasingEnabled(bool enabled)
{
    m_easingEnable = enabled;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetEasing(EasingEffect easingType, StepAnimation animationType, float duration)
{
    m_easingModule.SetEasingType(easingType);
    m_easingModule.SetAnimationType(animationType);
    m_easingModule.SetDuration(duration);
    m_easingEnable = true;
}

void MeshMovementModuleCS::DisableEasing()
{
    m_easingEnable = false;
}

void MeshMovementModuleCS::SetVelocityMode(VelocityMode mode)
{
    m_velocityMode = mode;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetVelocityCurve(const std::vector<VelocityPoint>& curve)
{
    m_velocityCurve = curve;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::AddVelocityPoint(float time, const Mathf::Vector3& velocity, float strength)
{
    VelocityPoint point;
    point.time = time;
    point.velocity = velocity;
    point.strength = strength;

    m_velocityCurve.push_back(point);
    // 시간순으로 정렬
    std::sort(m_velocityCurve.begin(), m_velocityCurve.end(),
        [](const VelocityPoint& a, const VelocityPoint& b) {
            return a.time < b.time;
        });
    m_paramsDirty = true;
}

void MeshMovementModuleCS::AddImpulse(float triggerTime, const Mathf::Vector3& direction, float force, float duration, float impulseRange, UINT impulseType)
{
    ImpulseData impulse;
    impulse.triggerTime = triggerTime;
    impulse.direction = direction;
    impulse.force = force;
    impulse.duration = duration;
    impulse.impulseRange = impulseRange;
    impulse.impulseType = impulseType;

    m_impulses.push_back(impulse);
    std::sort(m_impulses.begin(), m_impulses.end(),
        [](const ImpulseData& a, const ImpulseData& b) {
            return a.triggerTime < b.triggerTime;
        });
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetWindEffect(const Mathf::Vector3& direction, float strength, float turbulence, float frequency)
{
    m_windData.direction = direction;
    m_windData.baseStrength = strength;
    m_windData.turbulence = turbulence;
    m_windData.frequency = frequency;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetOrbitalMotion(const Mathf::Vector3& center, float radius, float speed, const Mathf::Vector3& axis)
{
    m_orbitalData.center = center;
    m_orbitalData.radius = radius;
    m_orbitalData.speed = speed;
    m_orbitalData.axis = axis;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetExplosiveEffect(float initialSpeed, float speedDecay, float randomFactor, float sphereRadius)
{
    m_explosiveData.initialSpeed = initialSpeed;
    m_explosiveData.speedDecay = speedDecay;
    m_explosiveData.randomFactor = randomFactor;
    m_explosiveData.sphereRadius = sphereRadius;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::SetOrbitalCenter(const Mathf::Vector3& center)
{
    m_orbitalData.center = center;
    m_paramsDirty = true;
}

void MeshMovementModuleCS::ClearVelocityCurve()
{
    m_velocityCurve.clear();
    m_paramsDirty = true;
}

void MeshMovementModuleCS::ClearImpulses()
{
    m_impulses.clear();
    m_paramsDirty = true;
}

nlohmann::json MeshMovementModuleCS::SerializeData() const
{
    nlohmann::json json;

    // Movement 파라미터 직렬화
    json["movementParams"] = {
        {"velocityMode", static_cast<int>(m_velocityMode)},
        {"currentTime", m_currentTime}
    };

    // MeshMovementParams 구조체 내용 직렬화
    json["meshParams"] = {
        {"useGravity", m_movementParams.useGravity},
        {"gravityStrength", m_movementParams.gravityStrength},
    };

    // VelocityCurve 데이터 직렬화
    json["velocityCurve"] = nlohmann::json::array();
    for (const auto& point : m_velocityCurve)
    {
        json["velocityCurve"].push_back({
            {"time", point.time},
            {"velocity", {point.velocity.x, point.velocity.y, point.velocity.z}},
            {"strength", point.strength}
            });
    }

    // Impulse 데이터 직렬화
    json["impulses"] = nlohmann::json::array();
    for (const auto& impulse : m_impulses)
    {
        json["impulses"].push_back({
            {"triggerTime", impulse.triggerTime},
            {"direction", {impulse.direction.x, impulse.direction.y, impulse.direction.z}},
            {"force", impulse.force},
            {"duration", impulse.duration},
            {"range", impulse.impulseRange},
            {"type", impulse.impulseType}
            });
    }

    // Wind 데이터 직렬화
    json["windData"] = {
        {"direction", {m_windData.direction.x, m_windData.direction.y, m_windData.direction.z}},
        {"baseStrength", m_windData.baseStrength},
        {"turbulence", m_windData.turbulence},
        {"frequency", m_windData.frequency}
    };

    // Orbital 데이터 직렬화
    json["orbitalData"] = {
        {"center", {m_orbitalData.center.x, m_orbitalData.center.y, m_orbitalData.center.z}},
        {"radius", m_orbitalData.radius},
        {"speed", m_orbitalData.speed},
        {"axis", {m_orbitalData.axis.x, m_orbitalData.axis.y, m_orbitalData.axis.z}}
    };

    // Explosive 데이터 직렬화
    json["explosiveData"] = {
        {"initialSpeed", m_explosiveData.initialSpeed},
        {"speedDecay", m_explosiveData.speedDecay},
        {"randomFactor", m_explosiveData.randomFactor},
        {"sphereRadius", m_explosiveData.sphereRadius}
    };

    // 이징 정보 직렬화
    json["easing"] = {
        {"enabled", m_easingEnable}
    };
    if (m_easingEnable)
    {
        json["easing"]["easingType"] = static_cast<int>(m_easingModule.GetEasingType());
        json["easing"]["animationType"] = static_cast<int>(m_easingModule.GetAnimationType());
        json["easing"]["duration"] = m_easingModule.GetDuration();
    }

    // 상태 정보
    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}

void MeshMovementModuleCS::DeserializeData(const nlohmann::json& json)
{
    // Movement 파라미터 복원
    if (json.contains("movementParams"))
    {
        const auto& movementJson = json["movementParams"];
        if (movementJson.contains("velocityMode"))
            m_velocityMode = static_cast<VelocityMode>(movementJson["velocityMode"]);
        if (movementJson.contains("currentTime"))
            m_currentTime = movementJson["currentTime"];
    }

    // MeshMovementParams 복원
    if (json.contains("meshParams"))
    {
        const auto& meshJson = json["meshParams"];
        if (meshJson.contains("useGravity"))
            m_movementParams.useGravity = meshJson["useGravity"];
        if (meshJson.contains("gravityStrength"))
            m_movementParams.gravityStrength = meshJson["gravityStrength"];
    }

    // VelocityCurve 데이터 복원
    if (json.contains("velocityCurve"))
    {
        m_velocityCurve.clear();
        for (const auto& pointJson : json["velocityCurve"])
        {
            VelocityPoint point;
            point.time = pointJson.value("time", 0.0f);
            auto vel = pointJson["velocity"];
            point.velocity = Mathf::Vector3(vel[0], vel[1], vel[2]);
            point.strength = pointJson.value("strength", 1.0f);
            m_velocityCurve.push_back(point);
        }
    }

    // Impulse 데이터 복원
    if (json.contains("impulses"))
    {
        m_impulses.clear();
        for (const auto& impulseJson : json["impulses"])
        {
            ImpulseData impulse;
            impulse.triggerTime = impulseJson.value("triggerTime", 0.0f);
            auto dir = impulseJson["direction"];
            impulse.direction = Mathf::Vector3(dir[0], dir[1], dir[2]);
            impulse.force = impulseJson.value("force", 1.0f);
            impulse.duration = impulseJson.value("duration", 1.0f);
            impulse.impulseRange = impulseJson.value("range", 1.0f);
            impulse.impulseType = impulseJson.value("type", 0);
            m_impulses.push_back(impulse);
        }
    }

    // Wind, Orbital, Explosive 데이터 복원
    if (json.contains("windData"))
    {
        const auto& windJson = json["windData"];
        auto dir = windJson["direction"];
        m_windData.direction = Mathf::Vector3(dir[0], dir[1], dir[2]);
        m_windData.baseStrength = windJson.value("baseStrength", 1.0f);
        m_windData.turbulence = windJson.value("turbulence", 0.5f);
        m_windData.frequency = windJson.value("frequency", 1.0f);
    }

    if (json.contains("orbitalData"))
    {
        const auto& orbitalJson = json["orbitalData"];
        auto center = orbitalJson["center"];
        m_orbitalData.center = Mathf::Vector3(center[0], center[1], center[2]);
        m_orbitalData.radius = orbitalJson.value("radius", 5.0f);
        m_orbitalData.speed = orbitalJson.value("speed", 1.0f);
        auto axis = orbitalJson["axis"];
        m_orbitalData.axis = Mathf::Vector3(axis[0], axis[1], axis[2]);
    }

    if (json.contains("explosiveData"))
    {
        const auto& explosiveJson = json["explosiveData"];
        m_explosiveData.initialSpeed = explosiveJson.value("initialSpeed", 50.0f);
        m_explosiveData.speedDecay = explosiveJson.value("speedDecay", 2.0f);
        m_explosiveData.randomFactor = explosiveJson.value("randomFactor", 0.4f);
        m_explosiveData.sphereRadius = explosiveJson.value("sphereRadius", 1.0f);
    }

    // 이징 정보 복원
    if (json.contains("easing"))
    {
        const auto& easingJson = json["easing"];
        m_easingEnable = easingJson.value("enabled", false);

        if (m_easingEnable && easingJson.contains("easingType") &&
            easingJson.contains("animationType") && easingJson.contains("duration"))
        {
            EasingEffect easingType = static_cast<EasingEffect>(easingJson["easingType"]);
            StepAnimation animationType = static_cast<StepAnimation>(easingJson["animationType"]);
            float duration = easingJson["duration"];
            SetEasing(easingType, animationType, duration);
        }
    }

    // 상태 정보 복원
    if (json.contains("state"))
    {
        const auto& stateJson = json["state"];
        if (stateJson.contains("particleCapacity"))
            m_particleCapacity = stateJson["particleCapacity"];
    }

    if (!m_isInitialized)
        Initialize();

    m_paramsDirty = true;
}

std::string MeshMovementModuleCS::GetModuleType() const
{
    return "MeshMovementModuleCS";
}