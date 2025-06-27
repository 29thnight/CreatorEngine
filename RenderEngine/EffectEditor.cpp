#include "EffectEditor.h"
#include "IconsFontAwesome6.h"
#include "EffectManager.h"

EffectEditor::EffectEditor()
{
    ImGui::ContextRegister("EffectEdit", false, [&]() {
        if (ImGui::BeginMenuBar())
        {
            ImGui::Text("Effect Editor");

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 30);
            if (ImGui::SmallButton(ICON_FA_X))
            {
                ImGui::GetContext("EffectEdit").Close();
            }
            ImGui::EndMenuBar();
        }

        RenderMainEditor();
        }, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);
    ImGui::GetContext("EffectEdit").Close();

    Texture* example = DataSystems->LoadTexture("123.png");

    m_textures.push_back(example);
}

void EffectEditor::Release()
{
    m_tempEmitters.clear();
    m_editingEmitter = nullptr;
}

void EffectEditor::Update(float delta)
{
   for (auto& tempEmitter : m_tempEmitters) {
       if (tempEmitter.particleSystem && tempEmitter.isPlaying) {
           tempEmitter.particleSystem->Update(delta);
       }
   }

   // ���� ���� ���� ������ ������Ʈ
   if (m_editingEmitter) {
       m_editingEmitter->Update(delta);
   }
}

void EffectEditor::Render(RenderScene& scene, Camera& camera)
{
    // �̸������ �ӽ� �����͵鸸 ������
    for (auto& tempEmitter : m_tempEmitters) {
        if (tempEmitter.particleSystem && tempEmitter.isPlaying) {
            tempEmitter.particleSystem->Render(scene, camera);
        }
    }

    // ���� ���� ���� ������ ������
    if (m_editingEmitter) {
        m_editingEmitter->Render(scene, camera);
    }
}

void EffectEditor::PlayPreview()
{
    for (auto& tempEmitter : m_tempEmitters) {
        tempEmitter.isPlaying = true;
        if (tempEmitter.particleSystem) {
            tempEmitter.particleSystem->Play();
        }
    }
}

void EffectEditor::StopPreview()
{
    for (auto& tempEmitter : m_tempEmitters) {
        tempEmitter.isPlaying = false;
        if (tempEmitter.particleSystem) {
            tempEmitter.particleSystem->Stop();
        }
    }
}

void EffectEditor::PlayEmitterPreview(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters[index].isPlaying = true;
        if (m_tempEmitters[index].particleSystem) {
            m_tempEmitters[index].particleSystem->Play();
        }
    }
}

void EffectEditor::StopEmitterPreview(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters[index].isPlaying = false;
        if (m_tempEmitters[index].particleSystem) {
            m_tempEmitters[index].particleSystem->Stop();
        }
    }
}

void EffectEditor::RemoveEmitter(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters.erase(m_tempEmitters.begin() + index);
    }
}

void EffectEditor::SetTexture(Texture* tex)
{
    m_textures.push_back(tex);
}

void EffectEditor::ExportToManager(const std::string& effectName)
{
    if (m_tempEmitters.empty()) {
        std::cout << "No emitters to export!" << std::endl;
        return;
    }

    // �ӽ� �����͵��� ���� ParticleSystem ���ͷ� ��ȯ
    std::vector<std::shared_ptr<ParticleSystem>> emittersToExport;
    for (const auto& tempEmitter : m_tempEmitters) {
        if (tempEmitter.particleSystem) {
            emittersToExport.push_back(tempEmitter.particleSystem);
        }
    }

    // EffectManager�� ���
    if (auto* manager = EffectManager::GetCurrent()) {
        manager->RegisterCustomEffect(effectName, emittersToExport);

        // ��ϵ� Effect�� �ٷ� ���
        if (auto* registeredEffect = manager->GetEffect(effectName)) {
            registeredEffect->Play();
        }

        std::cout << "Effect exported and started: " << effectName << std::endl;
    }
    else {
        std::cout << "No active EffectManager found!" << std::endl;
    }
}



