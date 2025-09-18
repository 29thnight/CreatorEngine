#include "ShaderSystem.h"
#include "MovementModuleCS.h"
#include "EffectSerializer.h"

MovementModuleCS::MovementModuleCS()
    : m_computeShader(nullptr), m_movementParamsBuffer(nullptr),
    m_velocityCurveBuffer(nullptr), m_velocityCurveSRV(nullptr),
    m_impulsesBuffer(nullptr), m_impulsesSRV(nullptr),
    m_paramsDirty(true), m_easingEnabled(false),
    m_easingType(0), m_currentTime(0.0f)
{
    // 기본값 설정
    m_velocityMode = VelocityMode::Constant;
    m_gravity = false;
    m_gravityStrength = 9.8f;

    // Wind 기본값
    m_windData.direction = Mathf::Vector3(1, 0, 0);
    m_windData.baseStrength = 1.0f;
    m_windData.turbulence = 0.5f;
    m_windData.frequency = 1.0f;

    // Orbital 기본값
    m_orbitalData.center = Mathf::Vector3(0, 0, 0);
    m_orbitalData.radius = 5.0f;
    m_orbitalData.speed = 1.0f;
    m_orbitalData.axis = Mathf::Vector3(0, 1, 0);

    m_explosiveData.initialSpeed = 50.0f;
    m_explosiveData.speedDecay = 2.0f;
    m_explosiveData.randomFactor = 0.4f;
    m_explosiveData.sphereRadius = 1.0f;
}

void MovementModuleCS::Initialize()
{
    //m_velocityMode = VelocityMode::Constant;
    m_currentTime = 0.0f; // 시간 초기화

    m_computeShader = ShaderSystem->ComputeShaders["MovementModule"].GetShader();
    InitializeCompute();
}

void MovementModuleCS::Update(float delta)
{
    if (!m_enabled) return;

    if (!m_isInitialized) return;

    DirectX11::BeginEvent(L"MovementModuleCS");

    // 시간 업데이트
    m_currentTime += delta;

    // Structured Buffer 업데이트 (매번 체크)
    UpdateStructuredBuffers();

    // 상수 버퍼 업데이트
    UpdateConstantBuffers(delta);

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

void MovementModuleCS::OnSystemResized(UINT max)
{
    if (max != m_particleCapacity)
    {
        m_particleCapacity = max;
        m_paramsDirty = true;
    }
}

void MovementModuleCS::SetEmitterTransform(const Mathf::Vector3& position, const Mathf::Vector3& rotation)
{
}

bool MovementModuleCS::InitializeCompute()
{
    // 상수 버퍼 생성
    D3D11_BUFFER_DESC movementParamsDesc = {};
    movementParamsDesc.ByteWidth = sizeof(MovementParams);
    movementParamsDesc.Usage = D3D11_USAGE_DYNAMIC;
    movementParamsDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    movementParamsDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&movementParamsDesc, nullptr, &m_movementParamsBuffer);
    if (FAILED(hr))
        return false;

    m_isInitialized = true;
    return true;
}

void MovementModuleCS::UpdateConstantBuffers(float delta)
{
    // 항상 상수 버퍼 업데이트 (시간 변수 때문에)
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = DirectX11::DeviceStates->g_pDeviceContext->Map(m_movementParamsBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    if (SUCCEEDED(hr))
    {
        MovementParams* params = reinterpret_cast<MovementParams*>(mappedResource.pData);
        params->deltaTime = delta;
        params->gravityStrength = m_gravityStrength;
        params->useGravity = m_gravity ? 1 : 0;

        // Velocity 관련 파라미터 업데이트
        params->velocityMode = static_cast<int>(m_velocityMode);
        params->currentTime = m_currentTime;

        // Wind 데이터
        params->windDirection = m_windData.direction;
        params->windStrength = m_windData.baseStrength;
        params->turbulence = m_windData.turbulence;
        params->frequency = m_windData.frequency;

        // Orbital 데이터
        params->orbitalCenter = m_orbitalData.center;
        params->orbitalRadius = m_orbitalData.radius;
        params->orbitalSpeed = m_orbitalData.speed;
        params->orbitalAxis = m_orbitalData.axis;

        params->explosiveSpeed = m_explosiveData.initialSpeed;
        params->explosiveDecay = m_explosiveData.speedDecay;
        params->explosiveRandom = m_explosiveData.randomFactor;
        params->explosiveSphere = m_explosiveData.sphereRadius;

        // 배열 크기
        params->velocityCurveSize = static_cast<int>(m_velocityCurve.size());
        params->impulseCount = static_cast<int>(m_impulses.size());

        DirectX11::DeviceStates->g_pDeviceContext->Unmap(m_movementParamsBuffer, 0);
        m_paramsDirty = false;
    }
}

void MovementModuleCS::UpdateStructuredBuffers()
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

void MovementModuleCS::Release()
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

void MovementModuleCS::ResetForReuse()
{
    if (!m_enabled) return;

    // 시간 초기화
    m_currentTime = 0.0f;

    // 더티 플래그 설정 (상수 버퍼 재업데이트 강제)
    m_paramsDirty = true;
}

bool MovementModuleCS::IsReadyForReuse() const
{
    return m_isInitialized &&
        m_movementParamsBuffer != nullptr;
}

void MovementModuleCS::SetVelocityMode(VelocityMode mode)
{
    m_velocityMode = mode;
    m_paramsDirty = true;
}

void MovementModuleCS::SetVelocityCurve(const std::vector<VelocityPoint>& curve)
{
    m_velocityCurve = curve;
    m_paramsDirty = true;
    // UpdateStructuredBuffers는 Update()에서 호출됨
}

void MovementModuleCS::AddVelocityPoint(float time, const Mathf::Vector3& velocity, float strength)
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
    // UpdateStructuredBuffers는 Update()에서 호출됨
}

