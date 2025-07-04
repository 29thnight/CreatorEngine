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

   // 현재 편집 중인 에미터 업데이트
   if (m_editingEmitter) {
       m_editingEmitter->Update(delta);
   }
}

void EffectEditor::Render(RenderScene& scene, Camera& camera)
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

    // 임시 에미터들을 실제 ParticleSystem 벡터로 변환
    std::vector<std::shared_ptr<ParticleSystem>> emittersToExport;
    for (const auto& tempEmitter : m_tempEmitters) {
        if (tempEmitter.particleSystem) {
            emittersToExport.push_back(tempEmitter.particleSystem);
        }
    }

    // EffectManager에 등록
    if (auto* manager = efm.get()) {
        manager->RegisterCustomEffect(effectName, emittersToExport);

        // 등록된 Effect를 바로 재생
        /*if (auto* registeredEffect = manager->GetEffect(effectName)) {
            registeredEffect->Play();
        }*/

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
            // 모든 렌더 모듈에 텍스처 설정
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

    // 인덱스로 모듈 찾기
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

    // 각 모듈 타입별로 세부 설정 UI 렌더링
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
    else if (auto* meshSpawnModule = dynamic_cast<MeshSpawnModuleCS*>(targetModule)) {
        RenderMeshSpawnModuleEditor(meshSpawnModule);
    }
}

void EffectEditor::RenderRenderModuleDetailEditor()
{
    if (!m_modifyingSystem || m_selectedRenderForEdit < 0) return;

    auto& renderList = m_modifyingSystem->GetRenderModules();

    if (m_selectedRenderForEdit >= renderList.size()) return;

    auto renderModule = renderList[m_selectedRenderForEdit];

    ImGui::Text("Render Module Settings:");
    ImGui::Separator();

    if (auto* billboardRender = dynamic_cast<BillboardModuleGPU*>(renderModule)) {
        RenderBillboardModuleGPUEditor(billboardRender);
    }
    else if (auto* meshRender = dynamic_cast<MeshModuleGPU*>(renderModule)) {
        RenderMeshModuleGPUEditor(meshRender);
    }
}

