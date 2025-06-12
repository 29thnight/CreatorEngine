#include "SparkleEffect.h"
#include "ImGuiRegister.h"
#include "../Camera.h"
#include "RenderModules.h"
#include "../DataSystem.h"

SparkleEffect::SparkleEffect(const Mathf::Vector3& position, int maxParticles) : ParticleSystem(maxParticles), m_delta(0.0f)
{
    m_position = position;

    // ���� �ؽ�ó ����
    m_sparkleTexture = DataSystems->LoadTexture("123.png");
    {
        ImGui::ContextRegister("Sparkle Effect", true, [&]()
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
                    
                    if (ImGui::Button("Show Module List"))
                    {
                        ImGui::OpenPopup("Module List");
                    }

                    if (ImGui::BeginPopup("Module List"))
                    {
                        ImGui::Text("Particle Modules");
                        ImGui::Separator();

                        int index = 0;
                        for (auto it = m_moduleList.begin(); it != m_moduleList.end(); ++it)
                        {
                            ParticleModule& module = *it;
                            ImGui::Text("Node %d: %s", index++, typeid(module).name());

                            // �� ��⿡ ���� �߰� ������ ǥ���Ϸ���:
                            ImGui::Indent();
                            ImGui::Text("Easing: %s", module.IsEasingEnabled() ? "Enabled" : "Disabled");
                            // ���⿡ �� ���� ��� �Ӽ� ǥ��
                            ImGui::Unindent();
                        }

                        ImGui::Separator();
                        ImGui::Text("Total nodes: %d", index);

                        ImGui::EndPopup();
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Tab 2"))
                {
                    if (ImGui::Button("point"))
                    {
                        auto module = GetModule<SpawnModuleCS>();
                        module->SetEmitterShape(EmitterType::point);
                    }
                    if (ImGui::Button("sphere"))
                    {
                        auto module = GetModule<SpawnModuleCS>();
                        module->SetEmitterShape(EmitterType::sphere);
                    }
                    if (ImGui::Button("box"))
                    {
                        auto module = GetModule<SpawnModuleCS>();
                        module->SetEmitterShape(EmitterType::box);
                    }
                    if (ImGui::Button("cone"))
                    {
                        auto module = GetModule<SpawnModuleCS>();
                        module->SetEmitterShape(EmitterType::cone);
                    }
                    if (ImGui::Button("circle"))
                    {
                        auto module = GetModule<SpawnModuleCS>();
                        module->SetEmitterShape(EmitterType::circle);
                    }
                    {
                        auto module = GetModule<LifeModuleCS>();
                        ImGui::InputScalar("MaxParticles", ImGuiDataType_U32, &m_max);
                        if (ImGui::Button("SetMaxParticles"))
                        {
                            ResizeParticleSystem(m_max);
                        }

                        ImGui::Text("Active Particles: %d / %d", module->GetActiveParticleCount(), m_maxParticles);
                        ImGui::Text("Instance Count: %d", m_billboardModule ? m_billboardModule->m_instanceCount : 0);
                    }
                    {
                        ImGui::InputFloat("Spawn Rate", &m_rate);
                        auto module = GetModule<SpawnModuleCS>();
                        ImGui::Text("Spawn Rate : %f", module->m_spawnRate);
                        if (ImGui::Button("SetSpawnRate"))
                        {
                            module->SetSpawnRate(m_rate);
                        }
                       
                    }
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Tab 3"))
                {
                    // ��ġ ���� UI
                    ImGui::Text("Position");
                    ImGui::SliderFloat("X", &m_position.x, -50.0f, 50.0f);
                    ImGui::SliderFloat("Y", &m_position.y, -50.0f, 50.0f);
                    ImGui::SliderFloat("Z", &m_position.z, -50.0f, 50.0f);

                    SetPosition(m_position);

                    if (ImGui::Button("Spawn Burst"))
                    {
                        SpawnSparklesBurst(20);
                    }

                    static bool isG = false;
                    if (ImGui::Checkbox("Gravity", &isG))
                    {
                        auto module = GetModule<MovementModuleCS>();
                        module->SetUseGravity(isG);
                    }

                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Tab 4"))
                {
                    auto& particleTemplate = GetModule<SpawnModuleCS>()->m_particleTemplate;
                    ImGui::SliderFloat3("velocity", &particleTemplate.velocity.x, -50.f, 50.f);
                    ImGui::SliderFloat3("accelaration", &particleTemplate.acceleration.x, -50.f, 50.f);
                    ImGui::SliderFloat2("size", &particleTemplate.size.x, -50.f, 50.f);
                    ImGui::ColorEdit4("color", &particleTemplate.color.x);
                    ImGui::SliderFloat("lifetime", &particleTemplate.lifeTime, 0.0f, 50.0f);
                    ImGui::SliderFloat("rotation", &particleTemplate.rotation, -50.0f, 50.0f);
                    ImGui::SliderFloat("rotatespeed", &particleTemplate.rotatespeed, -50.0f, 50.0f);

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
    
}

void SparkleEffect::InitializeModules()
{
    // ���� ��� �߰� (����Ʈ�� ��ġ�� m_position)
    //AddModule<SpawnModule>(10.0f, EmitterType::box);
    AddModule<LifeModuleCS>();
    AddModule<SpawnModuleCS>(10.0f, EmitterType::box, 1000000);




    // ���� ��� �߰�
    //AddModule<LifeModule>();

    // ������ ��� �߰� (�߷� ����, �����Ӱ� �����̴� ��¦��)
    auto movementModule = AddModule<MovementModuleCS>();
    //movementModule->SetUseGravity(false);

    // ���� ��� �߰� (��¦�̴� ȿ���� ���� ���� ��ȭ)
    //auto colorModule = AddModule<ColorModule>();

    // �� ��ȸ
    //std::vector<std::pair<float, Mathf::Vector4>> rainbowGradient = {
    //{0.0f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f)},  // ����
    //{0.5f, Mathf::Vector4(0.0f, 1.0f, 0.0f, 1.0f)},  // �ʷ�
    //{0.9f, Mathf::Vector4(0.0f, 0.0f, 1.0f, 1.0f)}, // �Ķ�
    //};
    
    //colorModule->SetTransitionMode(ColorTransitionMode::Discrete);
    //colorModule->SetColorGradient(rainbowGradient);
    //colorModule->SetEasingType(EasingEffect::InOutSine);
    //colorModule->EnableEasing(true);

    //auto collisionModule = AddModule<CollisionModule>();
    //collisionModule->SetBounceFactor(1.0f);
    //collisionModule->SetFloorHeight(-10.0f);

    // ũ�� ��� �߰� (�����̴� ȿ��)
    //auto sizeModule = AddModule<SizeModule>();
    //sizeModule->SetStartSize(0.2f);
    //sizeModule->SetEndSize(1.0f);
    //sizeModule->SetSizeOverLifeFunction([this](float t) {
    //    // ��¦�̴� ȿ���� ���� ���� �ĵ�
    //    float pulse = 0.7f + 0.3f * sin(t * m_sparkleParams->speed * 10.0f);
    //    float baseSize = 0.2f + t * (0.05f - 0.2f);
    //    return Mathf::Vector2(baseSize * pulse, baseSize * pulse);
    //   });

     // ���� ��� ����
    m_billboardModule = AddRenderModule<BillboardModuleGPU>();

    m_billboardModule->GetPSO()->m_pixelShader = &ShaderSystem->PixelShaders["Sparkle"];
  
}

void SparkleEffect::Update(float delta)
{
    if (!m_isRunning)
        return;

    // �ð� ������Ʈ
    m_delta += delta;
    if (m_delta > 10000.0f) {
        m_delta = 0.0f;
    }

    UpdateInstanceData();

    // �⺻ ������Ʈ ȣ�� (��� ������Ʈ ����)
    ParticleSystem::Update(delta);
}

void SparkleEffect::Render(RenderScene& scene, Camera& camera)
{
    if (!m_isRunning) // || m_activeParticleCount == 0)
        return;

    if (!RenderPassData::VaildCheck(&camera)) return;
    auto renderData = RenderPassData::GetData(&camera);

    m_billboardModule->m_particleSRV = GetCurrentRenderingSRV();
    m_billboardModule->m_instanceCount = m_activeParticleCount;

    m_renderModules[0]->SaveRenderState();

    ID3D11RenderTargetView* rtv = renderData->m_renderTarget->GetRTV();
    DirectX11::OMSetRenderTargets(1, &rtv, renderData->m_depthStencil->m_pDSV);

    ID3D11ShaderResourceView* srv = m_sparkleTexture->m_pSRV;
    DirectX11::PSSetShaderResources(0, 1, &srv);

    //if (m_billboardModule && m_activeParticleCount > 0) {
    //    m_billboardModule->SetupInstancing(m_instanceData.data(), m_activeParticleCount);
    //}

    ParticleSystem::Render(scene, camera);

    m_renderModules[0]->CleanupRenderState();

    m_renderModules[0]->RestoreRenderState();
}

void SparkleEffect::SpawnSparklesBurst(int count)
{
    // ������ ��ƼŬ �� ��Ȱ�� ������ ���� ã�� Ȱ��ȭ
    int spawned = 0;

    // �����ϰ� ��� ������ ��ƼŬ ���� üũ
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
            particle.lifeTime = 0.5f + (rand() / (float)RAND_MAX) * 1.0f; // 0.5 ~ 1.5��
            particle.rotation = rand() / (float)RAND_MAX * 6.28f;  // 0 ~ 2��
            particle.rotatespeed = (rand() / (float)RAND_MAX - 0.5f) * 2.0f;
            particle.size = Mathf::Vector2(0.2f, 0.2f);
            particle.isActive = true;

            // Ȱ�� ��ƼŬ ī��Ʈ ����
            m_activeParticleCount++;
            spawned++;
        }
    }
}

void SparkleEffect::UpdateInstanceData()
{
    // ���� ũ�� ���� ���� m_activeParticleCount�� 0���� ū�� Ȯ��
    if (m_activeParticleCount <= 0) {
        m_instanceData.clear();
        return;
    }

    // �ν��Ͻ� ������ �迭 ũ�� ����
    m_instanceData.resize(m_activeParticleCount);

    int instanceIndex = 0;
    Mathf::Vector3 worldPos;

    for (const auto& particle : m_particleData)
    {
        if (particle.isActive)
        {
            // ���� üũ �߰�
            if (instanceIndex >= m_activeParticleCount) {
                std::cout << "Warning: instanceIndex (" << instanceIndex
                    << ") exceeds m_activeParticleCount (" << m_activeParticleCount << ")" << std::endl;
                break;
            }

            worldPos = particle.position + m_position;
            m_instanceData[instanceIndex].Position = Mathf::Vector4(
                worldPos.x,
                worldPos.y,
                worldPos.z,
                1.0f
            );

            m_instanceData[instanceIndex].TexCoord = particle.size;
            m_instanceData[instanceIndex].TexIndex = static_cast<UINT>(particle.age / particle.lifeTime * 10) % 10;
            m_instanceData[instanceIndex].Color = particle.color;
            instanceIndex++;
        }
    }

    // ���� Ȱ�� ��ƼŬ ���� ī��Ʈ�� ��ġ���� �ʴ� ��� ����
    if (instanceIndex != m_activeParticleCount) {
        //std::cout << "Adjusting m_activeParticleCount from " << m_activeParticleCount
        //    << " to " << instanceIndex << std::endl;
        m_activeParticleCount = instanceIndex;
        m_instanceData.resize(instanceIndex);
    }
}

