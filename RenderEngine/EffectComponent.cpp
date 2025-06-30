#include "EffectComponent.h"
#include "IconsFontAwesome6.h"
#include "EffectManager.h"

void EffectComponent::Awake()
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
}

void EffectComponent::Update(float tick)
{
    //// 미리보기용 임시 에미터들만 업데이트
    //for (auto& tempEmitter : m_tempEmitters) {
    //    if (tempEmitter.particleSystem && tempEmitter.isPlaying) {
    //        tempEmitter.particleSystem->Update(tick);
    //    }
    //}

    //// 현재 편집 중인 에미터 업데이트
    //if (m_editingEmitter) {
    //    m_editingEmitter->Update(tick);
    //}
}

void EffectComponent::OnDistroy()
{
    m_tempEmitters.clear();
    m_editingEmitter = nullptr;
}

void EffectComponent::Render(RenderScene& scene, Camera& camera)
{
    // 미리보기용 임시 에미터들만 렌더링
    for (auto& tempEmitter : m_tempEmitters) {
        if (tempEmitter.particleSystem && tempEmitter.isPlaying) {
            tempEmitter.particleSystem->Render(scene, camera);
        }
    }

    // 현재 편집 중인 에미터 렌더링
    if (m_editingEmitter) {
        m_editingEmitter->Render(scene, camera);
    }
}

void EffectComponent::PlayPreview()
{
    for (auto& tempEmitter : m_tempEmitters) {
        tempEmitter.isPlaying = true;
        if (tempEmitter.particleSystem) {
            tempEmitter.particleSystem->Play();
        }
    }
}

void EffectComponent::StopPreview()
{
    for (auto& tempEmitter : m_tempEmitters) {
        tempEmitter.isPlaying = false;
        if (tempEmitter.particleSystem) {
            tempEmitter.particleSystem->Stop();
        }
    }
}

void EffectComponent::PlayEmitterPreview(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters[index].isPlaying = true;
        if (m_tempEmitters[index].particleSystem) {
            m_tempEmitters[index].particleSystem->Play();
        }
    }
}

void EffectComponent::StopEmitterPreview(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters[index].isPlaying = false;
        if (m_tempEmitters[index].particleSystem) {
            m_tempEmitters[index].particleSystem->Stop();
        }
    }
}

void EffectComponent::RemoveEmitter(int index)
{
    if (index >= 0 && index < m_tempEmitters.size()) {
        m_tempEmitters.erase(m_tempEmitters.begin() + index);
    }
}

void EffectComponent::SetTexture(Texture* tex)
{
    m_textures.push_back(tex);
}

void EffectComponent::ExportToManager(const std::string& effectName)
{
    if (m_tempEmitters.empty()) {
        std::cout << "No emitters to export!" << std::endl;
        return;
    }

    // 임시 에미터들을 실제 ParticleSystem 벡터로 변환
    std::vector<std::shared_ptr<ParticleSystem>> emittersToExport;
    for (const auto& tempEmitter : m_tempEmitters) {
        if (tempEmitter.particleSystem) {
            emittersToExport.push_back(tempEmitter.particleSystem);
        }
    }

    // EffectManager에 등록
    if (auto* manager = EffectManager::GetCurrent()) {
        manager->RegisterCustomEffect(effectName, emittersToExport);

        // 등록된 Effect를 바로 재생
        if (auto* registeredEffect = manager->GetEffect(effectName)) {
            registeredEffect->Play();
        }

        std::cout << "Effect exported and started: " << effectName << std::endl;
    }
    else {
        std::cout << "No active EffectManager found!" << std::endl;
    }
}

void EffectComponent::AssignTextureToEmitter(int emitterIndex, int textureIndex)
{
    if (emitterIndex >= 0 && emitterIndex < m_tempEmitters.size() &&
        textureIndex >= 0 && textureIndex < m_textures.size()) {

        auto emitter = m_tempEmitters[emitterIndex].particleSystem;
        if (emitter) {
            // 모든 렌더 모듈에 텍스처 설정
            for (auto& renderModule : emitter->GetRenderModules()) {
                renderModule->SetTexture(m_textures[textureIndex]);
            }
        }
    }
}

void EffectComponent::RenderMainEditor()
{
    if (!m_isEditingEmitter) {
        if (ImGui::Button("Create New Emitter")) {
            StartCreateEmitter();
        }

        // 미리보기 컨트롤
        RenderPreviewControls();

        // Export 섹션
        if (!m_tempEmitters.empty()) {
            ImGui::Separator();
            ImGui::Text("Export to Game");

            static char effectName[256] = "MyCustomEffect";
            ImGui::InputText("Effect Name", effectName, sizeof(effectName));

            if (ImGui::Button("Export to Game")) {
                ExportToManager(std::string(effectName));
            }
        }

        // 임시 에미터들 표시
        if (!m_tempEmitters.empty()) {
            ImGui::Separator();
            ImGui::Text("Preview Emitters (%zu)", m_tempEmitters.size());

            for (int i = 0; i < m_tempEmitters.size(); ++i) {
                ImGui::PushID(i);

                ImGui::Text("Emitter %d: %s", i, m_tempEmitters[i].name.c_str());

                // 위치 설정
                if (m_tempEmitters[i].particleSystem) {
                    Mathf::Vector3 currentPos = m_tempEmitters[i].particleSystem->GetPosition();
                    float pos[3] = { currentPos.x, currentPos.y, currentPos.z };
                    if (ImGui::DragFloat3("Position", pos, 0.1f)) {
                        m_tempEmitters[i].particleSystem->SetPosition(Mathf::Vector3(pos[0], pos[1], pos[2]));
                    }
                }

                // 텍스처 할당 UI
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

                if (ImGui::Button("Play")) {
                    PlayEmitterPreview(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    StopEmitterPreview(i);
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove")) {
                    RemoveEmitter(i);
                }

                ImGui::PopID();
            }
        }
    }
    else {
        RenderEmitterEditor();
    }
}

void EffectComponent::RenderPreviewControls()
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

void EffectComponent::RenderEmitterEditor()
{
    if (!m_editingEmitter) return;

    ImGui::Text("Creating New Emitter");
    ImGui::Separator();

    // 모듈 선택 및 추가
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

    // 렌더 모듈 선택 및 추가
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

    // 기존 모듈들 표시
    RenderExistingModules();

    ImGui::Separator();

    // 저장/취소 버튼
    if (ImGui::Button("Save Emitter")) {
        SaveCurrentEmitter();
    }

    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        CancelEditing();
    }
}

void EffectComponent::StartCreateEmitter()
{
    m_editingEmitter = std::make_shared<ParticleSystem>(1000, ParticleDataType::Standard);
    m_isEditingEmitter = true;
}

void EffectComponent::SaveCurrentEmitter()
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

void EffectComponent::CancelEditing()
{
    m_editingEmitter = nullptr;
    m_isEditingEmitter = false;
}

void EffectComponent::RenderExistingModules()
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

            // 모듈별 세부 설정 UI
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

        // 렌더 모듈들 표시
        ImGui::Separator();
        ImGui::Text("Render Modules");

        auto& renderList = m_editingEmitter->GetRenderModules();
        int renderIndex = 0;

        for (auto it = renderList.begin(); it != renderList.end(); ++it) {
            RenderModules& render = **it;
            ImGui::Text("Render %d: %s", renderIndex++, typeid(render).name());
            ImGui::Indent();

            // 렌더 타입별 세부 정보 표시
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

void EffectComponent::AddSelectedModule()
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

void EffectComponent::AddSelectedRender()
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