#include "MeshModuleGPU.h"
#include "ShaderSystem.h"
#include "DataSystem.h"

MeshModuleGPU::MeshModuleGPU()
{
    // 모든 포인터와 값들을 안전하게 초기화
    m_pso = nullptr;
    m_meshType = MeshType::None;
    m_instanceCount = 0;
    m_particleSRV = nullptr;
    m_model = nullptr;
    m_meshIndex = 0;
    m_assignedTexture = nullptr;
    m_tempCubeMesh = nullptr;
    m_clippingBuffer = nullptr;
    m_isClippingAnimating = false;
    m_clippingAnimationSpeed = 1.0f;

    m_worldMatrix = Mathf::Matrix::Identity;
    m_invWorldMatrix = Mathf::Matrix::Identity;

    // 상수 버퍼 데이터도 초기화
    memset(&m_constantBufferData, 0, sizeof(MeshConstantBuffer));
}

void MeshModuleGPU::Initialize()
{
    m_pso = std::make_unique<PipelineStateObject>();
    m_instanceCount = 0;
    m_particleSRV = nullptr;

    // 기본 셰이더 설정
    if (m_vertexShaderName == "None") {
        m_vertexShaderName = "MeshParticle";
    }
    if (m_pixelShaderName == "None") {
        m_pixelShaderName = "MeshParticle";
    }

    // 렌더 상태 프리셋 설정
    if (m_blendPreset == BlendPreset::None) { // 부모 기본값
        m_blendPreset = BlendPreset::Additive;
    }
    if (m_depthPreset == DepthPreset::None) { // 부모 기본값  
        m_depthPreset = DepthPreset::ReadOnly;
    }
    if (m_rasterizerPreset == RasterizerPreset::None) { // 부모 기본값
        m_rasterizerPreset = RasterizerPreset::NoCull;
    }

    // 프리미티브 토폴로지 설정
    m_pso->m_primitiveTopology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // 부모의 함수들로 모든 상태 업데이트
    UpdatePSOShaders();
    UpdatePSORenderStates();

    // 샘플러 설정
    auto linearSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    auto clampSampler = std::make_shared<Sampler>(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_CLAMP);

    m_pso->m_samplers.push_back(linearSampler);
    m_pso->m_samplers.push_back(clampSampler);

    // 상수 버퍼 생성
    m_constantBuffer = DirectX11::CreateBuffer(
        sizeof(MeshConstantBuffer),
        D3D11_BIND_CONSTANT_BUFFER,
        &m_constantBufferData
    );

    CreateClippingBuffer();

    // 기본 큐브 메시 설정
    if (m_meshType == MeshType::None) {
        SetMeshType(MeshType::Cube);
    }

    if (IsPolarClippingEnabled()) {
        OnClippingStateChanged();
    }
}