void EffectEditor::AssignTextureToEmitter(int emitterIndex, int textureIndex)
{
    if (emitterIndex >= 0 && emitterIndex < m_tempEmitters.size() &&
        textureIndex >= 0 && textureIndex < m_textures.size()) {

        auto emitter = m_tempEmitters[emitterIndex].particleSystem;
        if (emitter) {
            // ��� ���� ��⿡ �ؽ�ó ����
            for (auto& renderModule : emitter->GetRenderModules()) {
                renderModule->SetTexture(m_textures[textureIndex]);
            }
        }
    }
}

void EffectEditor::RenderModuleDetailEditor()
{
    if (!m_modifyingSystem || m_selectedModuleForEdit < 0) return;

    auto& moduleList = m_modifyingSystem->GetModuleList();

    // �ε����� ��� ã��
    int currentIndex = 0;
    ParticleModule* targetModule = nullptr;

    for (auto& module : moduleList) {
        if (currentIndex == m_selectedModuleForEdit) {
            targetModule = &module;
            break;
        }
        currentIndex++;
    }

    if (!targetModule) return;

    ImGui::Text("Module Settings:");
    ImGui::Separator();

    // �� ��� Ÿ�Ժ��� ���� ���� UI ������
    if (auto* spawnModule = dynamic_cast<SpawnModuleCS*>(targetModule)) {
        RenderSpawnModuleEditor(spawnModule);
    }
    else if (auto* movementModule = dynamic_cast<MovementModuleCS*>(targetModule)) {
        RenderMovementModuleEditor(movementModule);
    }
    else if (auto* colorModule = dynamic_cast<ColorModuleCS*>(targetModule)) {
        RenderColorModuleEditor(colorModule);
    }
    else if (auto* sizeModule = dynamic_cast<SizeModuleCS*>(targetModule)) {
        RenderSizeModuleEditor(sizeModule);
    }
}

