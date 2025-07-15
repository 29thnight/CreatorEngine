#include "MeshModuleGPU.h"
#include "ShaderSystem.h"

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

    // 클리핑 버퍼 생성
    CreateClippingBuffer();

    // 기본 큐브 메시 설정
    if (m_meshType == MeshType::None) {
        SetMeshType(MeshType::Cube);
    }

    if (IsClippingEnabled()) {
        OnClippingStateChanged();
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

void MeshModuleGPU::SetModel(Model* model, const std::string_view& meshName)
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

    if (IsClippingEnabled()) {
        auto it = ShaderSystem->PixelShaders.find("MeshParticleClipping");
        if (it != ShaderSystem->PixelShaders.end()) {
            m_pso->m_pixelShader = &it->second;
        }
    }
    else {
        auto it = ShaderSystem->PixelShaders.find("MeshParticle");
        if (it != ShaderSystem->PixelShaders.end()) {
            m_pso->m_pixelShader = &it->second;
        }
    }
}

void MeshModuleGPU::CreateClippingBuffer()
{
    if (m_clippingBuffer)
        return;

    D3D11_BUFFER_DESC bufferDesc = {};
    bufferDesc.ByteWidth = sizeof(ClippingParams);
    bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
    bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &m_clippingBuffer);
    if (FAILED(hr))
    {
        m_clippingBuffer.Reset();
    }
}

void MeshModuleGPU::UpdateClippingBuffer()
{
    if (!SupportsClipping() || !m_clippingBuffer)
        return;
    if (!DeviceState::g_pDevice || !DeviceState::g_pDeviceContext)
        return;

    auto& deviceContext = DeviceState::g_pDeviceContext;
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    HRESULT hr = deviceContext->Map(m_clippingBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);

    if (SUCCEEDED(hr))
    {
        ClippingParams* params = static_cast<ClippingParams*>(mappedResource.pData);

        // 기존 파라미터
        const auto& baseParams = GetClippingParams();
        params->clippingProgress = baseParams.clippingProgress;
        params->clippingAxis = baseParams.clippingAxis;
        params->clippingEnabled = baseParams.clippingEnabled;

        // 현재 메쉬의 바운딩 박스 설정
        auto bounds = GetCurrentMeshBounds();
        params->meshBoundingMin = bounds.first;
        params->meshBoundingMax = bounds.second;

        deviceContext->Unmap(m_clippingBuffer.Get(), 0);
    }
}

void MeshModuleGPU::SetClippingAnimation(bool enable, float speed)
{
    m_isClippingAnimating = enable;
    m_clippingAnimationSpeed = speed;
}

nlohmann::json MeshModuleGPU::SerializeData() const
{
    nlohmann::json json;

    // 메시 타입 정보
    json["mesh"] = {
        {"type", static_cast<int>(m_meshType)},
        {"meshIndex", m_meshIndex}
    };

    // 모델 정보
    json["model"] = {
        {"hasModel", m_model != nullptr}
    };

    if (m_model)
    {
        json["model"]["name"] = m_model->name;
        json["model"]["assigned"] = true;
        json["model"]["meshIndex"] = m_meshIndex;
    }

    // 텍스처 정보
    json["texture"] = {
        {"hasTexture", m_assignedTexture != nullptr}
    };

    if (m_assignedTexture)
    {
        json["texture"]["name"] = m_assignedTexture->m_name;
        json["texture"]["assigned"] = true;
    }

    // 클리핑 관련 설정
    if (SupportsClipping())
    {
        json["clipping"] = {
            {"enabled", IsClippingEnabled()},
            {"animating", m_isClippingAnimating},
            {"animationSpeed", m_clippingAnimationSpeed}
        };

        // 클리핑 파라미터 (상대 좌표계만 저장)
        const auto& clippingParams = GetClippingParams();
        json["clipping"]["params"] = {
            {"clippingProgress", clippingParams.clippingProgress},
            {"clippingAxis", {
                {"x", clippingParams.clippingAxis.x},
                {"y", clippingParams.clippingAxis.y},
                {"z", clippingParams.clippingAxis.z}
            }},
            {"clippingEnabled", clippingParams.clippingEnabled}
        };
    }

    // 상수 버퍼 데이터
    json["constantBuffer"] = {
        {"cameraPosition", {
            {"x", m_constantBufferData.cameraPosition.x},
            {"y", m_constantBufferData.cameraPosition.y},
            {"z", m_constantBufferData.cameraPosition.z}
        }}
    };

    // 렌더링 상태
    json["renderState"] = {
        {"instanceCount", m_instanceCount}
    };

    return json;
}