void MeshModuleGPU::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
    if (!m_enabled) return;

    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;

    // 렌더링 전 UAV 해제 (해저드 방지)
    ID3D11UnorderedAccessView* nullUAVs[] = { nullptr };
    deviceContext->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

    auto currentMesh = GetCurrentMesh();
    if (!currentMesh || !m_particleSRV || m_instanceCount == 0)
        return;

    m_worldMatrix = world;
    m_invWorldMatrix = world.Invert();

    // Polar 클리핑 애니메이션 처리
    if (m_isPolarClippingAnimating && IsPolarClippingEnabled()) {
        float progress = 0.f;
        if (m_useEffectProgress) {
            progress = m_effectProgress;
        }
        else {
            float currentTime = Time->GetTotalSeconds();
            progress = (sin(currentTime * m_polarClippingAnimationSpeed) + 1.0f) * 0.5f;
        }
        SetPolarAngleProgress(progress);
        UpdateClippingBuffer();
    }

    // 상수 버퍼 업데이트
    UpdateConstantBuffer(world, view, projection);
    deviceContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // Polar 클리핑 상수 버퍼 바인딩
    if (IsPolarClippingEnabled() && m_polarClippingBuffer) {
        UpdateClippingBuffer();
        deviceContext->PSSetConstantBuffers(2, 1, m_polarClippingBuffer.GetAddressOf());
    }

    if (m_timeBuffer) {
        m_timeParams.time = Time->GetTotalSeconds();

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = deviceContext->Map(m_timeBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            TimeParams* params = static_cast<TimeParams*>(mappedResource.pData);
            *params = m_timeParams;
            deviceContext->Unmap(m_timeBuffer.Get(), 0);
        }

        deviceContext->PSSetConstantBuffers(3, 1, m_timeBuffer.GetAddressOf());
    }

    // 파티클 SRV 바인딩
    deviceContext->VSSetShaderResources(0, 1, &m_particleSRV);

    // 다중 텍스처 바인딩 사용
    if (GetTextureCount() > 0) {
        BindTextures();
    }
    else if (m_meshType == MeshType::Model && m_model) {
        // 모델의 기본 텍스처 사용 (fallback)
        auto material = m_model->GetMaterial(m_meshIndex);
        if (material && material->m_pBaseColor && material->m_pBaseColor->m_pSRV) {
            deviceContext->PSSetShaderResources(0, 1, &material->m_pBaseColor->m_pSRV);
        }
    }

    // 메시의 버텍스/인덱스 버퍼 바인딩
    UINT stride = currentMesh->GetStride();
    UINT offset = 0;
    auto vertexBuffer = currentMesh->GetVertexBuffer();
    auto indexBuffer = currentMesh->GetIndexBuffer();

    deviceContext->IASetVertexBuffers(0, 1, vertexBuffer.GetAddressOf(), &stride, &offset);
    deviceContext->IASetIndexBuffer(indexBuffer.Get(), DXGI_FORMAT_R32_UINT, 0);

    // 인스턴싱 렌더링
    const auto& indices = currentMesh->GetIndices();
    deviceContext->DrawIndexedInstanced(indices.size(), m_instanceCount, 0, 0, 0);

    if (IsPolarClippingEnabled()) {
        ID3D11Buffer* nullBuffer[1] = { nullptr };
        deviceContext->PSSetConstantBuffers(2, 1, nullBuffer);
    }

    if (GetTextureCount() > 0) {
        std::vector<ID3D11ShaderResourceView*> nullSRVs(GetTextureCount(), nullptr);
        deviceContext->PSSetShaderResources(0, nullSRVs.size(), nullSRVs.data());
    }
}

void MeshModuleGPU::Release()
{
    if (m_tempCubeMesh) {
        delete m_tempCubeMesh;
        m_tempCubeMesh = nullptr;
    }

    m_constantBuffer.Reset();
    m_polarClippingBuffer.Reset();
    m_model = nullptr;
    m_meshIndex = 0;
    m_particleSRV = nullptr;
    m_instanceCount = 0;

    // 다중 텍스처 정리
    ClearTextures();
}

void MeshModuleGPU::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
    m_particleSRV = particleSRV;
    m_instanceCount = instanceCount;
}

void MeshModuleGPU::SetupRenderTarget(RenderPassData* renderData)
{
    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;
    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    deviceContext->OMSetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);
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

std::pair<Mathf::Vector3, Mathf::Vector3> MeshModuleGPU::GetCurrentMeshBounds() const
{
    auto currentMesh = GetCurrentMesh();
    if (!currentMesh) {
        // 메쉬가 없으면 기본 큐브 크기 반환
        return { Mathf::Vector3(-0.5f, -0.5f, -0.5f), Mathf::Vector3(0.5f, 0.5f, 0.5f) };
    }

    // 메쉬의 바운딩 박스 가져오기
    BoundingBox boundingBox = currentMesh->GetBoundingBox();

    // Center와 Extents에서 min, max 계산
    // min = center - extents, max = center + extents
    Mathf::Vector3 center(boundingBox.Center.x, boundingBox.Center.y, boundingBox.Center.z);
    Mathf::Vector3 extents(boundingBox.Extents.x, boundingBox.Extents.y, boundingBox.Extents.z);

    Mathf::Vector3 minBounds = center - extents;
    Mathf::Vector3 maxBounds = center + extents;

    return { minBounds, maxBounds };
}