void EffectEditor::RenderMainEditor()
{
    if (!m_isEditingEmitter && !m_isModifyingEmitter) {
        if (ImGui::Button("Create New Emitter")) {
            StartCreateEmitter();
        }

        RenderPreviewControls();

        if (!m_tempEmitters.empty()) {
            ImGui::Separator();
            ImGui::Text("Export to Game");

            static char effectName[256] = "MyCustomEffect";
            ImGui::InputText("Effect Name", effectName, sizeof(effectName));

            if (ImGui::Button("Export to Game")) {
                ExportToManager(std::string(effectName));
            }
        }

        // �ӽ� �����͵� ǥ�� (������ �κ�)
        if (!m_tempEmitters.empty()) {
            ImGui::Separator();
            ImGui::Text("Preview Emitters (%zu)", m_tempEmitters.size());

            for (int i = 0; i < m_tempEmitters.size(); ++i) {
                ImGui::PushID(i);

                ImGui::Text("Emitter %d: %s", i, m_tempEmitters[i].name.c_str());

                // ��ġ ����
                if (m_tempEmitters[i].particleSystem) {
                    Mathf::Vector3 currentPos = m_tempEmitters[i].particleSystem->GetPosition();
                    float pos[3] = { currentPos.x, currentPos.y, currentPos.z };
                    if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                        m_tempEmitters[i].particleSystem->SetPosition(Mathf::Vector3(pos[0], pos[1], pos[2]));
                    }
                }

                // �ؽ�ó �Ҵ� UI
                ImGui::Text("Assign Texture:");
                ImGui::SameLine();

                if (!m_textures.empty()) {
                    static int selectedTextureIndex = 0;
                    std::string comboLabel = "Texture##" + std::to_string(i);

                    if (ImGui::BeginCombo(comboLabel.c_str(), ("Texture " + std::to_string(selectedTextureIndex)).c_str())) {
                        for (int t = 0; t < m_textures.size(); ++t) {
                            bool isSelected = (selectedTextureIndex == t);
                            std::string textureLabel = "Texture " + std::to_string(t);
                            if (ImGui::Selectable(textureLabel.c_str(), isSelected)) {
                                selectedTextureIndex = t;
                            }
                            if (isSelected) {
                                ImGui::SetItemDefaultFocus();
                            }
                        }
                        ImGui::EndCombo();
                    }

                    ImGui::SameLine();
                    if (ImGui::Button(("Assign##" + std::to_string(i)).c_str())) {
                        AssignTextureToEmitter(i, selectedTextureIndex);
                    }
                }
                else {
                    ImGui::Text("No textures loaded");
                }

                // ��Ʈ�� ��ư�� (Play, Stop, Modify, Remove)
                if (ImGui::Button("Play")) {
                    PlayEmitterPreview(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    StopEmitterPreview(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Modify")) {
                    StartModifyEmitter(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    RemoveEmitter(i);
                }

                ImGui::PopID();
            }
        }
    }
    else if (m_isEditingEmitter) {
        RenderEmitterEditor();
    }
    else if (m_isModifyingEmitter) {
        RenderModifyEmitterEditor();
    }
}

void EffectEditor::RenderPreviewControls()
{
    if (!m_tempEmitters.empty()) {
        ImGui::Separator();
        ImGui::Text("Preview Controls");

        if (ImGui::Button("Play All Preview")) {
            PlayPreview();
        }
        ImGui::SameLine();
        if (ImGui::Button("Stop All Preview")) {
            StopPreview();
        }
        ImGui::Separator();
    }
}

void EffectEditor::RenderModifyEmitterEditor()
{
    if (!m_modifyingSystem) return;

    ImGui::Text("Modifying Emitter: %s", m_tempEmitters[m_modifyingEmitterIndex].name.c_str());
    ImGui::Separator();

    // ��� ����Ʈ ǥ��
    ImGui::Text("Particle Modules:");
    auto& moduleList = m_modifyingSystem->GetModuleList();
    int moduleIndex = 0;

    for (auto it = moduleList.begin(); it != moduleList.end(); ++it) {
        ParticleModule& module = *it;

        ImGui::PushID(moduleIndex);

        // ��� Ÿ�� ǥ��
        std::string moduleName = "Unknown Module";
        if (dynamic_cast<SpawnModuleCS*>(&module)) {
            moduleName = "Spawn Module";
        }
        else if (dynamic_cast<MovementModuleCS*>(&module)) {
            moduleName = "Movement Module";
        }
        else if (dynamic_cast<ColorModuleCS*>(&module)) {
            moduleName = "Color Module";
        }
        else if (dynamic_cast<SizeModuleCS*>(&module)) {
            moduleName = "Size Module";
        }
        else if (dynamic_cast<MeshSpawnModuleCS*>(&module)) {
            moduleName = "Mesh Spawn Module";
        }

        bool isSelected = (m_selectedModuleForEdit == moduleIndex);
        if (ImGui::Selectable((moduleName + "##" + std::to_string(moduleIndex)).c_str(), isSelected)) {
            m_selectedModuleForEdit = isSelected ? -1 : moduleIndex;
            m_selectedRenderForEdit = -1; // ��� ���ý� ���� ���� ����
        }

        ImGui::PopID();
        moduleIndex++;
    }

    ImGui::Separator();

    // ���� ��� ����Ʈ ǥ��
    ImGui::Text("Render Modules:");
    auto& renderList = m_modifyingSystem->GetRenderModules();
    int renderIndex = 0;

    for (auto it = renderList.begin(); it != renderList.end(); ++it) {
        RenderModules& render = **it;

        ImGui::PushID(renderIndex + 1000); // ID �浹 ����

        std::string renderName = "Unknown Render";
        if (dynamic_cast<BillboardModuleGPU*>(&render)) {
            renderName = "Billboard Render";
        }
        else if (dynamic_cast<MeshModuleGPU*>(&render)) {
            renderName = "Mesh Render";
        }

        bool isSelected = (m_selectedRenderForEdit == renderIndex);
        if (ImGui::Selectable((renderName + "##" + std::to_string(renderIndex)).c_str(), isSelected)) {
            m_selectedRenderForEdit = isSelected ? -1 : renderIndex;
            m_selectedModuleForEdit = -1; // ���� ���ý� ��� ���� ����
        }

        ImGui::PopID();
        renderIndex++;
    }

    ImGui::Separator();

    // ���õ� ����� ���� ���� UI
    if (m_selectedModuleForEdit >= 0) {
        RenderModuleDetailEditor();
    }

    ImGui::Separator();

    // ����/��� ��ư
    if (ImGui::Button("Save Changes")) {
        SaveModifiedEmitter();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        CancelModifyEmitter();
    }
}

void EffectEditor::StartModifyEmitter(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_isModifyingEmitter = true;
        m_modifyingEmitterIndex = index;
        m_modifyingSystem = m_tempEmitters[index].particleSystem;
        m_selectedModuleForEdit = -1;
        m_selectedRenderForEdit = -1;
    }
}

void EffectEditor::SaveModifiedEmitter()
{
    // ���������� �̹� �ǽð����� ����ǹǷ� ���¸� �ʱ�ȭ
    m_isModifyingEmitter = false;
    m_modifyingEmitterIndex = -1;
    m_modifyingSystem = nullptr;
    m_selectedModuleForEdit = -1;
    m_selectedRenderForEdit = -1;
}

void EffectEditor::CancelModifyEmitter()
{
    // TODO: ���� ���·� �����ϴ� ������ �ʿ��ϴٸ� ���⿡ ����
    m_isModifyingEmitter = false;
    m_modifyingEmitterIndex = -1;
    m_modifyingSystem = nullptr;
    m_selectedModuleForEdit = -1;
    m_selectedRenderForEdit = -1;
}

void EffectEditor::RenderEmitterEditor()
{
    if (!m_editingEmitter) return;

    ImGui::Text("Creating New Emitter");
    ImGui::Separator();

    // ��� ���� �� �߰�
    ImGui::Text("Add Modules:");
    const char* currentModuleName = m_selectedModuleIndex < m_availableModules.size() ?
        m_availableModules[m_selectedModuleIndex].name : "None";

    if (ImGui::BeginCombo("Select Module", currentModuleName)) {
        for (size_t i = 0; i < m_availableModules.size(); i++) {
            bool isSelected = (m_selectedModuleIndex == i);
            if (ImGui::Selectable(m_availableModules[i].name, isSelected)) {
                m_selectedModuleIndex = static_cast<int>(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Add Module")) {
        AddSelectedModule();
    }

    ImGui::Separator();

    // ���� ��� ���� �� �߰�
    ImGui::Text("Add Render Module:");
    const char* currentRenderName = m_selectedRenderIndex < m_availableRenders.size() ?
        m_availableRenders[m_selectedRenderIndex].name : "None";

    if (ImGui::BeginCombo("Select Render", currentRenderName)) {
        for (size_t i = 0; i < m_availableRenders.size(); i++) {
            bool isSelected = (m_selectedRenderIndex == i);
            if (ImGui::Selectable(m_availableRenders[i].name, isSelected)) {
                m_selectedRenderIndex = static_cast<int>(i);
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    if (ImGui::Button("Add Render Module")) {
        AddSelectedRender();
    }

    ImGui::Separator();

    // ���� ���� ǥ��
    RenderExistingModules();

    ImGui::Separator();

    // ����/��� ��ư
    if (ImGui::Button("Save Emitter")) {
        SaveCurrentEmitter();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        CancelEditing();
    }
}

void EffectEditor::StartCreateEmitter()
{
    m_editingEmitter = std::make_shared<ParticleSystem>(1000, ParticleDataType::Standard);
    m_isEditingEmitter = true;
}

void EffectEditor::SaveCurrentEmitter()
{
    if (m_editingEmitter) {
        TempEmitterInfo newEmitter;
        newEmitter.particleSystem = m_editingEmitter;
        newEmitter.name = "Emitter_" + std::to_string(m_tempEmitters.size() + 1);
        newEmitter.isPlaying = false;

        m_tempEmitters.push_back(newEmitter);
        m_editingEmitter = nullptr;
        m_isEditingEmitter = false;
    }
}

void EffectEditor::CancelEditing()
{
    m_editingEmitter = nullptr;
    m_isEditingEmitter = false;
}

void EffectEditor::RenderExistingModules()
{
    if (!m_editingEmitter) return;

    if (ImGui::Button("Show Module List")) {
        ImGui::OpenPopup("Module List");
    }

    if (ImGui::BeginPopup("Module List")) {
        ImGui::Text("Current Particle Modules");
        ImGui::Separator();
        int index = 0;

        auto& moduleList = m_editingEmitter->GetModuleList();
        for (auto it = moduleList.begin(); it != moduleList.end(); ++it) {
            ParticleModule& module = *it;
            ImGui::Text("Module %d: %s", index++, typeid(module).name());
            ImGui::Indent();
            ImGui::Text("Easing: %s", module.IsEasingEnabled() ? "Enabled" : "Disabled");

            // ��⺰ ���� ���� UI
            if (auto* spawnModule = dynamic_cast<SpawnModuleCS*>(&module)) {
                ImGui::Text("Spawn Rate: %.2f", spawnModule->GetSpawnRate());
            }
            else if (auto* movementModule = dynamic_cast<MovementModuleCS*>(&module)) {
                ImGui::Text("Use Gravity: %s", movementModule->GetUseGravity() ? "Yes" : "No");
            }
            else if (auto* colorModule = dynamic_cast<ColorModuleCS*>(&module)) {
                ImGui::Text("Color Module Settings");
            }
            else if (auto* sizeModule = dynamic_cast<SizeModuleCS*>(&module)) {
                ImGui::Text("Size Module Settings");
            }
            else if (auto* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(&module)) {
                ImGui::Text("Mesh Spawn Module Settings");
            }

            ImGui::Unindent();
            ImGui::Separator();
        }

        ImGui::Text("Total modules: %d", index);

        // ���� ���� ǥ��
        ImGui::Separator();
        ImGui::Text("Render Modules");

        auto& renderList = m_editingEmitter->GetRenderModules();
        int renderIndex = 0;

        for (auto it = renderList.begin(); it != renderList.end(); ++it) {
            RenderModules& render = **it;
            ImGui::Text("Render %d: %s", renderIndex++, typeid(render).name());
            ImGui::Indent();

            // ���� Ÿ�Ժ� ���� ���� ǥ��
            if (auto* billboardRender = dynamic_cast<BillboardModuleGPU*>(&render)) {
                ImGui::Text("Type: Billboard");
            }
            else if (auto* meshRender = dynamic_cast<MeshModuleGPU*>(&render)) {
                ImGui::Text("Type: Mesh");
            }

            ImGui::Unindent();
            ImGui::Separator();
        }

        ImGui::Text("Total renders: %d", renderIndex);
        ImGui::EndPopup();
    }
}

void EffectEditor::AddSelectedModule()
{
    if (!m_editingEmitter || m_selectedModuleIndex >= m_availableModules.size()) return;

    EffectModuleType type = m_availableModules[m_selectedModuleIndex].type;

    switch (type) {
    case EffectModuleType::SpawnModule:
        m_editingEmitter->AddModule<SpawnModuleCS>();
        break;
    case EffectModuleType::MeshSpawnModule:
        m_editingEmitter->AddModule<MeshSpawnModuleCS>();
        break;
    case EffectModuleType::MovementModule:
        m_editingEmitter->AddModule<MovementModuleCS>();
        break;
    case EffectModuleType::ColorModule:
        m_editingEmitter->AddModule<ColorModuleCS>();
        break;
    case EffectModuleType::SizeModule:
        m_editingEmitter->AddModule<SizeModuleCS>();
        break;
    }
}

void EffectEditor::AddSelectedRender()
{
    if (!m_editingEmitter || m_selectedRenderIndex >= m_availableRenders.size()) return;

    RenderType type = m_availableRenders[m_selectedRenderIndex].type;

    switch (type) {
    case RenderType::Billboard:
        m_editingEmitter->AddRenderModule<BillboardModuleGPU>();
        break;
    case RenderType::Mesh:
        m_editingEmitter->AddRenderModule<MeshModuleGPU>();
        break;
    }
}

void EffectEditor::RenderSpawnModuleEditor(SpawnModuleCS* spawnModule)
{
    if (!spawnModule) return;

    ImGui::Text("Spawn Module Settings");

    // ���� ����Ʈ ����
    float spawnRate = spawnModule->GetSpawnRate();
    if (ImGui::DragFloat("Spawn Rate", &spawnRate, 0.1f, 0.0f, 1000.0f)) {
        spawnModule->SetSpawnRate(spawnRate);
    }

    // ������ Ÿ�� ����
    EmitterType currentType = spawnModule->GetEmitterType();
    int typeIndex = static_cast<int>(currentType);
    const char* emitterTypes[] = { "Point", "Box", "Sphere", "Circle", "Cone"};

    if (ImGui::Combo("Emitter Type", &typeIndex, emitterTypes, IM_ARRAYSIZE(emitterTypes))) {
        spawnModule->SetEmitterType(static_cast<EmitterType>(typeIndex));
    }

    // ������ ũ��/������ ����
    static float emitterSize[3] = { 1.0f, 1.0f, 1.0f };
    static float emitterRadius = 1.0f;

    if (currentType == EmitterType::box) {
        if (ImGui::DragFloat3("Emitter Size", emitterSize, 0.1f, 0.1f, 100.0f)) {
            spawnModule->SetEmitterSize(XMFLOAT3(emitterSize[0], emitterSize[1], emitterSize[2]));
        }
    }
    else if (currentType == EmitterType::cone) {
        if (ImGui::DragFloat("Cone Radius", &emitterRadius, 0.1f, 0.1f, 100.0f)) {
            spawnModule->SetEmitterRadius(emitterRadius);
        }
        // ���Ŀ� �߰�?
        //if (ImGui::DragFloat("Cone Height", &coneHeight, 0.1f, 0.1f, 100.0f)) {
        //    spawnModule->SetConeHeight(coneHeight);  // �̷� �޼��尡 �ִٸ�
        //}
    }
    else if (currentType == EmitterType::sphere || currentType == EmitterType::circle) {
        if (ImGui::DragFloat("Emitter Radius", &emitterRadius, 0.1f, 0.1f, 100.0f)) {
            spawnModule->SetEmitterRadius(emitterRadius);
        }
    }

    ImGui::Separator();
    ImGui::Text("Particle Template Settings");

    // ��ƼŬ ���ø� ����
    ParticleTemplateParams currentTemplate = spawnModule->GetTemplate();

    // ������Ÿ��
    if (ImGui::DragFloat("Life Time", &currentTemplate.lifeTime, 0.1f, 0.1f, 100.0f)) {
        spawnModule->SetParticleLifeTime(currentTemplate.lifeTime);
    }

    // ũ��
    float size[2] = { currentTemplate.size.x, currentTemplate.size.y };
    if (ImGui::DragFloat2("Size", size, 0.01f, 0.01f, 10.0f)) {
        spawnModule->SetParticleSize(XMFLOAT2(size[0], size[1]));
    }

    // ����
    float color[4] = { currentTemplate.color.x, currentTemplate.color.y, currentTemplate.color.z, currentTemplate.color.w };
    if (ImGui::ColorEdit4("Color", color)) {
        spawnModule->SetParticleColor(XMFLOAT4(color[0], color[1], color[2], color[3]));
    }

    // �ӵ�
    float velocity[3] = { currentTemplate.velocity.x, currentTemplate.velocity.y, currentTemplate.velocity.z };
    if (ImGui::DragFloat3("Velocity", velocity, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetParticleVelocity(XMFLOAT3(velocity[0], velocity[1], velocity[2]));
    }

    // ���ӵ�
    float acceleration[3] = { currentTemplate.acceleration.x, currentTemplate.acceleration.y, currentTemplate.acceleration.z };
    if (ImGui::DragFloat3("Acceleration", acceleration, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetParticleAcceleration(XMFLOAT3(acceleration[0], acceleration[1], acceleration[2]));
    }

    // �ӵ� ����
    float minVertical = currentTemplate.minVerticalVelocity;
    float maxVertical = currentTemplate.maxVerticalVelocity;
    float horizontalRange = currentTemplate.horizontalVelocityRange;

    if (ImGui::DragFloat("Min Vertical Velocity", &minVertical, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }
    if (ImGui::DragFloat("Max Vertical Velocity", &maxVertical, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }
    if (ImGui::DragFloat("Horizontal Velocity Range", &horizontalRange, 0.1f, 0.0f, 100.0f)) {
        spawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }

    // ȸ�� �ӵ�
    if (ImGui::DragFloat("Rotate Speed", &currentTemplate.rotateSpeed, 0.1f, -360.0f, 360.0f)) {
        spawnModule->SetRotateSpeed(currentTemplate.rotateSpeed);
    }
}

void EffectEditor::RenderMovementModuleEditor(MovementModuleCS* movementModule)
{
}

void EffectEditor::RenderColorModuleEditor(ColorModuleCS* colorModule)
{
}

void EffectEditor::RenderSizeModuleEditor(SizeModuleCS* sizeModule)
{
}