void MeshModuleGPU::DeserializeData(const nlohmann::json& json)
{
    // 모델 정보 복원
    if (json.contains("model"))
    {
        const auto& modelJson = json["model"];
        if (modelJson.contains("name"))
        {
            std::string modelName = modelJson["name"];
            if (modelName.find('.') == std::string::npos)
            {
                modelName += ".fbx";
            }
            m_model = DataSystems->LoadCashedModel(modelName);

            if (m_model) {
                m_meshType = MeshType::Model;
            }
        }
        if (modelJson.contains("meshIndex"))
        {
            m_meshIndex = modelJson["meshIndex"];
        }
    }

    // 메시 타입 정보 복원
    if (json.contains("mesh"))
    {
        const auto& meshJson = json["mesh"];
        if (meshJson.contains("type"))
        {
            MeshType jsonType = static_cast<MeshType>(meshJson["type"]);
            // 모델이 로드되지 않았거나, JSON에서 명시적으로 다른 타입을 지정한 경우에만 변경
            if (!m_model || jsonType != MeshType::Model) {
                SetMeshType(jsonType);
            }
        }
        if (meshJson.contains("meshIndex"))
        {
            m_meshIndex = meshJson["meshIndex"];
        }
    }

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

    // 클리핑 관련 설정 복원
    if (json.contains("clipping"))
    {
        const auto& clippingJson = json["clipping"];

        if (clippingJson.contains("enabled"))
        {
            bool enabled = clippingJson["enabled"];
            EnableClipping(enabled);
        }

        if (clippingJson.contains("animating"))
        {
            m_isClippingAnimating = clippingJson["animating"];
        }

        if (clippingJson.contains("animationSpeed"))
        {
            m_clippingAnimationSpeed = clippingJson["animationSpeed"];
        }

        // 클리핑 파라미터 복원
        if (clippingJson.contains("params"))
        {
            const auto& paramsJson = clippingJson["params"];

            if (paramsJson.contains("clippingProgress"))
            {
                float progress = paramsJson["clippingProgress"];
                SetClippingProgress(progress);
            }

            if (paramsJson.contains("clippingAxis"))
            {
                const auto& axisJson = paramsJson["clippingAxis"];
                Mathf::Vector3 axis(
                    axisJson.value("x", 0.0f),
                    axisJson.value("y", 1.0f),
                    axisJson.value("z", 0.0f)
                );
                SetClippingAxis(axis);
            }

            // boundsMin/Max 관련 코드 제거됨 (상대 좌표계는 고정된 -1~1 범위 사용)

            if (paramsJson.contains("clippingEnabled"))
            {
                float enabled = paramsJson["clippingEnabled"];
                EnableClipping(enabled > 0.5f);
            }
        }

        // 하위 호환성: 기존 boundsMin/Max가 있는 파일 처리
        if (clippingJson.contains("params"))
        {
            const auto& paramsJson = clippingJson["params"];

            // 기존 파일에 boundsMin/Max가 있어도 무시하고 경고 로그만 출력
            if (paramsJson.contains("boundsMin") || paramsJson.contains("boundsMax"))
            {
                // 로그 출력 (옵션)
                // Logger::Warning("Legacy boundsMin/Max detected in clipping data. Using relative coordinate system instead.");
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

    // 클리핑 버퍼 업데이트
    if (IsClippingEnabled())
    {
        if (DeviceState::g_pDevice && DeviceState::g_pDeviceContext)
        {
            UpdateClippingBuffer();
            OnClippingStateChanged();
        }
    }
}

std::string MeshModuleGPU::GetModuleType() const
{
    return "MeshModuleGPU";
}

void MeshModuleGPU::SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount)
{
    m_particleSRV = particleSRV;
    m_instanceCount = instanceCount;
}

void MeshModuleGPU::SetupRenderTarget(RenderPassData* renderData)
{
    auto& deviceContext = DeviceState::g_pDeviceContext;
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

void MeshModuleGPU::Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection)
{
    auto currentMesh = GetCurrentMesh();
    if (!currentMesh || !m_particleSRV || m_instanceCount == 0)
        return;

    // 월드 행렬 저장 및 역행렬 계산
    m_worldMatrix = world;
    m_invWorldMatrix = world.Invert();

    // 클리핑 애니메이션 처리
    if (m_isClippingAnimating && IsClippingEnabled())
    {
        float currentTime = Time->GetTotalSeconds();
        float animatedProgress = (sin(currentTime * m_clippingAnimationSpeed) + 1.0f) * 0.5f;
        SetClippingProgress(animatedProgress);
        UpdateClippingBuffer();
    }

    auto& deviceContext = DeviceState::g_pDeviceContext;

    // 상수 버퍼 업데이트
    UpdateConstantBuffer(world, view, projection);
    deviceContext->VSSetConstantBuffers(0, 1, m_constantBuffer.GetAddressOf());

    // 클리핑 상수 버퍼 바인딩 및 업데이트
    if (IsClippingEnabled() && m_clippingBuffer)
    {
        UpdateClippingBuffer();
        deviceContext->PSSetConstantBuffers(1, 1, m_clippingBuffer.GetAddressOf());
    }

    // 파티클 SRV 바인딩
    deviceContext->VSSetShaderResources(0, 1, &m_particleSRV);

    // 텍스처 바인딩
    if (m_assignedTexture)
    {
        if (m_assignedTexture->m_pSRV)
        {
            deviceContext->PSSetShaderResources(0, 1, &m_assignedTexture->m_pSRV);
        }
    }
    else if (m_meshType == MeshType::Model && m_model)
    {
        // 모델의 기본 텍스처 사용 (BaseColor 텍스처)
        auto material = m_model->GetMaterial(m_meshIndex);
        if (material && material->m_pBaseColor && material->m_pBaseColor->m_pSRV)
        {
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

    // 리소스 정리
    ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
    deviceContext->VSSetShaderResources(0, 1, nullSRV);
    deviceContext->PSSetShaderResources(0, 1, nullSRV);

    if (IsClippingEnabled())
    {
        ID3D11Buffer* nullBuffer[1] = { nullptr };
        deviceContext->PSSetConstantBuffers(1, 1, nullBuffer);
    }
}

void MeshModuleGPU::SetTexture(Texture* texture)
{
    m_assignedTexture = texture;
}

void MeshModuleGPU::Release()
{
    // 임시 큐브 메시 정리
    if (m_tempCubeMesh)
    {
        delete m_tempCubeMesh;
        m_tempCubeMesh = nullptr;
    }

    m_constantBuffer.Reset();
    m_clippingBuffer.Reset();
    m_model = nullptr;
    m_meshIndex = 0;
    m_particleSRV = nullptr;
    m_instanceCount = 0;
    m_assignedTexture = nullptr;
}