void EffectEditor::RenderMainEditor()
{
    if (!m_isEditingEmitter && !m_isModifyingEmitter) {
        // 창을 상하로 분할
        ImVec2 windowSize = ImGui::GetContentRegionAvail();

        // 상단 영역: 에미터 생성 및 내보내기
        if (ImGui::BeginChild("CreateSection", ImVec2(0, windowSize.y * 0.3f), true)) {
            // 드래그 드롭 타겟 추가
            RenderTextureDragDropTarget();

            if (ImGui::Button("Create New Emitter")) {
                StartCreateEmitter();
            }

            if (!m_tempEmitters.empty()) {
                ImGui::Separator();
                ImGui::Text("Export to Game");

                static char effectName[256] = "MyCustomEffect";
                ImGui::InputText("Effect Name", effectName, sizeof(effectName));

                if (ImGui::Button("Export to Game")) {
                    ExportToManager(std::string(effectName));
                }

                // json 저장
                ImGui::Separator();
                RenderJsonSaveLoadUI();
            }
        }
        ImGui::EndChild();

        // 하단 영역: 에미터 관리 및 미리보기
        if (ImGui::BeginChild("ManageSection", ImVec2(0, 0), true)) {
            // 드래그 드롭 타겟 추가
            RenderTextureDragDropTarget();

            RenderPreviewControls();

            if (!m_tempEmitters.empty()) {
                ImGui::Text("Preview Emitters (%zu)", m_tempEmitters.size());
                ImGui::Separator();

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

                        // 현재 선택된 텍스처의 이름 표시
                        std::string currentTextureName = (selectedTextureIndex >= 0 && selectedTextureIndex < m_textures.size())
                            ? m_textures[selectedTextureIndex]->m_name
                            : "None";

                        if (ImGui::BeginCombo(comboLabel.c_str(), currentTextureName.c_str())) {
                            for (int t = 0; t < m_textures.size(); ++t) {
                                bool isSelected = (selectedTextureIndex == t);
                                std::string textureLabel = m_textures[t]->m_name;

                                // 텍스처 이름이 비어있으면 기본 이름 사용
                                if (textureLabel.empty()) {
                                    textureLabel = "Texture " + std::to_string(t);
                                }

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

                    // 컨트롤 버튼들
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

                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
    }
    else if (m_isEditingEmitter) {
        // 에미터 편집 모드에서도 드래그 드롭 적용
        RenderTextureDragDropTarget();
        RenderEmitterEditor();
    }
    else if (m_isModifyingEmitter) {
        // 에미터 수정 모드에서도 드래그 드롭 적용
        RenderTextureDragDropTarget();
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

    // 이름 변경 입력 필드 추가
    if (!m_emitterNameInitialized) {
        strcpy_s(m_newEmitterName, sizeof(m_newEmitterName), m_tempEmitters[m_modifyingEmitterIndex].name.c_str());
        m_emitterNameInitialized = true;
    }

    ImGui::InputText("Emitter Name", m_newEmitterName, sizeof(m_newEmitterName));
    ImGui::Separator();

    // 모듈 리스트 표시
    ImGui::Text("Particle Modules:");
    auto& moduleList = m_modifyingSystem->GetModuleList();
    int moduleIndex = 0;

    for (auto it = moduleList.begin(); it != moduleList.end(); ++it) {
        ParticleModule& module = *it;
        ImGui::PushID(moduleIndex);

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
            m_selectedRenderForEdit = -1; // 다른 모듈 선택시 렌더 선택 해제
        }

        ImGui::PopID();
        moduleIndex++;
    }

    ImGui::Separator();

    // 렌더 모듈 리스트 표시
    ImGui::Text("Render Modules:");
    auto& renderList = m_modifyingSystem->GetRenderModules();
    int renderIndex = 0;

    for (auto it = renderList.begin(); it != renderList.end(); ++it) {
        RenderModules& render = **it;
        ImGui::PushID(renderIndex + 1000);

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
            m_selectedModuleForEdit = -1; // 다른 렌더 선택시 모듈 선택 해제
        }

        ImGui::PopID();
        renderIndex++;
    }

    ImGui::Separator();

    // 선택된 모듈의 세부 설정 UI (ParticleModule)
    if (m_selectedModuleForEdit >= 0) {
        RenderModuleDetailEditor();
    }

    // 선택된 렌더 모듈의 세부 설정 UI (RenderModules)
    if (m_selectedRenderForEdit >= 0) {
        RenderRenderModuleDetailEditor();
    }

    ImGui::Separator();

    // 저장/취소 버튼
    if (ImGui::Button("Save Changes")) {
        SaveModifiedEmitter(std::string(m_newEmitterName));
        m_emitterNameInitialized = false;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        CancelModifyEmitter();
        m_emitterNameInitialized = false;
    }
}

void EffectEditor::RenderTextureDragDropTarget()
{
    // 현재 창의 전체 영역을 드래그 드롭 타겟으로 설정
    ImVec2 availSize = ImGui::GetContentRegionAvail();
    ImVec2 windowPos = ImGui::GetCursorScreenPos();
    ImRect dropRect(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y));

    if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("EffectEditorDropTarget")))
    {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Texture"))
        {
            const char* droppedFilePath = (const char*)payload->Data;
            file::path filename = droppedFilePath;
            file::path filepath = PathFinder::Relative("UI\\") / filename.filename();

            // 텍스처 로드
            Texture* texture = DataSystems->LoadTexture(filepath.string().c_str());

            if (texture)
            {
                // 텍스처에 파일명 설정 (확장자 제거)
                std::string textureName = filename.stem().string();
                texture->m_name = textureName;

                // 이름으로 중복 체크
                bool isDuplicate = false;
                for (const auto& existingTexture : m_textures)
                {
                    if (existingTexture->m_name == textureName)
                    {
                        isDuplicate = true;
                        break;
                    }
                }

                if (!isDuplicate)
                {
                    m_textures.push_back(texture);
                    std::cout << "Texture added to EffectEditor: " << textureName << std::endl;
                }
                else
                {
                    std::cout << "Texture already exists in list: " << textureName << std::endl;
                }
            }
            else
            {
                std::cout << "Failed to load texture: " << filepath.string() << std::endl;
            }
        }
        ImGui::EndDragDropTarget();
    }
}

