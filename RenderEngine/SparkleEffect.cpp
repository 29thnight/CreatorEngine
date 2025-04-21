#include "SparkleEffect.h"
#include "ImGuiRegister.h"
#include "Camera.h"

SparkleEffect::SparkleEffect(const Mathf::Vector3& position, int maxParticles) : EffectModules(maxParticles), m_delta(0.0f)
{
    m_position = position;
    {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(SparkleParameters);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        DeviceState::g_pDevice->CreateBuffer(&cbDesc, nullptr, m_constantBuffer.GetAddressOf());
    }

    m_sparkleParams = new SparkleParameters;
    m_sparkleParams->time = 0.0f;
    m_sparkleParams->intensity = 1.5f;
    m_sparkleParams->speed = 3.0f;
    m_sparkleParams->color = Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    m_sparkleParams->size = Mathf::Vector2(1.0f, 1.0f);
    m_sparkleParams->range = Mathf::Vector2(4.0f, 2.0f);
    SetParameters(m_sparkleParams);

    // 렌더 모듈 설정
    m_billboardModule = AddRenderModule<BillboardModule>();
    m_billboardModule->Initialize();
    BillboardVertex vertex;
    vertex.position = float4(position.x, position.y, position.z, 1.0f);
    m_billboardModule->CreateBillboard(&vertex, BillBoardType::Basic);

    m_billboardModule->GetPSO()->m_pixelShader = &ShaderSystem->PixelShaders["Sparkle"];

    // 실제 텍스처 사진
    m_sparkleTexture = std::shared_ptr<Texture>(Texture::LoadFormPath("God.jpg"));

    {
        ImGui::ContextRegister("Sparkle Effect", [&]()
            {
                //ImGui::SetWindowFocus("Sparkle Effect");

                if (ImGui::BeginTabBar("Setting"))
                {

                    if (ImGui::BeginTabItem("Tab 1"))
                    {
                        if (ImGui::Button("Play"))
                        {
                            Play();
                        }

                        if (ImGui::Button("Stop"))
                        {
                            Stop();
                        }

                        if (ImGui::Button("Resume"))
                        {
                            Resume();
                        }

                        if (ImGui::Button("Pause"))
                        {
                            Pause();
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Tab 2"))
                    {
                        if (ImGui::Button("point"))
                        {
                            auto module = GetModule<SpawnModule>();
                            module->SetEmitterShape(EmitterType::point);
                        }
                        if (ImGui::Button("sphere"))
                        {
                            auto module = GetModule<SpawnModule>();
                            module->SetEmitterShape(EmitterType::sphere);
                        }
                        if (ImGui::Button("box"))
                        {
                            auto module = GetModule<SpawnModule>();
                            module->SetEmitterShape(EmitterType::box);
                        }
                        if (ImGui::Button("cone"))
                        {
                            auto module = GetModule<SpawnModule>();
                            module->SetEmitterShape(EmitterType::cone);
                        }
                        if (ImGui::Button("circle"))
                        {
                            auto module = GetModule<SpawnModule>();
                            module->SetEmitterShape(EmitterType::circle);
                        }
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Tab 3"))
                    {
                        ImGui::SliderFloat("Intensity", &m_sparkleParams->intensity, 0.0f, 5.0f);
                        ImGui::SliderFloat("Speed", &m_sparkleParams->speed, 0.1f, 10.0f);
                        ImGui::ColorEdit3("Color", &m_sparkleParams->color.x);
                        ImGui::SliderFloat2("Size", &m_sparkleParams->size.x, 0.1f, 10.0f);

                        // 위치 조정 UI
                        ImGui::Text("Position");
                        ImGui::SliderFloat("X", &m_position.x, -50.0f, 50.0f);
                        ImGui::SliderFloat("Y", &m_position.y, -50.0f, 50.0f);
                        ImGui::SliderFloat("Z", &m_position.z, -50.0f, 50.0f);

                        if (ImGui::Button("Spawn Burst"))
                        {
                            SpawnSparklesBurst(20);
                        }

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }

             

                
            });
    }

    InitializeModules();

    Play();
}

SparkleEffect::~SparkleEffect()
{
    if (m_sparkleParams)
    {
        delete m_sparkleParams;
        m_sparkleParams = nullptr;
    }
}

void SparkleEffect::InitializeModules()
{
    // 스폰 모듈 추가 (이펙트의 위치는 m_position)
    m_spawnModule = AddModule<SpawnModule>(m_maxParticles, EmitterType::point);
    m_spawnModule->m_particleTemplate.lifeTime = 0.0f;

    // 수명 모듈 추가
    AddModule<LifeModule>();

    // 움직임 모듈 추가 (중력 없음, 자유롭게 움직이는 반짝임)
    auto movementModule = AddModule<MovementModule>();
    movementModule->SetUseGravity(false);

    // 색상 모듈 추가 (반짝이는 효과를 위한 투명도 변화)
    //auto colorModule = AddModule<ColorModule>();
    //colorModule->SetColorGradient({
    //    {0.0f, Mathf::Vector4(m_sparkleParams->color.x, m_sparkleParams->color.y, m_sparkleParams->color.z, 0.0f)},
    //    {0.1f, Mathf::Vector4(m_sparkleParams->color.x, m_sparkleParams->color.y, m_sparkleParams->color.z, 0.9f)},
    //    {0.4f, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f)},
    //    {0.7f, Mathf::Vector4(m_sparkleParams->color.x, m_sparkleParams->color.y, m_sparkleParams->color.z, 0.7f)},
    //    {1.0f, Mathf::Vector4(m_sparkleParams->color.x * 0.5f, m_sparkleParams->color.y * 0.5f, m_sparkleParams->color.z * 0.5f, 0.0f)}
    //    });
    
    // 무지개 색
    //std::vector<std::pair<float, Mathf::Vector4>> rainbowGradient = {
    //{0.0f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f)},  // 빨강
    //{0.16f, Mathf::Vector4(1.0f, 0.5f, 0.0f, 1.0f)}, // 주황
    //{0.33f, Mathf::Vector4(1.0f, 1.0f, 0.0f, 1.0f)}, // 노랑
    //{0.5f, Mathf::Vector4(0.0f, 1.0f, 0.0f, 1.0f)},  // 초록
    //{0.66f, Mathf::Vector4(0.0f, 0.0f, 1.0f, 1.0f)}, // 파랑
    //{0.83f, Mathf::Vector4(0.3f, 0.0f, 0.5f, 1.0f)}, // 남색
    //{1.0f, Mathf::Vector4(0.5f, 0.0f, 0.5f, 1.0f)}   // 보라
    //};
    //
    //colorModule->SetColorGradient(rainbowGradient);

    // 크기 모듈 추가 (깜빡이는 효과)
    //auto sizeModule = AddModule<SizeModule>();
    //sizeModule->SetStartSize(0.2f);
    //sizeModule->SetEndSize(1.0f);
    //sizeModule->SetSizeOverLifeFunction([this](float t) {
    //    // 반짝이는 효과를 위한 사인 파동
    //    float pulse = 0.7f + 0.3f * sin(t * m_sparkleParams->speed * 10.0f);
    //    float baseSize = 0.2f + t * (0.05f - 0.2f);
    //    return Mathf::Vector2(baseSize * pulse, baseSize * pulse);
    //   });
}

void SparkleEffect::Update(float delta)
{
    if (!m_isRunning)
        return;

    // 시간 업데이트
    m_delta += delta;
    if (m_delta > 10000.0f) {
        m_delta = 0.0f;
    }

    m_sparkleParams->time = m_delta;

    UpdateConstantBuffer();

    UpdateInstanceData();

    // 기본 업데이트 호출 (모듈 업데이트 포함)
    EffectModules::Update(delta);
}

void SparkleEffect::Render(RenderScene& scene, Camera& camera)
{
    if (!m_isRunning || m_activeParticleCount == 0)
        return;

    auto& deviceContext = DeviceState::g_pDeviceContext;

    m_renderModules[0]->SaveRenderState();

    DeviceState::g_pDeviceContext->PSSetConstantBuffers(3, 1, m_constantBuffer.GetAddressOf());

    ID3D11RenderTargetView* rtv = camera.m_renderTarget->GetRTV();
    deviceContext->OMSetRenderTargets(1, &rtv, DeviceState::g_pDepthStencilView);

    ID3D11ShaderResourceView* srv = m_sparkleTexture->m_pSRV;
    DirectX11::PSSetShaderResources(0, 1, &srv);

    EffectModules::Render(scene, camera);

    m_renderModules[0]->CleanupRenderState();

    m_renderModules[0]->RestoreRenderState();
}

void SparkleEffect::SpawnSparklesBurst(int count)
{
    // 보유한 파티클 중 비활성 상태인 것을 찾아 활성화
    int spawned = 0;

    // 안전하게 사용 가능한 파티클 수를 체크
    int availableSlots = m_maxParticles - m_activeParticleCount;
    int actualSpawnCount = std::min(count, availableSlots);

    for (auto& particle : m_particleData) {
        if (!particle.isActive && spawned < actualSpawnCount) {
            particle.position = Mathf::Vector3(0.0f, 0.0f, 0.0f);
            particle.velocity = Mathf::Vector3(
                (rand() / (float)RAND_MAX - 0.5f) * 4.0f,  // x: -2 ~ 2
                (rand() / (float)RAND_MAX - 0.5f) * 4.0f,  // y: -2 ~ 2
                (rand() / (float)RAND_MAX - 0.5f) * 4.0f   // z: -2 ~ 2
            );
            particle.acceleration = Mathf::Vector3(0.0f, 0.0f, 0.0f);
            particle.age = 0.0f;
            particle.lifeTime = 0.5f + (rand() / (float)RAND_MAX) * 1.0f; // 0.5 ~ 1.5초
            particle.rotation = rand() / (float)RAND_MAX * 6.28f;  // 0 ~ 2π
            particle.rotatespeed = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
            particle.size = Mathf::Vector2(0.2f, 0.2f);
            particle.color = Mathf::Vector4(
                m_sparkleParams->color.x,
                m_sparkleParams->color.y,
                m_sparkleParams->color.z,
                1.0f
            );
            particle.isActive = true;

            // 활성 파티클 카운트 증가
            m_activeParticleCount++;
            spawned++;
        }
    }
}

void SparkleEffect::UpdateConstantBuffer()
{
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    DirectX11::ThrowIfFailed(
        DeviceState::g_pDeviceContext->Map(
            m_constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource
        )
    );
    memcpy(mappedResource.pData, m_sparkleParams, sizeof(SparkleParameters));
    DeviceState::g_pDeviceContext->Unmap(m_constantBuffer.Get(), 0);
}

void SparkleEffect::UpdateInstanceData()
{
    // 벡터 크기 조정 전에 m_activeParticleCount가 0보다 큰지 확인
    if (m_activeParticleCount <= 0) {
        m_instanceData.clear();
        return;
    }

    // 인스턴스 데이터 배열 크기 조정
    m_instanceData.resize(m_activeParticleCount);

    int instanceIndex = 0;
    Mathf::Vector3 worldPos;

    for (const auto& particle : m_particleData)
    {
        if (particle.isActive)
        {
            // 범위 체크 추가
            if (instanceIndex >= m_activeParticleCount) {
                std::cout << "Warning: instanceIndex (" << instanceIndex
                    << ") exceeds m_activeParticleCount (" << m_activeParticleCount << ")" << std::endl;
                break;
            }

            worldPos = particle.position + m_position;
            m_instanceData[instanceIndex].position = Mathf::Vector4(
                worldPos.x,
                worldPos.y,
                worldPos.z,
                1.0f
            );

            m_instanceData[instanceIndex].size = particle.size * m_sparkleParams->size;
            m_instanceData[instanceIndex].id = static_cast<UINT>(particle.age / particle.lifeTime * 10) % 10;
            m_instanceData[instanceIndex].color = particle.color;
            instanceIndex++;
        }
    }

    // 실제 활성 파티클 수와 카운트가 일치하지 않는 경우 조정
    if (instanceIndex != m_activeParticleCount) {
        std::cout << "Adjusting m_activeParticleCount from " << m_activeParticleCount
            << " to " << instanceIndex << std::endl;
        m_activeParticleCount = instanceIndex;
        m_instanceData.resize(instanceIndex);
    }
}

void SparkleEffect::SetParameters(SparkleParameters* params)
{
    if (params) {
        *m_sparkleParams = *params;
        UpdateConstantBuffer();
    }
}