void MovementModuleCS::AddImpulse(float triggerTime, const Mathf::Vector3& direction, float force, float duration)
{
    ImpulseData impulse;
    impulse.triggerTime = triggerTime;
    impulse.direction = direction;
    impulse.force = force;
    impulse.duration = duration;

    m_impulses.push_back(impulse);
    std::sort(m_impulses.begin(), m_impulses.end(),
        [](const ImpulseData& a, const ImpulseData& b) {
            return a.triggerTime < b.triggerTime;
        });
    m_paramsDirty = true;
    // UpdateStructuredBuffers는 Update()에서 호출됨
}

void MovementModuleCS::SetWindEffect(const Mathf::Vector3& direction, float strength, float turbulence, float frequency)
{
    m_windData.direction = direction;
    m_windData.baseStrength = strength;
    m_windData.turbulence = turbulence;
    m_windData.frequency = frequency;
    m_paramsDirty = true;
}

void MovementModuleCS::SetOrbitalMotion(const Mathf::Vector3& center, float radius, float speed, const Mathf::Vector3& axis)
{
    m_orbitalData.center = center;
    m_orbitalData.radius = radius;
    m_orbitalData.speed = speed;
    m_orbitalData.axis = axis;
    m_paramsDirty = true;
}

void MovementModuleCS::SetExplosiveEffect(float initialSpeed, float speedDecay,
    float randomFactor, float sphereRadius)
{
    m_explosiveData.initialSpeed = initialSpeed;
    m_explosiveData.speedDecay = speedDecay;
    m_explosiveData.randomFactor = randomFactor;
    m_explosiveData.sphereRadius = sphereRadius;
    m_paramsDirty = true;
}

void MovementModuleCS::ClearVelocityCurve()
{
    m_velocityCurve.clear();
    m_paramsDirty = true;
    // UpdateStructuredBuffers는 Update()에서 호출됨
}

void MovementModuleCS::ClearImpulses()
{
    m_impulses.clear();
    m_paramsDirty = true;
    // UpdateStructuredBuffers는 Update()에서 호출됨
}

nlohmann::json MovementModuleCS::SerializeData() const
{
    nlohmann::json json;

    // Movement 파라미터 직렬화
    json["movementParams"] = {
        {"useGravity", m_gravity},
        {"gravityStrength", m_gravityStrength},
        {"easingEnabled", m_easingEnabled},
        {"easingType", m_easingType},
        {"velocityMode", static_cast<int>(m_velocityMode)},
        {"currentTime", m_currentTime}
    };

    // VelocityCurve 데이터 직렬화
    json["velocityCurve"] = nlohmann::json::array();
    for (const auto& point : m_velocityCurve)
    {
        json["velocityCurve"].push_back({
            {"time", point.time},
            {"velocity", EffectSerializer::SerializeVector3(point.velocity)},
            {"strength", point.strength}
            });
    }

    // Impulse 데이터 직렬화
    json["impulses"] = nlohmann::json::array();
    for (const auto& impulse : m_impulses)
    {
        json["impulses"].push_back({
            {"triggerTime", impulse.triggerTime},
            {"direction", EffectSerializer::SerializeVector3(impulse.direction)},
            {"force", impulse.force},
            {"duration", impulse.duration}
            });
    }

    // Wind 데이터 직렬화
    json["windData"] = {
        {"direction", EffectSerializer::SerializeVector3(m_windData.direction)},
        {"baseStrength", m_windData.baseStrength},
        {"turbulence", m_windData.turbulence},
        {"frequency", m_windData.frequency}
    };

    // Orbital 데이터 직렬화
    json["orbitalData"] = {
        {"center", EffectSerializer::SerializeVector3(m_orbitalData.center)},
        {"radius", m_orbitalData.radius},
        {"speed", m_orbitalData.speed},
        {"axis", EffectSerializer::SerializeVector3(m_orbitalData.axis)}
    };

    json["explosiveData"] = {
        {"initialSpeed", m_explosiveData.initialSpeed},
        {"speedDecay", m_explosiveData.speedDecay},
        {"randomFactor", m_explosiveData.randomFactor},
        {"sphereRadius", m_explosiveData.sphereRadius}
    };

    // 상태 정보
    json["state"] = {
        {"isInitialized", m_isInitialized},
        {"particleCapacity", m_particleCapacity}
    };

    return json;
}