void EffectEditor::RenderJsonSaveLoadUI()
{
    ImGui::Text("JSON Save/Load");

    // 저장 버튼
    if (ImGui::Button("Save as JSON")) {
        m_showSaveDialog = true;
    }

    // 로드 버튼
    ImGui::SameLine();
    if (ImGui::Button("Load from JSON")) {
        m_showLoadDialog = true;
    }

    // 저장 다이얼로그
    if (m_showSaveDialog) {
        ImGui::OpenPopup("Save Effect as JSON");
        m_showSaveDialog = false;
    }

    if (ImGui::BeginPopupModal("Save Effect as JSON", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Save current effect to JSON file");
        ImGui::Separator();

        ImGui::InputText("Filename", m_saveFileName, sizeof(m_saveFileName));

        if (ImGui::Button("Save")) {
            SaveEffectToJson(std::string(m_saveFileName));
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }

    // 로드 다이얼로그
    if (m_showLoadDialog) {
        ImGui::OpenPopup("Load Effect from JSON");
        m_showLoadDialog = false;
    }

    if (ImGui::BeginPopupModal("Load Effect from JSON", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Load effect from JSON file");
        ImGui::Separator();

        ImGui::InputText("Filename", m_loadFileName, sizeof(m_loadFileName));

        if (ImGui::Button("Load")) {
            LoadEffectFromJson(std::string(m_loadFileName));
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
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
        m_emitterNameInitialized = false;
    }
}

void EffectEditor::SaveModifiedEmitter(const std::string& name)
{
    // 이름이 비어있지 않다면 새 이름으로 업데이트
    if (!name.empty()) {
        m_tempEmitters[m_modifyingEmitterIndex].name = name;
    }

    // 수정사항은 이미 실시간으로 적용되므로 상태만 초기화
    m_isModifyingEmitter = false;
    m_modifyingEmitterIndex = -1;
    m_modifyingSystem = nullptr;
    m_selectedModuleForEdit = -1;
    m_selectedRenderForEdit = -1;
}

void EffectEditor::CancelModifyEmitter()
{
    // TODO: 원본 상태로 복원하는 로직이 필요하다면 여기에 구현
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

    // 에미터 이름 입력 필드 추가
    if (!m_emitterNameInitialized) {
        strcpy_s(m_newEmitterName, sizeof(m_newEmitterName), "NewEmitter");
        m_emitterNameInitialized = true;
    }

    ImGui::InputText("Emitter Name", m_newEmitterName, sizeof(m_newEmitterName));
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

    // 저장/취소 버튼 - 이름을 매개변수로 전달
    if (ImGui::Button("Save Emitter")) {
        SaveCurrentEmitter(std::string(m_newEmitterName));
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

void EffectEditor::SaveCurrentEmitter(const std::string& name)
{
    if (m_editingEmitter) {
        TempEmitterInfo newEmitter;
        newEmitter.particleSystem = m_editingEmitter;
        newEmitter.name = name.empty() ? ("Emitter_" + std::to_string(m_tempEmitters.size() + 1)) : name;
        newEmitter.isPlaying = false;

        m_tempEmitters.push_back(newEmitter);
        m_editingEmitter = nullptr;
        m_isEditingEmitter = false;
        m_emitterNameInitialized = false;
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
        m_editingEmitter->SetParticleDatatype(ParticleDataType::Standard);
        m_editingEmitter->AddRenderModule<BillboardModuleGPU>();
        break;
    case RenderType::Mesh:
        m_editingEmitter->SetParticleDatatype(ParticleDataType::Mesh);
        m_editingEmitter->AddRenderModule<MeshModuleGPU>();
        break;
    }
}

void EffectEditor::SaveEffectToJson(const std::string& filename)
{
    if (m_tempEmitters.empty()) {
        std::cout << "No emitters to save!" << std::endl;
        return;
    }

    try {
        // 임시 이펙트 생성
        auto tempEffect = std::make_unique<EffectBase>();
        tempEffect->SetName("EditorEffect");

        // 모든 에미터를 이펙트에 추가
        for (const auto& tempEmitter : m_tempEmitters) {
            if (tempEmitter.particleSystem) {
                tempEffect->AddParticleSystem(tempEmitter.particleSystem);
            }
        }

        // JSON으로 직렬화
        nlohmann::json effectJson = EffectSerializer::SerializeEffect(*tempEffect);

        // 파일로 저장
        std::ofstream file(filename);
        if (file.is_open()) {
            file << effectJson.dump(4); // 들여쓰기로 보기 좋게
            file.close();
            std::cout << "Effect saved to: " << filename << std::endl;
        }
        else {
            std::cerr << "Failed to open file for writing: " << filename << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cerr << "Error saving effect: " << e.what() << std::endl;
    }
}

void EffectEditor::LoadEffectFromJson(const std::string& filename)
{
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        nlohmann::json effectJson;
        file >> effectJson;
        file.close();

        // JSON에서 이펙트 복원
        auto loadedEffect = EffectSerializer::DeserializeEffect(effectJson);

        if (loadedEffect) {
            // 기존 임시 에미터들 클리어
            m_tempEmitters.clear();

            // 로드된 이펙트의 ParticleSystem들을 임시 에미터로 변환
            const auto& particleSystems = loadedEffect->GetAllParticleSystems();

            for (size_t i = 0; i < particleSystems.size(); ++i) {
                TempEmitterInfo tempEmitter;
                tempEmitter.particleSystem = particleSystems[i];
                tempEmitter.name = "LoadedEmitter_" + std::to_string(i + 1);
                tempEmitter.isPlaying = false;

                m_tempEmitters.push_back(tempEmitter);
            }

            std::cout << "Effect loaded from: " << filename << std::endl;
            std::cout << "Loaded " << particleSystems.size() << " particle systems" << std::endl;

        }
        else {
            std::cerr << "Failed to deserialize effect from JSON" << std::endl;
        }

    }
    catch (const std::exception& e) {
        std::cerr << "Error loading effect: " << e.what() << std::endl;
    }
}

void EffectEditor::RenderSpawnModuleEditor(SpawnModuleCS* spawnModule)
{
    if (!spawnModule) return;

    ImGui::Text("Spawn Module Settings");

    // 스폰 레이트 설정
    float spawnRate = spawnModule->GetSpawnRate();
    if (ImGui::DragFloat("Spawn Rate", &spawnRate, 0.1f, 0.0f, 1000.0f)) {
        spawnModule->SetSpawnRate(spawnRate);
    }

    // 에미터 타입 설정
    EmitterType currentType = spawnModule->GetEmitterType();
    int typeIndex = static_cast<int>(currentType);
    const char* emitterTypes[] = { "Point", "Sphere", "Box", "Cone", "Circle"};

    if (ImGui::Combo("Emitter Type", &typeIndex, emitterTypes, IM_ARRAYSIZE(emitterTypes))) {
        spawnModule->SetEmitterType(static_cast<EmitterType>(typeIndex));
    }

    // 에미터 크기/반지름 설정
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
        // 추후에 추가?
        //if (ImGui::DragFloat("Cone Height", &coneHeight, 0.1f, 0.1f, 100.0f)) {
        //    spawnModule->SetConeHeight(coneHeight);  // 이런 메서드가 있다면
        //}
    }
    else if (currentType == EmitterType::sphere || currentType == EmitterType::circle) {
        if (ImGui::DragFloat("Emitter Radius", &emitterRadius, 0.1f, 0.1f, 100.0f)) {
            spawnModule->SetEmitterRadius(emitterRadius);
        }
    }

    ImGui::Separator();
    ImGui::Text("Particle Template Settings");

    // 파티클 템플릿 설정
    ParticleTemplateParams currentTemplate = spawnModule->GetTemplate();

    // 라이프타임
    if (ImGui::DragFloat("Life Time", &currentTemplate.lifeTime, 0.1f, 0.1f, 100.0f)) {
        spawnModule->SetParticleLifeTime(currentTemplate.lifeTime);
    }

    // 크기
    float size[2] = { currentTemplate.size.x, currentTemplate.size.y };
    if (ImGui::DragFloat2("Size", size, 0.01f, 0.01f, 10.0f)) {
        spawnModule->SetParticleSize(XMFLOAT2(size[0], size[1]));
    }

    // 색상
    float color[4] = { currentTemplate.color.x, currentTemplate.color.y, currentTemplate.color.z, currentTemplate.color.w };
    if (ImGui::ColorEdit4("Color", color)) {
        spawnModule->SetParticleColor(XMFLOAT4(color[0], color[1], color[2], color[3]));
    }

    // 속도
    float velocity[3] = { currentTemplate.velocity.x, currentTemplate.velocity.y, currentTemplate.velocity.z };
    if (ImGui::DragFloat3("Velocity", velocity, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetParticleVelocity(XMFLOAT3(velocity[0], velocity[1], velocity[2]));
    }

    // 가속도
    float acceleration[3] = { currentTemplate.acceleration.x, currentTemplate.acceleration.y, currentTemplate.acceleration.z };
    if (ImGui::DragFloat3("Acceleration", acceleration, 0.1f, -100.0f, 100.0f)) {
        spawnModule->SetParticleAcceleration(XMFLOAT3(acceleration[0], acceleration[1], acceleration[2]));
    }

    // 속도 범위
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

    // 회전 속도
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

void EffectEditor::RenderBillboardModuleGPUEditor(BillboardModuleGPU* billboardModule)
{
    if (!billboardModule) return;

    ImGui::Text("Billboard Render Module Settings");

    // 텍스처 설정 UI
    ImGui::Text("Texture Assignment:");

    if (!m_textures.empty()) {
        static int selectedTextureIndex = 0;

        std::string currentTextureName = (selectedTextureIndex >= 0 && selectedTextureIndex < m_textures.size())
            ? m_textures[selectedTextureIndex]->m_name
            : "None";

        if (ImGui::BeginCombo("Select Texture", currentTextureName.c_str())) {
            for (int t = 0; t < m_textures.size(); ++t) {
                bool isSelected = (selectedTextureIndex == t);
                std::string textureLabel = m_textures[t]->m_name;

                if (textureLabel.empty()) {
                    textureLabel = "Texture " + std::to_string(t);
                }

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
        if (ImGui::Button("Assign Texture")) {
            if (selectedTextureIndex >= 0 && selectedTextureIndex < m_textures.size()) {
                billboardModule->SetTexture(m_textures[selectedTextureIndex]);
            }
        }
    }
    else {
        ImGui::Text("No textures available");
        ImGui::Text("Drag and drop texture files to add them");
    }

    ImGui::Separator();

    // 빌보드 관련 추가 설정들이 있다면 여기에 추가
    ImGui::Text("Additional billboard settings can be added here");
}

void EffectEditor::RenderMeshSpawnModuleEditor(MeshSpawnModuleCS* meshSpawnModule)
{
    if (!meshSpawnModule) return;

    ImGui::Text("Mesh Spawn Module Settings");

    // 스폰 레이트 설정
    float spawnRate = meshSpawnModule->GetSpawnRate();
    if (ImGui::DragFloat("Spawn Rate", &spawnRate, 0.1f, 0.0f, 1000.0f)) {
        meshSpawnModule->SetSpawnRate(spawnRate);
    }

    // 에미터 타입 설정
    EmitterType currentType = meshSpawnModule->GetEmitterType();
    int typeIndex = static_cast<int>(currentType);
    const char* emitterTypes[] = { "Point", "Sphere", "Box", "Cone", "Circle" };

    if (ImGui::Combo("Emitter Type", &typeIndex, emitterTypes, IM_ARRAYSIZE(emitterTypes))) {
        meshSpawnModule->SetEmitterType(static_cast<EmitterType>(typeIndex));
    }

    // 에미터 크기/반지름 설정
    static float emitterSize[3] = { 1.0f, 1.0f, 1.0f };
    static float emitterRadius = 1.0f;

    if (currentType == EmitterType::box) {
        if (ImGui::DragFloat3("Emitter Size", emitterSize, 0.1f, 0.1f, 100.0f)) {
            meshSpawnModule->SetEmitterSize(XMFLOAT3(emitterSize[0], emitterSize[1], emitterSize[2]));
        }
    }
    else if (currentType == EmitterType::cone) {
        if (ImGui::DragFloat("Cone Radius", &emitterRadius, 0.1f, 0.1f, 100.0f)) {
            meshSpawnModule->SetEmitterRadius(emitterRadius);
        }
    }
    else if (currentType == EmitterType::sphere || currentType == EmitterType::circle) {
        if (ImGui::DragFloat("Emitter Radius", &emitterRadius, 0.1f, 0.1f, 100.0f)) {
            meshSpawnModule->SetEmitterRadius(emitterRadius);
        }
    }

    ImGui::Separator();
    ImGui::Text("Mesh Particle Template Settings");

    // 메시 파티클 템플릿 설정 (3D 기반)
    MeshParticleTemplateParams currentTemplate = meshSpawnModule->GetTemplate();

    // 라이프타임
    if (ImGui::DragFloat("Life Time", &currentTemplate.lifeTime, 0.1f, 0.1f, 100.0f)) {
        meshSpawnModule->SetParticleLifeTime(currentTemplate.lifeTime);
    }

    // 3D 스케일 범위
    float minScale[3] = { currentTemplate.minScale.x, currentTemplate.minScale.y, currentTemplate.minScale.z };
    float maxScale[3] = { currentTemplate.maxScale.x, currentTemplate.maxScale.y, currentTemplate.maxScale.z };

    if (ImGui::DragFloat3("Min Scale", minScale, 0.01f, 0.01f, 10.0f)) {
        meshSpawnModule->SetParticleScaleRange(
            XMFLOAT3(minScale[0], minScale[1], minScale[2]),
            XMFLOAT3(maxScale[0], maxScale[1], maxScale[2])
        );
    }

    if (ImGui::DragFloat3("Max Scale", maxScale, 0.01f, 0.01f, 10.0f)) {
        meshSpawnModule->SetParticleScaleRange(
            XMFLOAT3(minScale[0], minScale[1], minScale[2]),
            XMFLOAT3(maxScale[0], maxScale[1], maxScale[2])
        );
    }

    // 3D 회전 속도 범위
    float minRotSpeed[3] = { currentTemplate.minRotationSpeed.x, currentTemplate.minRotationSpeed.y, currentTemplate.minRotationSpeed.z };
    float maxRotSpeed[3] = { currentTemplate.maxRotationSpeed.x, currentTemplate.maxRotationSpeed.y, currentTemplate.maxRotationSpeed.z };

    if (ImGui::DragFloat3("Min Rotation Speed", minRotSpeed, 0.1f, -360.0f, 360.0f)) {
        meshSpawnModule->SetParticleRotationSpeedRange(
            XMFLOAT3(minRotSpeed[0], minRotSpeed[1], minRotSpeed[2]),
            XMFLOAT3(maxRotSpeed[0], maxRotSpeed[1], maxRotSpeed[2])
        );
    }

    if (ImGui::DragFloat3("Max Rotation Speed", maxRotSpeed, 0.1f, -360.0f, 360.0f)) {
        meshSpawnModule->SetParticleRotationSpeedRange(
            XMFLOAT3(minRotSpeed[0], minRotSpeed[1], minRotSpeed[2]),
            XMFLOAT3(maxRotSpeed[0], maxRotSpeed[1], maxRotSpeed[2])
        );
    }

    // 3D 초기 회전 범위
    float minInitRot[3] = { currentTemplate.minInitialRotation.x, currentTemplate.minInitialRotation.y, currentTemplate.minInitialRotation.z };
    float maxInitRot[3] = { currentTemplate.maxInitialRotation.x, currentTemplate.maxInitialRotation.y, currentTemplate.maxInitialRotation.z };

    if (ImGui::DragFloat3("Min Initial Rotation", minInitRot, 0.1f, -360.0f, 360.0f)) {
        meshSpawnModule->SetParticleInitialRotationRange(
            XMFLOAT3(minInitRot[0], minInitRot[1], minInitRot[2]),
            XMFLOAT3(maxInitRot[0], maxInitRot[1], maxInitRot[2])
        );
    }

    if (ImGui::DragFloat3("Max Initial Rotation", maxInitRot, 0.1f, -360.0f, 360.0f)) {
        meshSpawnModule->SetParticleInitialRotationRange(
            XMFLOAT3(minInitRot[0], minInitRot[1], minInitRot[2]),
            XMFLOAT3(maxInitRot[0], maxInitRot[1], maxInitRot[2])
        );
    }

    // 색상
    float color[4] = { currentTemplate.color.x, currentTemplate.color.y, currentTemplate.color.z, currentTemplate.color.w };
    if (ImGui::ColorEdit4("Color", color)) {
        meshSpawnModule->SetParticleColor(XMFLOAT4(color[0], color[1], color[2], color[3]));
    }

    // 속도
    float velocity[3] = { currentTemplate.velocity.x, currentTemplate.velocity.y, currentTemplate.velocity.z };
    if (ImGui::DragFloat3("Velocity", velocity, 0.1f, -100.0f, 100.0f)) {
        meshSpawnModule->SetParticleVelocity(XMFLOAT3(velocity[0], velocity[1], velocity[2]));
    }

    // 가속도
    float acceleration[3] = { currentTemplate.acceleration.x, currentTemplate.acceleration.y, currentTemplate.acceleration.z };
    if (ImGui::DragFloat3("Acceleration", acceleration, 0.1f, -100.0f, 100.0f)) {
        meshSpawnModule->SetParticleAcceleration(XMFLOAT3(acceleration[0], acceleration[1], acceleration[2]));
    }

    // 속도 범위
    float minVertical = currentTemplate.minVerticalVelocity;
    float maxVertical = currentTemplate.maxVerticalVelocity;
    float horizontalRange = currentTemplate.horizontalVelocityRange;

    if (ImGui::DragFloat("Min Vertical Velocity", &minVertical, 0.1f, -100.0f, 100.0f)) {
        meshSpawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }
    if (ImGui::DragFloat("Max Vertical Velocity", &maxVertical, 0.1f, -100.0f, 100.0f)) {
        meshSpawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }
    if (ImGui::DragFloat("Horizontal Velocity Range", &horizontalRange, 0.1f, 0.0f, 100.0f)) {
        meshSpawnModule->SetVelocityRange(minVertical, maxVertical, horizontalRange);
    }
}

void EffectEditor::RenderMeshModuleGPUEditor(MeshModuleGPU* meshModule)
{
    if (!meshModule) return;

    ImGui::Text("Mesh Render Module Settings");

    // 메시 타입 설정
    MeshType currentMeshType = meshModule->GetMeshType();
    int meshTypeIndex = static_cast<int>(currentMeshType);
    const char* meshTypes[] = { "None", "Cube", "Sphere", "Custom" };

    if (ImGui::Combo("Mesh Type", &meshTypeIndex, meshTypes, IM_ARRAYSIZE(meshTypes))) {
        meshModule->SetMeshType(static_cast<MeshType>(meshTypeIndex));
    }

    // 카메라 위치 설정
    static float cameraPos[3] = { 0.0f, 0.0f, 0.0f };
    if (ImGui::DragFloat3("Camera Position", cameraPos, 0.1f)) {
        meshModule->SetCameraPosition(Mathf::Vector3(cameraPos[0], cameraPos[1], cameraPos[2]));
    }

    ImGui::Separator();

    // 텍스처 설정 UI
    ImGui::Text("Texture Assignment:");

    if (!m_textures.empty()) {
        static int selectedTextureIndex = 0;

        // 현재 선택된 텍스처의 이름 표시
        std::string currentTextureName = (selectedTextureIndex >= 0 && selectedTextureIndex < m_textures.size())
            ? m_textures[selectedTextureIndex]->m_name
            : "None";

        if (ImGui::BeginCombo("Select Texture", currentTextureName.c_str())) {
            for (int t = 0; t < m_textures.size(); ++t) {
                bool isSelected = (selectedTextureIndex == t);
                std::string textureLabel = m_textures[t]->m_name;

                if (textureLabel.empty()) {
                    textureLabel = "Texture " + std::to_string(t);
                }

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
        if (ImGui::Button("Assign Texture")) {
            if (selectedTextureIndex >= 0 && selectedTextureIndex < m_textures.size()) {
                meshModule->SetTexture(m_textures[selectedTextureIndex]);
            }
        }
    }
    else {
        ImGui::Text("No textures available");
        ImGui::Text("Drag and drop texture files to add them");
    }

    ImGui::Separator();

    // 외부 메시 로드 UI
    ImGui::Text("Custom Mesh Settings:");

    static char meshFilePath[256] = "";
    ImGui::InputText("Mesh File Path", meshFilePath, sizeof(meshFilePath));

    ImGui::SameLine();
    if (ImGui::Button("Load Mesh")) {
        // 외부 메시 로드 로직 (실제 구현은 프로젝트에 맞게 수정 필요)
        if (strlen(meshFilePath) > 0) {
            // 예시: Mesh* customMesh = MeshLoader::LoadMesh(meshFilePath);
            // if (customMesh) {
            //     meshModule->SetCustomMesh(customMesh);
            // }
            ImGui::Text("Mesh loading functionality needs to be implemented");
        }
    }

    // 파티클 데이터 설정 정보 표시
    ImGui::Separator();
    ImGui::Text("Particle Data Info:");

    UINT instanceCount = meshModule->GetInstanceCount();
    ImGui::Text("Instance Count: %u", instanceCount);

    if (instanceCount == 0) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No particle data assigned");
        ImGui::Text("This will be automatically set when connected to a particle system");
    }
}