void MeshModuleGPU::ResetForReuse()
{
    if (!m_enabled) return;

    std::lock_guard<std::mutex> lock(m_resetMutex);

    m_instanceCount = 0;
    m_isRendering = false;
    m_gpuWorkPending = false;

    m_particleSRV = nullptr;

    // 다중 텍스처 초기화
    ClearTextures();

    m_meshType = MeshType::Cube;
    m_model = nullptr;
    m_meshIndex = 0;

    // 렌더 상태를 기본값으로 리셋
    //if (m_vertexShaderName == "None") {
    //    m_vertexShaderName = "MeshParticle";
    //}
    //
    //if (m_pixelShaderName == "None") {
    //    m_pixelShaderName = "MeshParticle";
    //}
    //
    //if (m_blendPreset == BlendPreset::Alpha) { // 부모 기본값
    //    m_blendPreset = BlendPreset::Additive;
    //}
    //if (m_depthPreset == DepthPreset::Default) { // 부모 기본값  
    //    m_depthPreset = DepthPreset::ReadOnly;
    //}
    //if (m_rasterizerPreset == RasterizerPreset::Default) { // 부모 기본값
    //    m_rasterizerPreset = RasterizerPreset::NoCull;
    //}

    UpdatePSOShaders();
    UpdatePSORenderStates();

    if (m_constantBuffer) {
        m_constantBufferData = {};
        m_constantBufferData.world = Mathf::Matrix::Identity;
        m_constantBufferData.view = Mathf::Matrix::Identity;
        m_constantBufferData.projection = Mathf::Matrix::Identity;
    }

    m_polarClippingParams = {};
    m_isPolarClippingAnimating = false;
    m_polarClippingAnimationSpeed = 1.0f;
    m_effectProgress = 0.0f;
    m_useEffectProgress = false;

    m_worldMatrix = Mathf::Matrix::Identity;
    m_invWorldMatrix = Mathf::Matrix::Identity;
}

bool MeshModuleGPU::IsReadyForReuse() const
{
    // GPU 작업이 완료되었고, 렌더링 중이 아닐 때만 재사용 가능
    bool ready = !m_isRendering &&
        !m_gpuWorkPending.load()
        && m_instanceCount == 0;

    // 필수 리소스들이 유효한지 확인
    bool resourcesValid = m_constantBuffer != nullptr &&
        m_pso != nullptr;

    return ready && resourcesValid;
}

void MeshModuleGPU::WaitForGPUCompletion()
{
    m_gpuWorkPending = false;
}