void MovementModuleCS::DeserializeData(const nlohmann::json& json)
{
    // Movement 파라미터 복원 (기존과 동일)
    if (json.contains("movementParams"))
    {
        const auto& movementJson = json["movementParams"];

        if (movementJson.contains("useGravity"))
            m_gravity = movementJson["useGravity"];
        if (movementJson.contains("gravityStrength"))
            m_gravityStrength = movementJson["gravityStrength"];
        if (movementJson.contains("easingEnabled"))
            m_easingEnabled = movementJson["easingEnabled"];
        if (movementJson.contains("easingType"))
            m_easingType = movementJson["easingType"];
        if (movementJson.contains("velocityMode"))
        {
            SetVelocityMode(static_cast<VelocityMode>(movementJson["velocityMode"]));
            m_velocityMode = static_cast<VelocityMode>(movementJson["velocityMode"]);
        }
            
        if (movementJson.contains("currentTime"))
            m_currentTime = movementJson["currentTime"];
    }

    // VelocityCurve 데이터 복원
    if (json.contains("velocityCurve"))
    {
        ClearVelocityCurve();
        for (const auto& pointJson : json["velocityCurve"])
        {
            float time = 0.0f;
            Mathf::Vector3 velocity;
            float strength = 1.0f;

            if (pointJson.contains("time"))
                time = pointJson["time"];
            if (pointJson.contains("velocity"))
                velocity = EffectSerializer::DeserializeVector3(pointJson["velocity"]);
            if (pointJson.contains("strength"))
                strength = pointJson["strength"];

            AddVelocityPoint(time, velocity, strength);
        }
    }

    // Impulse 데이터 복원
    if (json.contains("impulses"))
    {
        ClearImpulses();
        for (const auto& impulseJson : json["impulses"])
        {
            float triggerTime = 0.0f;
            Mathf::Vector3 direction;
            float force = 1.0f;
            float duration = 1.0f;

            if (impulseJson.contains("triggerTime"))
                triggerTime = impulseJson["triggerTime"];
            if (impulseJson.contains("direction"))
                direction = EffectSerializer::DeserializeVector3(impulseJson["direction"]);
            if (impulseJson.contains("force"))
                force = impulseJson["force"];
            if (impulseJson.contains("duration"))
                duration = impulseJson["duration"];

            AddImpulse(triggerTime, direction, force, duration);
        }
    }

    // Wind 데이터 복원
    if (json.contains("windData"))
    {
        const auto& windJson = json["windData"];
        Mathf::Vector3 direction = Mathf::Vector3(1, 0, 0);
        float strength = 1.0f;
        float turbulence = 0.5f;
        float frequency = 1.0f;

        if (windJson.contains("direction"))
            direction = EffectSerializer::DeserializeVector3(windJson["direction"]);
        if (windJson.contains("baseStrength"))
            strength = windJson["baseStrength"];
        if (windJson.contains("turbulence"))
            turbulence = windJson["turbulence"];
        if (windJson.contains("frequency"))
            frequency = windJson["frequency"];

        SetWindEffect(direction, strength, turbulence, frequency);
    }

    // Orbital 데이터 복원
    if (json.contains("orbitalData"))
    {
        const auto& orbitalJson = json["orbitalData"];
        Mathf::Vector3 center = Mathf::Vector3(0, 0, 0);
        float radius = 5.0f;
        float speed = 1.0f;
        Mathf::Vector3 axis = Mathf::Vector3(0, 1, 0);

        if (orbitalJson.contains("center"))
            center = EffectSerializer::DeserializeVector3(orbitalJson["center"]);
        if (orbitalJson.contains("radius"))
            radius = orbitalJson["radius"];
        if (orbitalJson.contains("speed"))
            speed = orbitalJson["speed"];
        if (orbitalJson.contains("axis"))
            axis = EffectSerializer::DeserializeVector3(orbitalJson["axis"]);

        SetOrbitalMotion(center, radius, speed, axis);
    }

    // Explosive 데이터 복원
    if (json.contains("explosiveData"))
    {
        const auto& explosiveJson = json["explosiveData"];
        float initialSpeed = 50.0f;
        float speedDecay = 2.0f;
        float randomFactor = 0.4f;
        float sphereRadius = 1.0f;

        if (explosiveJson.contains("initialSpeed"))
            initialSpeed = explosiveJson["initialSpeed"];
        if (explosiveJson.contains("speedDecay"))
            speedDecay = explosiveJson["speedDecay"];
        if (explosiveJson.contains("randomFactor"))
            randomFactor = explosiveJson["randomFactor"];
        if (explosiveJson.contains("sphereRadius"))
            sphereRadius = explosiveJson["sphereRadius"];

        SetExplosiveEffect(initialSpeed, speedDecay, randomFactor, sphereRadius);
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
}

std::string MovementModuleCS::GetModuleType() const
{
    return "MovementModuleCS";
}