void MeshModuleGPU::UpdatePSOShaders()
{
    RenderModules::UpdatePSOShaders();

    if (m_pso && m_pso->m_vertexShader) {
        if (m_pso->m_inputLayout) {
            m_pso->m_inputLayout = nullptr;
        }

        D3D11_INPUT_ELEMENT_DESC vertexLayoutDesc[] = {
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

void MeshModuleGPU::CreateCubeMesh()
{
    auto vertices = PrimitiveCreator::CubeVertices();
    auto indices = PrimitiveCreator::CubeIndices();

    // 임시 큐브 메시 생성 (Model 시스템과 분리)
    if (m_tempCubeMesh)
    {
        delete m_tempCubeMesh;
    }

    m_tempCubeMesh = new Mesh("CubeParticle", vertices, indices);
}

void MeshModuleGPU::CreateSphereMesh()
{
    // 향후 구현 예정
}

void MeshModuleGPU::SetMeshType(MeshType type)
{
    if (m_meshType == type) return;

    m_meshType = type;

    switch (type)
    {
    case MeshType::Cube:
        CreateCubeMesh();
        m_model = nullptr;
        break;
    case MeshType::Sphere:
        CreateSphereMesh();
        m_model = nullptr;
        break;
    case MeshType::Model:
        // SetModel에서 설정됨
        break;
    }
}

void MeshModuleGPU::SetModel(Model* model, int meshIndex)
{
    if (!model) return;

    if (meshIndex < 0 || meshIndex >= model->m_numTotalMeshes)
    {
        meshIndex = 0; // 기본값으로 첫 번째 메시 사용
    }

    m_meshType = MeshType::Model;
    m_model = model;
    m_meshIndex = meshIndex;
}

void MeshModuleGPU::SetModel(Model* model, std::string_view meshName)
{
    if (!model) return;

    m_meshType = MeshType::Model;
    m_model = model;

    // 메시 이름으로 인덱스 찾기
    for (int i = 0; i < model->m_numTotalMeshes; ++i)
    {
        auto mesh = model->GetMesh(i);
        if (mesh && mesh->GetName() == meshName)
        {
            m_meshIndex = i;
            return;
        }
    }

    // 찾지 못한 경우 첫 번째 메시 사용
    m_meshIndex = 0;
}

Mesh* MeshModuleGPU::GetCurrentMesh() const
{
    switch (m_meshType)
    {
    case MeshType::Cube:
        return m_tempCubeMesh;
    case MeshType::Sphere:
        return nullptr; // 미구현
    case MeshType::Model:
        return m_model ? m_model->GetMesh(m_meshIndex) : nullptr;
    default:
        return nullptr;
    }
}

void MeshModuleGPU::OnClippingStateChanged()
{
    if (!m_pso || !ShaderSystem)
        return;

    if (IsPolarClippingEnabled()) {
        auto it = ShaderSystem->PixelShaders.find("MeshParticleTest");
        if (it != ShaderSystem->PixelShaders.end()) {
            m_pso->m_pixelShader = &it->second;
        }
    }
    else {
        auto it = ShaderSystem->PixelShaders.find("MeshParticleTest");
        if (it != ShaderSystem->PixelShaders.end()) {
            m_pso->m_pixelShader = &it->second;
        }
    }
}

void MeshModuleGPU::SetClippingAnimation(bool enable, float speed)
{
    m_isClippingAnimating = enable;
    m_clippingAnimationSpeed = speed;
}

void MeshModuleGPU::CreateClippingBuffer()
{
    if (m_polarClippingBuffer)
        return;

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(PolarClippingParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_polarClippingBuffer);
    if (FAILED(hr))
    {
        m_polarClippingBuffer.Reset();
    }

    bufferDesc.ByteWidth = sizeof(TimeParams);
    hr = DirectX11::DeviceStates->g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_timeBuffer);
    if (FAILED(hr))
    {
        m_timeBuffer.Reset();
    }
}

void MeshModuleGPU::UpdateClippingBuffer()
{
    if (!m_polarClippingBuffer)
        return;
    if (!DirectX11::DeviceStates->g_pDevice || !DirectX11::DeviceStates->g_pDeviceContext)
        return;

    auto& deviceContext = DirectX11::DeviceStates->g_pDeviceContext;
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = deviceContext->Map(m_polarClippingBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

    if (SUCCEEDED(hr))
    {
        PolarClippingParams* params = static_cast<PolarClippingParams*>(mappedResource.pData);
        *params = m_polarClippingParams;
        deviceContext->Unmap(m_polarClippingBuffer.Get(), 0);
    }
}

void MeshModuleGPU::EnablePolarClipping(bool enable)
{
    m_polarClippingParams.polarClippingEnabled = enable ? 1.0f : 0.0f;
    OnClippingStateChanged();
}

bool MeshModuleGPU::IsPolarClippingEnabled() const
{
    return m_polarClippingParams.polarClippingEnabled > 0.5f;
}

void MeshModuleGPU::SetPolarAngleProgress(float progress)
{
    m_polarClippingParams.polarAngleProgress = std::clamp(progress, 0.0f, 1.0f);
    if (IsPolarClippingEnabled())
    {
        UpdateClippingBuffer();
    }
}

void MeshModuleGPU::SetPolarCenter(const Mathf::Vector3& center)
{
    m_polarClippingParams.polarCenter = center;
    if (IsPolarClippingEnabled())
    {
        UpdateClippingBuffer();
    }
}

void MeshModuleGPU::SetPolarUpAxis(const Mathf::Vector3& upAxis)
{
    Mathf::Vector3 normalization = upAxis;
    normalization.Normalize();
    m_polarClippingParams.polarUpAxis = normalization;
}

void MeshModuleGPU::SetPolarStartAngle(float angleRadians)
{
    m_polarClippingParams.polarStartAngle = angleRadians;
    if (IsPolarClippingEnabled())
    {
        UpdateClippingBuffer();
    }
}

void MeshModuleGPU::SetPolarDirection(float direction)
{
    m_polarClippingParams.polarDirection = (direction >= 0.0f) ? 1.0f : -1.0f;
    if (IsPolarClippingEnabled())
    {
        UpdateClippingBuffer();
    }
}

void MeshModuleGPU::SetPolarClippingAnimation(bool enable, float speed)
{
    m_isPolarClippingAnimating = enable;
    m_polarClippingAnimationSpeed = speed;
}

void MeshModuleGPU::SetPolarReferenceDirection(const Mathf::Vector3& referenceDir)
{
    Mathf::Vector3 normalized = referenceDir;
    normalized.Normalize();
    m_polarClippingParams.polarReferenceDir = normalized;
    if (IsPolarClippingEnabled())
    {
        UpdateClippingBuffer();
    }
}

nlohmann::json MeshModuleGPU::SerializeData() const
{
    nlohmann::json json;

    // 부모의 렌더 상태 직렬화 사용
    json.merge_patch(SerializeRenderStates());

    // 다중 텍스처 직렬화
    json["textures"] = SerializeTextures();

    json["mesh"] = {
        {"type", static_cast<int>(m_meshType)},
        {"meshIndex", m_meshIndex}
    };

    json["model"] = {
        {"hasModel", m_model != nullptr}
    };

    if (m_model) {
        json["model"]["name"] = m_model->name;
        json["model"]["assigned"] = true;
        json["model"]["meshIndex"] = m_meshIndex;
    }

    if (IsPolarClippingEnabled()) {
        json["polarClipping"] = {
            {"enabled", IsPolarClippingEnabled()},
            {"animating", m_isPolarClippingAnimating},
            {"animationSpeed", m_polarClippingAnimationSpeed},
            {"params", {
                {"polarAngleProgress", m_polarClippingParams.polarAngleProgress},
                {"polarStartAngle", m_polarClippingParams.polarStartAngle},
                {"polarDirection", m_polarClippingParams.polarDirection},
                {"polarCenter", {
                    {"x", m_polarClippingParams.polarCenter.x},
                    {"y", m_polarClippingParams.polarCenter.y},
                    {"z", m_polarClippingParams.polarCenter.z}
                }},
                {"polarUpAxis", {
                    {"x", m_polarClippingParams.polarUpAxis.x},
                    {"y", m_polarClippingParams.polarUpAxis.y},
                    {"z", m_polarClippingParams.polarUpAxis.z}
                }},
                {"polarReferenceDir", {
                    {"x", m_polarClippingParams.polarReferenceDir.x},
                    {"y", m_polarClippingParams.polarReferenceDir.y},
                    {"z", m_polarClippingParams.polarReferenceDir.z}
                }}
            }}
        };
    }

    json["constantBuffer"] = {
        {"cameraPosition", {
            {"x", m_constantBufferData.cameraPosition.x},
            {"y", m_constantBufferData.cameraPosition.y},
            {"z", m_constantBufferData.cameraPosition.z}
        }}
    };

    json["renderState"] = {
        {"instanceCount", m_instanceCount},
    };

    return json;
}

void MeshModuleGPU::DeserializeData(const nlohmann::json& json)
{
    if (!m_pso) {
        Initialize();
    }

    // 부모의 렌더 상태 역직렬화 사용
    DeserializeRenderStates(json);

    // 다중 텍스처 역직렬화
    DeserializeTextures(json);

    // 모델 정보 복원
    if (json.contains("model")) {
        const auto& modelJson = json["model"];
        if (modelJson.contains("name")) {
            std::string modelName = modelJson["name"];

            if (modelName.find('.') == std::string::npos) {
                std::vector<std::string> extensions = { ".gltf", ".fbx" };

                for (const auto& ext : extensions) {
                    std::string testName = modelName + ext;
                    m_model = DataSystems->LoadCashedModel(testName);
                    if (m_model) break;
                }
            }

            m_model = DataSystems->LoadCashedModel(modelName);

            if (m_model) {
                m_meshType = MeshType::Model;
            }
        }
        if (modelJson.contains("meshIndex")) {
            m_meshIndex = modelJson["meshIndex"];
        }
    }

    // 메시 타입 정보 복원
    if (json.contains("mesh")) {
        const auto& meshJson = json["mesh"];
        if (meshJson.contains("type")) {
            MeshType jsonType = static_cast<MeshType>(meshJson["type"]);
            if (!m_model || jsonType != MeshType::Model) {
                SetMeshType(jsonType);
            }
        }
        if (meshJson.contains("meshIndex")) {
            m_meshIndex = meshJson["meshIndex"];
        }
    }

    if (json.contains("polarClipping")) {
        const auto& polarClippingJson = json["polarClipping"];

        if (polarClippingJson.contains("enabled")) {
            bool enabled = polarClippingJson["enabled"];
            EnablePolarClipping(enabled);
        }

        if (polarClippingJson.contains("animating")) {
            m_isPolarClippingAnimating = polarClippingJson["animating"];
        }

        if (polarClippingJson.contains("animationSpeed")) {
            m_polarClippingAnimationSpeed = polarClippingJson["animationSpeed"];
        }

        if (polarClippingJson.contains("params")) {
            const auto& paramsJson = polarClippingJson["params"];

            if (paramsJson.contains("polarAngleProgress")) {
                SetPolarAngleProgress(paramsJson["polarAngleProgress"]);
            }

            if (paramsJson.contains("polarStartAngle")) {
                SetPolarStartAngle(paramsJson["polarStartAngle"]);
            }

            if (paramsJson.contains("polarDirection")) {
                SetPolarDirection(paramsJson["polarDirection"]);
            }

            if (paramsJson.contains("polarCenter")) {
                const auto& centerJson = paramsJson["polarCenter"];
                Mathf::Vector3 center(
                    centerJson.value("x", 0.0f),
                    centerJson.value("y", 0.0f),
                    centerJson.value("z", 0.0f)
                );
                SetPolarCenter(center);
            }

            if (paramsJson.contains("polarUpAxis")) {
                const auto& upAxisJson = paramsJson["polarUpAxis"];
                Mathf::Vector3 upAxis(
                    upAxisJson.value("x", 0.0f),
                    upAxisJson.value("y", 1.0f),
                    upAxisJson.value("z", 0.0f)
                );
                SetPolarUpAxis(upAxis);
            }

            if (paramsJson.contains("polarReferenceDir")) {
                const auto& refDirJson = paramsJson["polarReferenceDir"];
                Mathf::Vector3 referenceDir(
                    refDirJson.value("x", 1.0f),
                    refDirJson.value("y", 0.0f),
                    refDirJson.value("z", 0.0f)
                );
                SetPolarReferenceDirection(referenceDir);
            }
        }
    }

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
    }

    if (json.contains("renderState")) {
        const auto& renderJson = json["renderState"];

        if (renderJson.contains("instanceCount")) {
            m_instanceCount = renderJson["instanceCount"];
        }
    }

    if (IsPolarClippingEnabled()) {
        if (DirectX11::DeviceStates->g_pDevice && DirectX11::DeviceStates->g_pDeviceContext) {
            UpdateClippingBuffer();
            OnClippingStateChanged();
        }
    }
}

std::string MeshModuleGPU::GetModuleType() const
{
    return "MeshModuleGPU";
}