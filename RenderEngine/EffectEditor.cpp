#include "EffectEditor.h"
#include "IconsFontAwesome6.h"
#include "EffectManager.h"
#include "EffectBase.h"
#include "ImGuiRegister.h"
#include "DataSystem.h"

EffectEditor::EffectEditor()
{
	// 이펙트 리스트 창 등록
	ImGui::ContextRegister("EffectList", false, [&]() {
		// 제목과 X 버튼을 같은 줄에
		ImGui::Text("All Effects:");
		ImGui::SameLine();
		ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 30);
		if (ImGui::SmallButton(ICON_FA_X))
		{
			ImGui::GetContext("EffectList").Close();
		}

		// 리스트박스
		if (ImGui::BeginListBox("##EffectList"))
		{
			for (const auto& pair : EffectManagers->GetEffectTemplates()) {
				if (ImGui::Selectable(pair.first.c_str())) {
					// 선택했을 때 처리할 로직
					std::cout << "Selected effect: " << pair.first << std::endl;
				}
			}
			ImGui::EndListBox();
		}
		});
	ImGui::GetContext("EffectList").Close();

	// 메인 에디터 창 등록
	ImGui::ContextRegister("EffectEdit", false, [&]() {
		if (ImGui::BeginMenuBar())
		{
			ImGui::Text("Effect Editor");
			if (ImGui::Button("Effect List"))
			{
				ImGui::GetContext("EffectList").Open(); // 리스트 창 열기
			}
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
		if (index < m_emitterTextureSelections.size()) {
			m_emitterTextureSelections.erase(m_emitterTextureSelections.begin() + index);
		}
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

	// 임시 EffectBase 생성
	auto tempEffect = std::make_unique<EffectBase>();
	tempEffect->SetName(effectName);
	tempEffect->SetTimeScale(m_effectTimeScale);
	tempEffect->SetLoop(m_effectLoop);
	tempEffect->SetDuration(m_effectDuration);

	for (const auto& tempEmitter : m_tempEmitters) {
		if (tempEmitter.particleSystem) {
			tempEmitter.particleSystem->m_name = tempEmitter.name;
			tempEffect->AddParticleSystem(tempEmitter.particleSystem);
		}
	}

	// JSON으로 직렬화
	nlohmann::json effectJson = EffectSerializer::SerializeEffect(*tempEffect);

	// 매니저에 템플릿으로 등록
	if (auto* manager = EffectManagers) {
		manager->RegisterTemplateFromEditor(effectName, effectJson);
		std::cout << "Effect template registered: " << effectName << std::endl;
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

void EffectEditor::RenderShaderSelectionUI(RenderModules* renderModule)
{
	if (!renderModule) return;

	ImGui::Separator();
	ImGui::Text("Shader Settings:");

	auto availableVS = RenderModules::GetAvailableVertexShaders();
	if (!availableVS.empty()) {
		std::string currentVS = renderModule->GetVertexShaderName();
		if (ImGui::BeginCombo("Vertex Shader", currentVS.c_str())) {
			for (const auto& shaderName : availableVS) {
				bool isSelected = (currentVS == shaderName);
				if (ImGui::Selectable(shaderName.c_str(), isSelected)) {
					renderModule->SetVertexShader(shaderName);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	auto availablePS = RenderModules::GetAvailablePixelShaders();
	if (!availablePS.empty()) {
		std::string currentPS = renderModule->GetPixelShaderName();
		if (ImGui::BeginCombo("Pixel Shader", currentPS.c_str())) {
			for (const auto& shaderName : availablePS) {
				bool isSelected = (currentPS == shaderName);
				if (ImGui::Selectable(shaderName.c_str(), isSelected)) {
					renderModule->SetPixelShader(shaderName);
				}
				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Separator();
}

void EffectEditor::RenderTextureSelectionUI(RenderModules* renderModule)
{
	if (!renderModule) return;

	ImGui::Separator();
	ImGui::Text("Texture Settings:");

	// 현재 텍스처 슬롯들 표시
	size_t textureCount = renderModule->GetTextureCount();
	for (size_t i = 0; i < std::max(textureCount, size_t(4)); ++i) {
		ImGui::PushID(i);

		std::string slotLabel = "Slot " + std::to_string(i);
		Texture* currentTexture = renderModule->GetTexture(i);
		std::string currentName = currentTexture ? currentTexture->m_name : "None";

		if (ImGui::BeginCombo(slotLabel.c_str(), currentName.c_str())) {
			// None 옵션
			if (ImGui::Selectable("None", currentTexture == nullptr)) {
				renderModule->RemoveTexture(i);
			}

			// 사용 가능한 텍스처들
			for (int t = 0; t < m_textures.size(); ++t) {
				bool isSelected = (currentTexture == m_textures[t]);
				std::string textureLabel = m_textures[t]->m_name;
				if (textureLabel.empty()) {
					textureLabel = "Texture " + std::to_string(t);
				}

				if (ImGui::Selectable(textureLabel.c_str(), isSelected)) {
					renderModule->SetTexture(i, m_textures[t]);
				}

				if (isSelected) {
					ImGui::SetItemDefaultFocus();
				}
			}
			ImGui::EndCombo();
		}

		ImGui::PopID();
	}

	if (ImGui::Button("Add Texture Slot")) {
		renderModule->EnsureTextureSlots(textureCount + 1);
	}

	ImGui::Separator();
}

void EffectEditor::RenderStateSelectionUI(RenderModules* renderModule)
{
	if (!renderModule) return;

	ImGui::Separator();
	ImGui::Text("Render State Settings:");

	// Blend State 설정
	const char* blendPresetNames[] = {
		"None", "Alpha", "Additive", "Multiply", "Subtractive", "Custom"
	};

	BlendPreset currentBlend = renderModule->GetBlendPreset();
	int currentBlendIndex = static_cast<int>(currentBlend);

	if (ImGui::Combo("Blend State", &currentBlendIndex, blendPresetNames, IM_ARRAYSIZE(blendPresetNames))) {
		renderModule->SetBlendPreset(static_cast<BlendPreset>(currentBlendIndex));
	}

	// Depth State 설정
	const char* depthPresetNames[] = {
		"None", "Default", "Read Only", "Write Only", "Disabled", "Custom"
	};

	DepthPreset currentDepth = renderModule->GetDepthPreset();
	int currentDepthIndex = static_cast<int>(currentDepth);

	if (ImGui::Combo("Depth State", &currentDepthIndex, depthPresetNames, IM_ARRAYSIZE(depthPresetNames))) {
		renderModule->SetDepthPreset(static_cast<DepthPreset>(currentDepthIndex));
	}

	// Rasterizer State 설정
	const char* rasterizerPresetNames[] = {
		"None", "Default", "No Cull", "Wireframe", "Wireframe No Cull", "Custom"
	};

	RasterizerPreset currentRasterizer = renderModule->GetRasterizerPreset();
	int currentRasterizerIndex = static_cast<int>(currentRasterizer);

	if (ImGui::Combo("Rasterizer State", &currentRasterizerIndex, rasterizerPresetNames, IM_ARRAYSIZE(rasterizerPresetNames))) {
		renderModule->SetRasterizerPreset(static_cast<RasterizerPreset>(currentRasterizerIndex));
	}

	ImGui::Separator();
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
	else if (auto* meshSpawnModule = dynamic_cast<MeshColorModuleCS*>(targetModule)) {
		RenderMeshColorModuleEditor(meshSpawnModule);
	}
	else if (auto* trailGenModule = dynamic_cast<TrailGenerateModule*>(targetModule)){
		RenderTrailGenerateModuleEditor(trailGenModule);
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
	else if (auto* trailRender = dynamic_cast<TrailRenderModule*>(renderModule))
	{
		RenderTrailRenderModuleEditor(trailRender);
	}
}

void EffectEditor::RenderMainEditor()
{
	if (!m_isEditingEmitter && !m_isModifyingEmitter) {
		// 창을 상하로 분할
		ImVec2 windowSize = ImGui::GetContentRegionAvail();

		// 상단 영역: 에미터 생성 및 내보내기
		if (ImGui::BeginChild("CreateSection", ImVec2(0, windowSize.y * 0.3f), true)) {
			// 드래그 드롭 타겟 추가 (통합된 버전)
			RenderUnifiedDragDropTarget();

			if (ImGui::Button("Create New Emitter")) {
				StartCreateEmitter();
			}

			ImGui::Separator();
			RenderEffectPlaybackSettings();

			// JSON 저장/로드 UI를 항상 표시
			ImGui::Separator();
			RenderJsonSaveLoadUI();

			if (!m_tempEmitters.empty()) {
				ImGui::Separator();
				ImGui::Text("Export to Game");

				static char effectName[256] = "MyCustomEffect";
				ImGui::InputText("Effect Name", effectName, sizeof(effectName));

				if (ImGui::Button("Export to Game")) {
					ExportToManager(std::string(effectName));
				}
			}
		}
		ImGui::EndChild();

		// 하단 영역: 에미터 관리 및 미리보기
		if (ImGui::BeginChild("ManageSection", ImVec2(0, 0), true)) {
			// 드래그 드롭 타겟 추가 (통합된 버전)
			RenderUnifiedDragDropTarget();

			RenderPreviewControls();

			if (!m_tempEmitters.empty()) {
				ImGui::Text("Preview Emitters (%zu)", m_tempEmitters.size());
				ImGui::Separator();

				for (int i = 0; i < m_tempEmitters.size(); ++i) {
					ImGui::PushID(i);

					ImGui::Text("Emitter %d: %s", i, m_tempEmitters[i].name.c_str());

					// 에미터 오프셋 설정 (이펙트 기준점에서의 상대 위치)
					if (m_tempEmitters[i].particleSystem) {
						Mathf::Vector3 currentPos = m_tempEmitters[i].particleSystem->GetPosition();
						float pos[3] = { currentPos.x, currentPos.y, currentPos.z };

						if (ImGui::DragFloat3("Emitter Position", pos, 0.1f)) {
							// ParticleSystem의 위치를 직접 설정 (이게 곧 상대 위치)
							m_tempEmitters[i].particleSystem->SetPosition(Mathf::Vector3(pos[0], pos[1], pos[2]));
						}

						// 도움말 텍스트
						ImGui::SameLine();
						ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Relative to Effect Center)");


						// 최대 파티클 수 설정
						UINT currentMaxParticles = m_tempEmitters[i].particleSystem->GetMaxParticles();
						int maxParticles = static_cast<int>(currentMaxParticles);
						if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 1, 100000)) {
							if (maxParticles > 0) {
								m_tempEmitters[i].particleSystem->ResizeParticleSystem(static_cast<UINT>(maxParticles));
							}
						}
					}

					// 텍스처 할당 UI
					ImGui::Text("Assign Texture:");
					ImGui::SameLine();
					if (!m_textures.empty()) {
						// 벡터 크기 확인 및 조정
						if (m_emitterTextureSelections.size() <= i) {
							m_emitterTextureSelections.resize(m_tempEmitters.size(), 0);
						}

						std::string comboLabel = "Texture##" + std::to_string(i);

						// 현재 에미터의 선택된 텍스처 이름 표시
						std::string currentTextureName = (m_emitterTextureSelections[i] >= 0 && m_emitterTextureSelections[i] < m_textures.size())
							? m_textures[m_emitterTextureSelections[i]]->m_name
							: "None";

						if (ImGui::BeginCombo(comboLabel.c_str(), currentTextureName.c_str())) {
							for (int t = 0; t < m_textures.size(); ++t) {
								bool isSelected = (m_emitterTextureSelections[i] == t);
								std::string textureLabel = m_textures[t]->m_name;

								if (textureLabel.empty()) {
									textureLabel = "Texture " + std::to_string(t);
								}

								if (ImGui::Selectable(textureLabel.c_str(), isSelected)) {
									m_emitterTextureSelections[i] = t;
								}
								if (isSelected) {
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}

						ImGui::SameLine();
						if (ImGui::Button(("Assign##" + std::to_string(i)).c_str())) {
							AssignTextureToEmitter(i, m_emitterTextureSelections[i]);
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
		// 에미터 편집 모드에서도 드래그 드롭 적용 (통합된 버전)
		RenderUnifiedDragDropTarget();
		RenderEmitterEditor();
	}
	else if (m_isModifyingEmitter) {
		// 에미터 수정 모드에서도 드래그 드롭 적용 (통합된 버전)
		RenderUnifiedDragDropTarget();
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
		else if (dynamic_cast<TrailGenerateModule*>(&module)) {
			moduleName = "Trail Module";
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
		else if (dynamic_cast<TrailRenderModule*>(&render)) {
			renderName = "Trail Render";
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

void EffectEditor::RenderEffectPlaybackSettings()
{
	ImGui::Text("Effect Playback Settings");
	ImGui::Separator();

	ImGui::DragFloat("Time Scale", &m_effectTimeScale, 0.01f, 0.1f, 5.0f);
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Controls the playback speed of the entire effect");
	}

	if (ImGui::Checkbox("Loop", &m_effectLoop)) {
		// 루프가 켜지면 duration을 -1로 설정
		if (m_effectLoop) {
			m_effectDuration = -1.0f;
		}
		// 루프가 꺼지면 기본 duration 값으로 설정
		else {
			m_effectDuration = 1.0f;
		}
	}
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("Whether the effect should loop when it finishes");
	}

	// 루프가 꺼져있을 때만 duration 설정 가능
	if (!m_effectLoop) {
		ImGui::DragFloat("Duration", &m_effectDuration, 0.1f, 0.1f, 100.0f);
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("How long the effect should play (seconds)");
		}
	}
	else {
		// 루프가 켜져있으면 duration을 비활성화 상태로 표시
		ImGui::BeginDisabled();
		float disabledDuration = -1.0f;
		ImGui::DragFloat("Duration", &disabledDuration, 0.1f, -1.0f, 100.0f);
		ImGui::EndDisabled();
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Duration is infinite when loop is enabled");
		}
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
	m_editingEmitter = std::make_shared<ParticleSystem>();
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
		m_emitterTextureSelections.push_back(0);

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
			else if (auto* meshColorModule = dynamic_cast<MeshColorModuleCS*>(&module)) {
				ImGui::Text("Mesh Color Module Settings");
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
		m_editingEmitter->GetModule<SpawnModuleCS>()->Initialize();
		break;
	case EffectModuleType::MeshSpawnModule:
		m_editingEmitter->AddModule<MeshSpawnModuleCS>();
		m_editingEmitter->GetModule<MeshSpawnModuleCS>()->Initialize();
		break;
	case EffectModuleType::MovementModule:
		m_editingEmitter->AddModule<MovementModuleCS>();
		m_editingEmitter->GetModule<MovementModuleCS>()->Initialize();
		break;
	case EffectModuleType::ColorModule:
		m_editingEmitter->AddModule<ColorModuleCS>();
		m_editingEmitter->GetModule<ColorModuleCS>()->Initialize();
		break;
	case EffectModuleType::SizeModule:
		m_editingEmitter->AddModule<SizeModuleCS>();
		m_editingEmitter->GetModule<SizeModuleCS>()->Initialize();
		break;
	case EffectModuleType::TrailModule:
		m_editingEmitter->AddModule<TrailGenerateModule>();
		m_editingEmitter->GetModule<TrailGenerateModule>()->Initialize();
		break;
	case EffectModuleType::MeshColorModule:
		m_editingEmitter->AddModule<MeshColorModuleCS>();
		m_editingEmitter->GetModule<MeshColorModuleCS>()->Initialize();
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
		m_editingEmitter->GetRenderModule<BillboardModuleGPU>()->Initialize();
		break;
	case RenderType::Mesh:
		m_editingEmitter->SetParticleDatatype(ParticleDataType::Mesh);
		m_editingEmitter->AddRenderModule<MeshModuleGPU>();
		m_editingEmitter->GetRenderModule<MeshModuleGPU>()->Initialize();
		break;
	case RenderType::Trail:
		m_editingEmitter->SetParticleDatatype(ParticleDataType::Mesh);
		m_editingEmitter->AddRenderModule<TrailRenderModule>();
		m_editingEmitter->GetRenderModule<TrailRenderModule>()->Initialize();
	}
}

void EffectEditor::SaveEffectToJson(const std::string& filename)
{
	m_saveFileName[0] = '\0';
	if (m_tempEmitters.empty()) {
		std::cout << "No emitters to save!" << std::endl;
		return;
	}

	try {
		file::path filepath = PathFinder::Relative("Effect\\") / filename;
		filepath.replace_extension(".json");
		std::string effectName = filepath.stem().string();

		auto tempEffect = std::make_unique<EffectBase>();
		tempEffect->SetName(effectName);
		tempEffect->SetTimeScale(m_effectTimeScale);
		tempEffect->SetLoop(m_effectLoop);
		tempEffect->SetDuration(m_effectDuration);

		for (const auto& tempEmitter : m_tempEmitters) {
			if (tempEmitter.particleSystem) {
				tempEmitter.particleSystem->m_name = tempEmitter.name;
				tempEffect->AddParticleSystem(tempEmitter.particleSystem);
			}
		}

		nlohmann::json effectJson = EffectSerializer::SerializeEffect(*tempEffect);

		ExportToManager(tempEffect->GetName());

		std::ofstream file(filepath);
		if (file.is_open()) {
			file << effectJson.dump(4);
			file.close();
			std::cout << "Effect saved to: " << filepath << std::endl;
		}
		else {
			std::cerr << "Failed to open file for writing: " << filepath << std::endl;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Error saving effect: " << e.what() << std::endl;
	}
}

void EffectEditor::LoadEffectFromJson(const std::string& filename)
{
	m_loadFileName[0] = '\0';
	try {
		file::path filepath = PathFinder::Relative("Effect\\") / filename;
		filepath.replace_extension(".json");

		std::ifstream file(filepath);
		if (!file.is_open()) {
			std::cerr << "Failed to open file: " << filepath << std::endl;
			return;
		}

		nlohmann::json effectJson;
		file >> effectJson;
		file.close();

		auto loadedEffect = EffectSerializer::DeserializeEffect(effectJson);
		if (loadedEffect) {
			m_tempEmitters.clear();
			m_emitterTextureSelections.clear();

			const auto& particleSystems = loadedEffect->GetAllParticleSystems();

			for (size_t i = 0; i < particleSystems.size(); ++i) {
				TempEmitterInfo tempEmitter;
				m_emitterTextureSelections.push_back(0);
				tempEmitter.particleSystem = particleSystems[i];

				std::string savedName = tempEmitter.particleSystem->m_name;
				tempEmitter.name = savedName.empty() ? ("LoadedEmitter_" + std::to_string(i + 1)) : savedName;
				tempEmitter.isPlaying = false;

				m_tempEmitters.push_back(tempEmitter);

				std::cout << "Emitter " << i << " added. Checking references..." << std::endl;
				if (tempEmitter.particleSystem) {
					const auto& renderModules = tempEmitter.particleSystem->GetRenderModules();
					std::cout << "  RenderModules count: " << renderModules.size() << std::endl;
					for (size_t j = 0; j < renderModules.size(); ++j) {
						auto renderModule = renderModules[j];
						std::cout << "    Module[" << j << "] ptr: " << renderModule << std::endl;
						if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
							Texture* tex = meshModule->GetAssignedTexture();
							std::cout << "      Texture: " << (tex ? tex->m_name : "NONE") << std::endl;
						}
					}
				}
			}

			m_loadedEffect = std::move(loadedEffect);
			SyncResourcesFromLoadedEmitters();

			std::cout << "Effect loaded from: " << filepath << std::endl;
			std::cout << "Loaded " << particleSystems.size() << " particle systems" << std::endl;
			std::cout << "Synced " << m_textures.size() << " textures to editor" << std::endl;
		}
		else {
			std::cerr << "Failed to deserialize effect from JSON" << std::endl;
		}
	}
	catch (const std::exception& e) {
		std::cerr << "Error loading effect: " << e.what() << std::endl;
	}
}

void EffectEditor::SyncResourcesFromLoadedEmitters()
{
	int textureFoundCount = 0;

	for (size_t i = 0; i < m_tempEmitters.size(); ++i) {
		const auto& tempEmitter = m_tempEmitters[i];

		if (!tempEmitter.particleSystem) {
			std::cout << "ERROR: Emitter " << i << " has no particle system!" << std::endl;
			continue;
		}

		const auto& renderModules = tempEmitter.particleSystem->GetRenderModules();

		for (size_t j = 0; j < renderModules.size(); ++j) {
			const auto& renderModule = renderModules[j];

			// MeshModuleGPU 체크
			if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {

				Texture* assignedTexture = meshModule->GetAssignedTexture();
				if (assignedTexture) {
					textureFoundCount++;
					AddTextureToEditorList(assignedTexture);
				}
				else {
					std::cout << "    -> No texture assigned" << std::endl;
				}

				Model* assignedModel = meshModule->GetCurrentModel();
				if (assignedModel) {
					std::cout << "    -> Model found: " << assignedModel->name << std::endl;
				}
				else {
					std::cout << "    -> No model assigned" << std::endl;
				}
			}

			// BillboardModuleGPU 체크
			if (auto* billboardModule = dynamic_cast<BillboardModuleGPU*>(renderModule)) {

				Texture* assignedTexture = billboardModule->GetAssignedTexture();
				if (assignedTexture) {
					std::cout << "    -> Texture found: " << assignedTexture->m_name << " (ptr: " << assignedTexture << ")" << std::endl;
					textureFoundCount++;
					AddTextureToEditorList(assignedTexture);
				}
				else {
					std::cout << "    -> No texture assigned" << std::endl;
				}
			}

			// TrailRenderModule 체크
			if (auto* trailRenderModule = dynamic_cast<TrailRenderModule*>(renderModule)) {

				Texture* assignedTexture = trailRenderModule->GetAssignedTexture();
				if (assignedTexture) {
					std::cout << "    -> Texture found: " << assignedTexture->m_name << " (ptr: " << assignedTexture << ")" << std::endl;
					textureFoundCount++;
					AddTextureToEditorList(assignedTexture);
				}
				else {
					std::cout << "    -> No texture assigned" << std::endl;
				}
			}

			// 만약 둘 다 아니라면
			if (!dynamic_cast<MeshModuleGPU*>(renderModule) && !dynamic_cast<BillboardModuleGPU*>(renderModule)) {
				std::cout << "    -> Unknown render module type: " << typeid(*renderModule).name() << std::endl;
			}
		}
	}
}

void EffectEditor::AddTextureToEditorList(Texture* texture)
{
	if (!texture) {
		std::cout << "ERROR: texture is nullptr!" << std::endl;
		return;
	}

	// 중복 체크만 하고 이름은 그대로 유지
	for (const auto& existingTexture : m_textures) {
		if (existingTexture == texture) {  // 포인터로 중복 체크
			std::cout << "DUPLICATE texture pointer found, skipping" << std::endl;
			return;
		}
	}

	// 새로운 텍스처 추가
	m_textures.push_back(texture);
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
	const char* emitterTypes[] = { "Point", "Sphere", "Box", "Cone", "Circle" };

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
	if (!movementModule) return;

	if (ImGui::CollapsingHeader("Movement Module", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 모듈 활성화 체크박스
		bool enabled = movementModule->IsEnabled();
		if (ImGui::Checkbox("Enable Movement Module", &enabled))
		{
			movementModule->SetEnabled(enabled);
		}

		if (!enabled) return;

		ImGui::Separator();

		// Velocity 모드 선택
		if (ImGui::TreeNode("Velocity Mode"))
		{
			static const char* velocityModes[] = {
				"Constant",
				"Curve",
				"Impulse",
				"Wind",
				"Orbital",
				"Explosive"
			};

			int currentVelocityMode = static_cast<int>(movementModule->GetVelocityMode());
			if (ImGui::Combo("Velocity Mode", &currentVelocityMode, velocityModes, IM_ARRAYSIZE(velocityModes)))
			{
				movementModule->SetVelocityMode(static_cast<VelocityMode>(currentVelocityMode));
			}

			ImGui::TreePop();
		}

		ImGui::Separator();

		int currentVelocityMode = static_cast<int>(movementModule->GetVelocityMode());

		if (currentVelocityMode == 1) // Curve
		{
			if (ImGui::TreeNode("Velocity Curve"))
			{
				// 모듈에서 현재 curve 데이터 가져오기
				auto currentCurve = movementModule->GetVelocityCurve();

				// static 대신 실시간으로 현재 데이터 사용
				auto velocityPoints = currentCurve;

				// 빈 경우 기본값 추가하고 바로 적용
				if (velocityPoints.empty())
				{
					VelocityPoint defaultPoint;
					defaultPoint.time = 0.0f;
					defaultPoint.velocity = Mathf::Vector3(0, 2, 0);
					defaultPoint.strength = 1.0f;
					velocityPoints.push_back(defaultPoint);

					defaultPoint.time = 0.5f;
					defaultPoint.velocity = Mathf::Vector3(1, 0, 0);
					defaultPoint.strength = 0.8f;
					velocityPoints.push_back(defaultPoint);

					defaultPoint.time = 1.0f;
					defaultPoint.velocity = Mathf::Vector3(0, -1, 0);
					defaultPoint.strength = 0.5f;
					velocityPoints.push_back(defaultPoint);

					// 기본값을 바로 적용
					movementModule->SetVelocityCurve(velocityPoints);
				}

				bool curveChanged = false;

				for (int i = 0; i < velocityPoints.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Point %d", i);

					if (ImGui::TreeNode(label))
					{
						if (ImGui::SliderFloat("Time", &velocityPoints[i].time, 0.0f, 1.0f))
						{
							curveChanged = true;
						}

						float velocity[3] = {
							velocityPoints[i].velocity.x,
							velocityPoints[i].velocity.y,
							velocityPoints[i].velocity.z
						};

						if (ImGui::DragFloat3("Velocity", velocity, 0.1f, -10.0f, 10.0f))
						{
							velocityPoints[i].velocity = Mathf::Vector3(velocity[0], velocity[1], velocity[2]);
							curveChanged = true;
						}

						if (ImGui::SliderFloat("Strength", &velocityPoints[i].strength, 0.0f, 2.0f))
						{
							curveChanged = true;
						}

						if (velocityPoints.size() > 1)
						{
							if (ImGui::Button("Delete Point"))
							{
								velocityPoints.erase(velocityPoints.begin() + i);
								movementModule->SetVelocityCurve(velocityPoints); // 즉시 적용
								ImGui::TreePop();
								ImGui::PopID();
								break;
							}
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Point") && velocityPoints.size() < 16)
				{
					VelocityPoint newPoint;
					newPoint.time = velocityPoints.empty() ? 0.5f : std::min(velocityPoints.back().time + 0.1f, 1.0f);
					newPoint.velocity = Mathf::Vector3(0, 0, 0);
					newPoint.strength = 1.0f;
					velocityPoints.push_back(newPoint);
					movementModule->SetVelocityCurve(velocityPoints); // 즉시 적용
				}

				// 실시간 업데이트 - 매 프레임마다 적용
				if (curveChanged)
				{
					movementModule->SetVelocityCurve(velocityPoints);
				}

				ImGui::TreePop();
			}
		}
		// Impulse 설정 (Impulse 모드일 때)
		else if (currentVelocityMode == 2) // Impulse
		{
			if (ImGui::TreeNode("Impulse Settings"))
			{
				// 모듈에서 현재 impulse 데이터 가져오기
				auto currentImpulses = movementModule->GetImpulses();
				auto impulses = currentImpulses;

				// 빈 경우 기본값 추가하고 바로 적용
				if (impulses.empty())
				{
					ImpulseData defaultImpulse;
					defaultImpulse.triggerTime = 0.2f;
					defaultImpulse.direction = Mathf::Vector3(0, 1, 0);
					defaultImpulse.force = 5.0f;
					defaultImpulse.duration = 0.1f;
					impulses.push_back(defaultImpulse);

					// 기본값을 바로 적용
					movementModule->ClearImpulses();
					for (const auto& impulse : impulses)
					{
						movementModule->AddImpulse(impulse.triggerTime, impulse.direction, impulse.force, impulse.duration);
					}
				}

				bool impulsesChanged = false;

				for (int i = 0; i < impulses.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Impulse %d", i);

					if (ImGui::TreeNode(label))
					{
						if (ImGui::SliderFloat("Trigger Time", &impulses[i].triggerTime, 0.0f, 1.0f))
							impulsesChanged = true;

						float direction[3] = {
							impulses[i].direction.x,
							impulses[i].direction.y,
							impulses[i].direction.z
						};

						if (ImGui::DragFloat3("Direction", direction, 0.1f, -1.0f, 1.0f))
						{
							impulses[i].direction = Mathf::Vector3(direction[0], direction[1], direction[2]);
							impulsesChanged = true;
						}

						if (ImGui::SliderFloat("Force", &impulses[i].force, 0.0f, 20.0f))
							impulsesChanged = true;

						if (ImGui::SliderFloat("Duration", &impulses[i].duration, 0.01f, 1.0f))
							impulsesChanged = true;

						if (ImGui::Button("Delete Impulse"))
						{
							impulses.erase(impulses.begin() + i);
							// 즉시 적용
							movementModule->ClearImpulses();
							for (const auto& impulse : impulses)
							{
								movementModule->AddImpulse(impulse.triggerTime, impulse.direction, impulse.force, impulse.duration);
							}
							ImGui::TreePop();
							ImGui::PopID();
							break;
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Impulse") && impulses.size() < 16)
				{
					ImpulseData newImpulse;
					newImpulse.triggerTime = 0.5f;
					newImpulse.direction = Mathf::Vector3(0, 1, 0);
					newImpulse.force = 5.0f;
					newImpulse.duration = 0.1f;
					impulses.push_back(newImpulse);

					// 즉시 적용
					movementModule->ClearImpulses();
					for (const auto& impulse : impulses)
					{
						movementModule->AddImpulse(impulse.triggerTime, impulse.direction, impulse.force, impulse.duration);
					}
				}

				// 실시간 업데이트 - 매 프레임마다 적용
				if (impulsesChanged)
				{
					movementModule->ClearImpulses();
					for (const auto& impulse : impulses)
					{
						movementModule->AddImpulse(impulse.triggerTime, impulse.direction, impulse.force, impulse.duration);
					}
				}

				ImGui::TreePop();
			}
		}
		// Wind 설정 (Wind 모드일 때)
		else if (currentVelocityMode == 3) // Wind
		{
			if (ImGui::TreeNode("Wind Settings"))
			{
				// 모듈에서 현재 wind 데이터 가져오기
				auto windData = movementModule->GetWindData();

				// static 대신 실시간으로 현재 데이터 사용
				float windDirection[3] = { windData.direction.x, windData.direction.y, windData.direction.z };
				float windStrength = windData.baseStrength;
				float turbulence = windData.turbulence;
				float frequency = windData.frequency;

				bool windChanged = false;

				if (ImGui::DragFloat3("Wind Direction", windDirection, 0.1f, -1.0f, 1.0f))
				{
					// 정규화
					float length = sqrt(windDirection[0] * windDirection[0] +
						windDirection[1] * windDirection[1] +
						windDirection[2] * windDirection[2]);
					if (length > 0.001f)
					{
						windDirection[0] /= length;
						windDirection[1] /= length;
						windDirection[2] /= length;
					}
					windChanged = true;
				}

				if (ImGui::SliderFloat("Wind Strength", &windStrength, 0.0f, 10.0f))
					windChanged = true;

				if (ImGui::SliderFloat("Turbulence", &turbulence, 0.0f, 2.0f))
					windChanged = true;

				if (ImGui::SliderFloat("Frequency", &frequency, 0.1f, 5.0f))
					windChanged = true;

				// 실시간 업데이트 - 매 프레임마다 적용
				if (windChanged)
				{
					movementModule->SetWindEffect(
						Mathf::Vector3(windDirection[0], windDirection[1], windDirection[2]),
						windStrength,
						turbulence,
						frequency
					);
				}

				ImGui::TreePop();
			}
		}
		// Orbital 설정 (Orbital 모드일 때)
		else if (currentVelocityMode == 4) // Orbital
		{
			if (ImGui::TreeNode("Orbital Settings"))
			{
				// 모듈에서 현재 orbital 데이터 가져오기
				auto orbitalData = movementModule->GetOrbitalData();

				// static 대신 실시간으로 현재 데이터 사용
				float orbitalCenter[3] = { orbitalData.center.x, orbitalData.center.y, orbitalData.center.z };
				float orbitalRadius = orbitalData.radius;
				float orbitalSpeed = orbitalData.speed;
				float orbitalAxis[3] = { orbitalData.axis.x, orbitalData.axis.y, orbitalData.axis.z };

				bool orbitalChanged = false;

				if (ImGui::DragFloat3("Orbital Center", orbitalCenter, 0.1f, -20.0f, 20.0f))
					orbitalChanged = true;

				if (ImGui::SliderFloat("Radius", &orbitalRadius, 0.1f, 20.0f))
					orbitalChanged = true;

				if (ImGui::SliderFloat("Speed", &orbitalSpeed, -5.0f, 5.0f))
					orbitalChanged = true;

				if (ImGui::DragFloat3("Rotation Axis", orbitalAxis, 0.1f, -1.0f, 1.0f))
				{
					// 정규화
					float length = sqrt(orbitalAxis[0] * orbitalAxis[0] +
						orbitalAxis[1] * orbitalAxis[1] +
						orbitalAxis[2] * orbitalAxis[2]);
					if (length > 0.001f)
					{
						orbitalAxis[0] /= length;
						orbitalAxis[1] /= length;
						orbitalAxis[2] /= length;
					}
					orbitalChanged = true;
				}

				// 프리셋 버튼들
				ImGui::Text("Presets:");
				if (ImGui::Button("Horizontal"))
				{
					orbitalAxis[0] = 0.0f; orbitalAxis[1] = 1.0f; orbitalAxis[2] = 0.0f;
					// 즉시 적용
					movementModule->SetOrbitalMotion(
						Mathf::Vector3(orbitalCenter[0], orbitalCenter[1], orbitalCenter[2]),
						orbitalRadius,
						orbitalSpeed,
						Mathf::Vector3(orbitalAxis[0], orbitalAxis[1], orbitalAxis[2])
					);
				}
				ImGui::SameLine();
				if (ImGui::Button("Vertical"))
				{
					orbitalAxis[0] = 1.0f; orbitalAxis[1] = 0.0f; orbitalAxis[2] = 0.0f;
					// 즉시 적용
					movementModule->SetOrbitalMotion(
						Mathf::Vector3(orbitalCenter[0], orbitalCenter[1], orbitalCenter[2]),
						orbitalRadius,
						orbitalSpeed,
						Mathf::Vector3(orbitalAxis[0], orbitalAxis[1], orbitalAxis[2])
					);
				}
				ImGui::SameLine();
				if (ImGui::Button("Depth"))
				{
					orbitalAxis[0] = 0.0f; orbitalAxis[1] = 0.0f; orbitalAxis[2] = 1.0f;
					// 즉시 적용
					movementModule->SetOrbitalMotion(
						Mathf::Vector3(orbitalCenter[0], orbitalCenter[1], orbitalCenter[2]),
						orbitalRadius,
						orbitalSpeed,
						Mathf::Vector3(orbitalAxis[0], orbitalAxis[1], orbitalAxis[2])
					);
				}

				// 실시간 업데이트 - 매 프레임마다 적용
				if (orbitalChanged)
				{
					movementModule->SetOrbitalMotion(
						Mathf::Vector3(orbitalCenter[0], orbitalCenter[1], orbitalCenter[2]),
						orbitalRadius,
						orbitalSpeed,
						Mathf::Vector3(orbitalAxis[0], orbitalAxis[1], orbitalAxis[2])
					);
				}

				ImGui::TreePop();
			}
		}
		// Explosive 설정 (Explosive 모드일 때)
		else if (currentVelocityMode == 5) // Explosive
		{
			if (ImGui::TreeNode("Explosive Settings"))
			{
				// 모듈에서 현재 explosive 데이터 가져오기
				auto explosiveData = movementModule->GetExplosiveData();

				// static 대신 실시간으로 현재 데이터 사용
				float initialSpeed = explosiveData.initialSpeed;
				float speedDecay = explosiveData.speedDecay;
				float randomFactor = explosiveData.randomFactor;
				float sphereRadius = explosiveData.sphereRadius;

				bool explosiveChanged = false;

				ImGui::Text("Basic Settings:");
				if (ImGui::SliderFloat("Initial Speed", &initialSpeed, 1.0f, 200.0f))
					explosiveChanged = true;

				if (ImGui::SliderFloat("Speed Decay", &speedDecay, 0.1f, 5.0f))
					explosiveChanged = true;
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Higher values make particles slow down faster");

				ImGui::Text("Distribution:");
				if (ImGui::SliderFloat("Random Factor", &randomFactor, 0.0f, 1.0f))
					explosiveChanged = true;
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("0 = uniform spread, 1 = very random");

				if (ImGui::SliderFloat("Sphere Radius", &sphereRadius, 0.1f, 2.0f))
					explosiveChanged = true;
				ImGui::SameLine();
				ImGui::TextDisabled("(?)");
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("1.0 = perfect sphere, lower values = flatter distribution");

				// 프리셋 버튼들
				ImGui::Separator();
				ImGui::Text("Presets:");

				if (ImGui::Button("Fireworks"))
				{
					initialSpeed = 80.0f;
					speedDecay = 1.5f;
					randomFactor = 0.3f;
					sphereRadius = 1.0f;
					explosiveChanged = true;
				}
				ImGui::SameLine();

				if (ImGui::Button("Grenade"))
				{
					initialSpeed = 120.0f;
					speedDecay = 2.5f;
					randomFactor = 0.6f;
					sphereRadius = 0.8f;
					explosiveChanged = true;
				}
				ImGui::SameLine();

				if (ImGui::Button("Soft Pop"))
				{
					initialSpeed = 30.0f;
					speedDecay = 0.8f;
					randomFactor = 0.2f;
					sphereRadius = 1.2f;
					explosiveChanged = true;
				}

				if (ImGui::Button("Ring Burst"))
				{
					initialSpeed = 60.0f;
					speedDecay = 1.2f;
					randomFactor = 0.1f;
					sphereRadius = 0.3f;
					explosiveChanged = true;
				}
				ImGui::SameLine();

				if (ImGui::Button("Chaos"))
				{
					initialSpeed = 100.0f;
					speedDecay = 3.0f;
					randomFactor = 0.8f;
					sphereRadius = 1.5f;
					explosiveChanged = true;
				}

				// 실시간 업데이트
				if (explosiveChanged)
				{
					movementModule->SetExplosiveEffect(
						initialSpeed,
						speedDecay,
						randomFactor,
						sphereRadius
					);
				}

				ImGui::TreePop();
			}
			}


		ImGui::Separator();

		// 중력 설정
		if (ImGui::TreeNode("Gravity Settings"))
		{
			bool useGravity = movementModule->GetUseGravity();
			if (ImGui::Checkbox("Use Gravity", &useGravity))
			{
				movementModule->SetUseGravity(useGravity);
			}

			if (useGravity)
			{
				// static 대신 실시간으로 현재 값 가져오기
				static float gravityStrength = 9.8f;
				static bool gravityInitialized = false;

				// 첫 번째 렌더링에서만 현재 값으로 초기화
				if (!gravityInitialized)
				{
					// movementModule에서 현재 중력 강도를 가져오는 getter 필요
					gravityInitialized = true;
				}

				if (ImGui::SliderFloat("Gravity Strength", &gravityStrength, 0.0f, 50.0f, "%.2f"))
				{
					movementModule->SetGravityStrength(gravityStrength);
				}

				ImGui::SameLine();
				if (ImGui::Button("Reset##Gravity"))
				{
					gravityStrength = 9.8f;
					movementModule->SetGravityStrength(gravityStrength);
				}

				// 프리셋 버튼들
				ImGui::Text("Gravity Presets:");
				ImGui::SameLine();

				if (ImGui::Button("Earth"))
				{
					gravityStrength = 9.8f;
					movementModule->SetGravityStrength(gravityStrength);
				}
				ImGui::SameLine();

				if (ImGui::Button("Moon"))
				{
					gravityStrength = 1.6f;
					movementModule->SetGravityStrength(gravityStrength);
				}
				ImGui::SameLine();

				if (ImGui::Button("Mars"))
				{
					gravityStrength = 3.7f;
					movementModule->SetGravityStrength(gravityStrength);
				}
				ImGui::SameLine();

				if (ImGui::Button("Jupiter"))
				{
					gravityStrength = 24.8f;
					movementModule->SetGravityStrength(gravityStrength);
				}
			}

			ImGui::TreePop();
		}

		ImGui::Separator();

		// 이징 설정
		if (ImGui::TreeNode("Easing Settings"))
		{
			static bool easingEnabled = false;
			if (ImGui::Checkbox("Enable Easing", &easingEnabled))
			{
				movementModule->SetEasingEnabled(easingEnabled);
			}

			if (easingEnabled)
			{
				static const char* easingTypes[] = {
					"Linear",
					"Ease In Quad", "Ease Out Quad", "Ease In Out Quad",
					"Ease In Cubic", "Ease Out Cubic", "Ease In Out Cubic",
					"Ease In Quart", "Ease Out Quart", "Ease In Out Quart",
					"Ease In Quint", "Ease Out Quint", "Ease In Out Quint",
					"Ease In Sine", "Ease Out Sine", "Ease In Out Sine",
					"Ease In Expo", "Ease Out Expo", "Ease In Out Expo",
					"Ease In Circ", "Ease Out Circ", "Ease In Out Circ",
					"Ease In Back", "Ease Out Back", "Ease In Out Back",
					"Ease In Elastic", "Ease Out Elastic", "Ease In Out Elastic",
					"Ease In Bounce", "Ease Out Bounce", "Ease In Out Bounce"
				};

				static int currentEasingType = 0;
				if (ImGui::Combo("Easing Type", &currentEasingType, easingTypes, IM_ARRAYSIZE(easingTypes)))
				{
					movementModule->SetEasingType(currentEasingType);
				}
			}

			ImGui::TreePop();
		}

		ImGui::Separator();

		// 디버그 정보
		if (ImGui::TreeNode("Debug Info"))
		{
			ImGui::Text("Module Status: %s", movementModule->IsEnabled() ? "Enabled" : "Disabled");
			ImGui::Text("Ready for Reuse: %s", movementModule->IsReadyForReuse() ? "Yes" : "No");
			ImGui::Text("Current Velocity Mode: %d", static_cast<int>(movementModule->GetVelocityMode()));
			ImGui::Text("Velocity Points: %zu", movementModule->GetVelocityCurve().size());
			ImGui::Text("Impulses: %zu", movementModule->GetImpulses().size());

			if (ImGui::Button("Reset Module"))
			{
				movementModule->ResetForReuse();
			}

			ImGui::TreePop();
		}
	}
}

void EffectEditor::RenderColorModuleEditor(ColorModuleCS* colorModule)
{
	if (!colorModule) return;

	if (ImGui::CollapsingHeader("Color Module", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 모듈 활성화 체크박스
		bool enabled = colorModule->IsEnabled();
		if (ImGui::Checkbox("Enable Color Module", &enabled))
		{
			colorModule->SetEnabled(enabled);
		}

		if (!enabled) return;

		ImGui::Separator();

		// 색상 전환 모드 선택
		static const char* transitionModes[] = {
			"Gradient",
			"Discrete",
			"Custom"
		};

		static int currentTransitionMode = 0;
		if (ImGui::Combo("Transition Mode", &currentTransitionMode, transitionModes, IM_ARRAYSIZE(transitionModes)))
		{
			colorModule->SetTransitionMode(static_cast<ColorTransitionMode>(currentTransitionMode));
		}

		ImGui::Separator();

		// 그라데이션 모드
		if (currentTransitionMode == 0)
		{
			if (ImGui::TreeNode("Gradient Settings"))
			{
				static std::vector<std::pair<float, Mathf::Vector4>> gradientPoints = {
					{0.0f, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f)},
					{0.3f, Mathf::Vector4(1.0f, 1.0f, 0.0f, 0.9f)},
					{0.7f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 0.7f)},
					{1.0f, Mathf::Vector4(0.5f, 0.0f, 0.0f, 0.0f)}
				};

				for (int i = 0; i < gradientPoints.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Point %d", i);

					if (ImGui::TreeNode(label))
					{
						ImGui::SliderFloat("Time", &gradientPoints[i].first, 0.0f, 1.0f);

						float color[4] = {
							gradientPoints[i].second.x,
							gradientPoints[i].second.y,
							gradientPoints[i].second.z,
							gradientPoints[i].second.w
						};

						if (ImGui::ColorEdit4("Color", color))
						{
							gradientPoints[i].second = Mathf::Vector4(color[0], color[1], color[2], color[3]);
						}

						if (gradientPoints.size() > 2)
						{
							if (ImGui::Button("Delete Point"))
							{
								gradientPoints.erase(gradientPoints.begin() + i);
								ImGui::TreePop();
								ImGui::PopID();
								break;
							}
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Point") && gradientPoints.size() < 16)
				{
					float newTime = gradientPoints.empty() ? 0.5f :
						(gradientPoints.back().first + 0.1f > 1.0f ? 0.5f : gradientPoints.back().first + 0.1f);
					gradientPoints.emplace_back(newTime, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				}

				if (ImGui::Button("Apply Gradient"))
				{
					std::sort(gradientPoints.begin(), gradientPoints.end(),
						[](const auto& a, const auto& b) { return a.first < b.first; });

					colorModule->SetColorGradient(gradientPoints);
				}

				ImGui::TreePop();
			}
		}
		// 이산 색상 모드
		else if (currentTransitionMode == 1)
		{
			if (ImGui::TreeNode("Discrete Colors"))
			{
				static std::vector<Mathf::Vector4> discreteColors = {
					Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f),
					Mathf::Vector4(0.0f, 1.0f, 0.0f, 1.0f),
					Mathf::Vector4(0.0f, 0.0f, 1.0f, 1.0f)
				};

				for (int i = 0; i < discreteColors.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Color %d", i);

					float color[4] = {
						discreteColors[i].x,
						discreteColors[i].y,
						discreteColors[i].z,
						discreteColors[i].w
					};

					if (ImGui::ColorEdit4(label, color))
					{
						discreteColors[i] = Mathf::Vector4(color[0], color[1], color[2], color[3]);
					}

					ImGui::SameLine();
					if (ImGui::Button("Remove") && discreteColors.size() > 1)
					{
						discreteColors.erase(discreteColors.begin() + i);
						ImGui::PopID();
						break;
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Color") && discreteColors.size() < 32)
				{
					discreteColors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
				}

				if (ImGui::Button("Apply Colors"))
				{
					colorModule->SetDiscreteColors(discreteColors);
				}

				ImGui::TreePop();
			}
		}
		// 커스텀 함수 모드
		else if (currentTransitionMode == 2)
		{
			if (ImGui::TreeNode("Custom Function"))
			{
				static const char* functionTypes[] = {
					"Pulse",
					"Sine Wave",
					"Flicker"
				};

				static int currentFunction = 0;
				ImGui::Combo("Function Type", &currentFunction, functionTypes, IM_ARRAYSIZE(functionTypes));

				if (currentFunction == 0) // Pulse
				{
					static float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					static float pulseColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
					static float frequency = 1.0f;

					ImGui::ColorEdit4("Base Color", baseColor);
					ImGui::ColorEdit4("Pulse Color", pulseColor);
					ImGui::SliderFloat("Frequency", &frequency, 0.1f, 10.0f);

					if (ImGui::Button("Apply Pulse"))
					{
						colorModule->SetPulseColor(
							Mathf::Vector4(baseColor[0], baseColor[1], baseColor[2], baseColor[3]),
							Mathf::Vector4(pulseColor[0], pulseColor[1], pulseColor[2], pulseColor[3]),
							frequency
						);
					}
				}
				else if (currentFunction == 1) // Sine Wave
				{
					static float color1[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
					static float color2[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
					static float frequency = 1.0f;
					static float phase = 0.0f;

					ImGui::ColorEdit4("Color 1", color1);
					ImGui::ColorEdit4("Color 2", color2);
					ImGui::SliderFloat("Frequency", &frequency, 0.1f, 10.0f);
					ImGui::SliderFloat("Phase", &phase, 0.0f, 6.28f);

					if (ImGui::Button("Apply Sine Wave"))
					{
						colorModule->SetSineWaveColor(
							Mathf::Vector4(color1[0], color1[1], color1[2], color1[3]),
							Mathf::Vector4(color2[0], color2[1], color2[2], color2[3]),
							frequency,
							phase
						);
					}
				}
				else if (currentFunction == 2) // Flicker
				{
					static std::vector<Mathf::Vector4> flickerColors = {
						Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f),
						Mathf::Vector4(1.0f, 1.0f, 0.0f, 1.0f),
						Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f)
					};
					static float speed = 5.0f;

					for (int i = 0; i < flickerColors.size(); ++i)
					{
						ImGui::PushID(i);

						char label[32];
						sprintf_s(label, "Flicker Color %d", i);

						float color[4] = {
							flickerColors[i].x,
							flickerColors[i].y,
							flickerColors[i].z,
							flickerColors[i].w
						};

						if (ImGui::ColorEdit4(label, color))
						{
							flickerColors[i] = Mathf::Vector4(color[0], color[1], color[2], color[3]);
						}

						ImGui::SameLine();
						if (ImGui::Button("Remove") && flickerColors.size() > 1)
						{
							flickerColors.erase(flickerColors.begin() + i);
							ImGui::PopID();
							break;
						}

						ImGui::PopID();
					}

					if (ImGui::Button("Add Flicker Color") && flickerColors.size() < 32)
					{
						flickerColors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
					}

					ImGui::SliderFloat("Flicker Speed", &speed, 0.1f, 20.0f);

					if (ImGui::Button("Apply Flicker"))
					{
						colorModule->SetFlickerColor(flickerColors, speed);
					}
				}

				ImGui::TreePop();
			}
		}

		ImGui::Separator();

		// 이징 설정
		if (ImGui::TreeNode("Easing Settings"))
		{
			static bool easingEnabled = false;
			ImGui::Checkbox("Enable Easing", &easingEnabled);

			if (easingEnabled)
			{
				static const char* easingTypes[] = {
					"Linear", "InSine", "OutSine", "InOutSine",
					"InQuad", "OutQuad", "InOutQuad",
					"InCubic", "OutCubic", "InOutCubic",
					"InQuart", "OutQuart", "InOutQuart",
					"InQuint", "OutQuint", "InOutQuint",
					"InExpo", "OutExpo", "InOutExpo",
					"InCirc", "OutCirc", "InOutCirc",
					"InBack", "OutBack", "InOutBack",
					"InElastic", "OutElastic", "InOutElastic",
					"InBounce", "OutBounce", "InOutBounce"
				};

				static const char* animationTypes[] = {
					"Once Forward", "Once Back", "Once PingPong",
					"Loop Forward", "Loop Back", "Loop PingPong"
				};

				static int currentEasingType = 0;
				static int currentAnimationType = 0;
				static float duration = 1.0f;

				ImGui::Combo("Easing Type", &currentEasingType, easingTypes, IM_ARRAYSIZE(easingTypes));
				ImGui::Combo("Animation Type", &currentAnimationType, animationTypes, IM_ARRAYSIZE(animationTypes));
				ImGui::SliderFloat("Duration", &duration, 0.1f, 10.0f);

				if (ImGui::Button("Apply Easing"))
				{
					colorModule->SetEasing(
						static_cast<EasingEffect>(currentEasingType),
						static_cast<StepAnimation>(currentAnimationType),
						duration
					);
				}
			}

			ImGui::TreePop();
		}
	}
}

void EffectEditor::RenderSizeModuleEditor(SizeModuleCS* sizeModule)
{
	if (!sizeModule) return;

	ImGui::SetNextItemOpen(true, ImGuiCond_Once);
	if (ImGui::CollapsingHeader("Size Module"))
	{
		// 모듈 활성화 체크박스
		bool enabled = sizeModule->IsEnabled();
		if (ImGui::Checkbox("Enable Size Module", &enabled))
		{
			sizeModule->SetEnabled(enabled);
		}

		if (!enabled)
		{
			ImGui::BeginDisabled();
		}

		ImGui::Separator();

		// 시작 크기 설정
		XMFLOAT2 startSize = sizeModule->GetStartSize();
		if (ImGui::DragFloat2("Start Size", &startSize.x, 0.01f, 0.01f, 10.0f, "%.2f"))
		{
			sizeModule->SetStartSize(startSize);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Initial size of particles (Width, Height)");
		}

		// 끝 크기 설정
		XMFLOAT2 endSize = sizeModule->GetEndSize();
		if (ImGui::DragFloat2("End Size", &endSize.x, 0.01f, 0.01f, 10.0f, "%.2f"))
		{
			sizeModule->SetEndSize(endSize);
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Final size of particles (Width, Height)");
		}

		ImGui::Separator();

		// 랜덤 스케일 설정
		ImGui::Text("Random Scale");

		bool useRandomScale = sizeModule->GetUseRandomScale();
		if (ImGui::Checkbox("Use Random Scale", &useRandomScale))
		{
			float minScale = sizeModule->GetRandomScaleMin();
			float maxScale = sizeModule->GetRandomScaleMax();
			sizeModule->SetRandomScale(useRandomScale, minScale, maxScale);
		}

		if (useRandomScale)
		{
			ImGui::Indent();

			float minScale = sizeModule->GetRandomScaleMin();
			if (ImGui::DragFloat("Min Scale", &minScale, 0.01f, 0.1f, 5.0f, "%.2f"))
			{
				float maxScale = sizeModule->GetRandomScaleMax();
				if (minScale > maxScale) maxScale = minScale;
				sizeModule->SetRandomScale(true, minScale, maxScale);
			}

			float maxScale = sizeModule->GetRandomScaleMax();
			if (ImGui::DragFloat("Max Scale", &maxScale, 0.01f, 0.1f, 5.0f, "%.2f"))
			{
				float minScale = sizeModule->GetRandomScaleMin();
				if (maxScale < minScale) minScale = maxScale;
				sizeModule->SetRandomScale(true, minScale, maxScale);
			}

			ImGui::Unindent();
		}

		ImGui::Separator();

		// 이징 설정
		ImGui::Text("Easing Animation");

		bool easingEnabled = sizeModule->IsEasingEnabled();
		if (ImGui::Checkbox("Enable Easing", &easingEnabled))
		{
			if (easingEnabled)
			{
				sizeModule->SetEasing(EasingEffect::Linear, StepAnimation::StepLoopForward, 1.0f);
			}
			else
			{
				sizeModule->DisableEasing();
			}
		}

		if (easingEnabled)
		{
			ImGui::Indent();

			// 이징 타입 선택
			const char* easingTypes[] = {
				"Linear",
				"InSine", "OutSine", "InOutSine",
				"InQuad", "OutQuad", "InOutQuad",
				"InCubic", "OutCubic", "InOutCubic",
				"InQuart", "OutQuart", "InOutQuart",
				"InQuint", "OutQuint", "InOutQuint",
				"InExpo", "OutExpo", "InOutExpo",
				"InCirc", "OutCirc", "InOutCirc",
				"InBack", "OutBack", "InOutBack",
				"InElastic", "OutElastic", "InOutElastic",
				"InBounce", "OutBounce", "InOutBounce"
			};
			int currentEasingType = static_cast<int>(sizeModule->GetEasingType());
			if (ImGui::Combo("Easing Type", &currentEasingType, easingTypes, IM_ARRAYSIZE(easingTypes)))
			{
				EasingEffect easingType = static_cast<EasingEffect>(currentEasingType);
				StepAnimation animationType = sizeModule->GetAnimationType();
				float duration = sizeModule->GetEasingDuration();
				sizeModule->SetEasing(easingType, animationType, duration);
			}

			// 애니메이션 타입 선택
			const char* animationTypes[] = {
				"Once Forward", "Once Back", "Once PingPong",
				"Loop Forward", "Loop Back", "Loop PingPong"
			};
			int currentAnimationType = static_cast<int>(sizeModule->GetAnimationType());
			if (ImGui::Combo("Animation Type", &currentAnimationType, animationTypes, IM_ARRAYSIZE(animationTypes)))
			{
				EasingEffect easingType = sizeModule->GetEasingType();
				StepAnimation animationType = static_cast<StepAnimation>(currentAnimationType);
				float duration = sizeModule->GetEasingDuration();
				sizeModule->SetEasing(easingType, animationType, duration);
			}

			// 지속시간 설정
			float duration = sizeModule->GetEasingDuration();
			if (ImGui::DragFloat("Duration", &duration, 0.1f, 0.1f, 10.0f, "%.1f sec"))
			{
				EasingEffect easingType = sizeModule->GetEasingType();
				StepAnimation animationType = sizeModule->GetAnimationType();
				sizeModule->SetEasing(easingType, animationType, duration);
			}

			ImGui::Unindent();
		}

		ImGui::Separator();

		// 프리셋 버튼들
		ImGui::Text("Presets");

		if (ImGui::Button("Small to Large"))
		{
			sizeModule->SetStartSize(XMFLOAT2(0.1f, 0.1f));
			sizeModule->SetEndSize(XMFLOAT2(2.0f, 2.0f));
			sizeModule->SetRandomScale(false, 0.5f, 2.0f);
		}
		ImGui::SameLine();

		if (ImGui::Button("Large to Small"))
		{
			sizeModule->SetStartSize(XMFLOAT2(2.0f, 2.0f));
			sizeModule->SetEndSize(XMFLOAT2(0.1f, 0.1f));
			sizeModule->SetRandomScale(false, 0.5f, 2.0f);
		}
		ImGui::SameLine();

		if (ImGui::Button("Constant Size"))
		{
			sizeModule->SetStartSize(XMFLOAT2(1.0f, 1.0f));
			sizeModule->SetEndSize(XMFLOAT2(1.0f, 1.0f));
			sizeModule->SetRandomScale(true, 0.8f, 1.2f);
		}

		if (ImGui::Button("Explosion Effect"))
		{
			sizeModule->SetStartSize(XMFLOAT2(0.05f, 0.05f));
			sizeModule->SetEndSize(XMFLOAT2(3.0f, 3.0f));
			sizeModule->SetRandomScale(true, 0.5f, 1.5f);
			sizeModule->SetEasing(EasingEffect::OutExpo, StepAnimation::StepOnceForward, 2.0f);
		}
		ImGui::SameLine();

		if (ImGui::Button("Pulse Effect"))
		{
			sizeModule->SetStartSize(XMFLOAT2(0.5f, 0.5f));
			sizeModule->SetEndSize(XMFLOAT2(1.5f, 1.5f));
			sizeModule->SetRandomScale(false, 0.5f, 2.0f);
			sizeModule->SetEasing(EasingEffect::InOutSine, StepAnimation::StepLoopPingPong, 1.0f);
		}

		// 디버그 정보 (디버그 모드에서만 표시)
#ifdef _DEBUG
		if (ImGui::TreeNode("Debug Info"))
		{
			ImGui::Text("Is Initialized: %s", sizeModule->IsInitialized() ? "Yes" : "No");
			ImGui::Text("Particle Capacity: %u", sizeModule->GetParticleCapacity());
			ImGui::Text("Ready for Reuse: %s", sizeModule->IsReadyForReuse() ? "Yes" : "No");

			if (ImGui::Button("Reset Module"))
			{
				sizeModule->ResetForReuse();
			}

			ImGui::TreePop();
		}
#endif

		if (!enabled)
		{
			ImGui::EndDisabled();
		}
	}
}

void EffectEditor::RenderBillboardModuleGPUEditor(BillboardModuleGPU* billboardModule)
{
	if (!billboardModule) return;
	ImGui::Text("Billboard Render Module Settings");
	// 셰이더 선택 UI
	RenderShaderSelectionUI(billboardModule);
	// 렌더 상태 선택 UI
	RenderStateSelectionUI(billboardModule);
	// 다중 텍스처 선택 UI
	RenderTextureSelectionUI(billboardModule);
	ImGui::Separator();
	ImGui::Text("Billboard Specific Settings:");

	// 빌보드 타입 선택
	const char* billboardTypes[] = { "None", "Basic", "SpriteAnimation"};
	int currentType = static_cast<int>(billboardModule->GetBillboardType());
	if (ImGui::Combo("Billboard Type", &currentType, billboardTypes, IM_ARRAYSIZE(billboardTypes)))
	{
		billboardModule->SetBillboardType(static_cast<BillBoardType>(currentType));
	}

	// 스프라이트 애니메이션 설정 (SpriteAnimation 타입일 때만 표시)
	if (billboardModule->GetBillboardType() == BillBoardType::SpriteAnimation)
	{
		ImGui::Separator();
		ImGui::Text("Sprite Animation Settings:");

		auto spriteAnimCB = billboardModule->GetSpriteAnimationBuffer();
		uint32 frameCount = spriteAnimCB.frameCount;
		float animationDuration = spriteAnimCB.animationDuration;
		uint32 gridColumns = spriteAnimCB.gridColumns;
		uint32 gridRows = spriteAnimCB.gridRows;

		bool changed = false;

		if (ImGui::InputScalar("Frame Count", ImGuiDataType_U32, &frameCount))
		{
			changed = true;
		}

		if (ImGui::InputFloat("Animation Duration", &animationDuration, 0.1f, 1.0f, "%.2f"))
		{
			if (animationDuration < 0.1f) animationDuration = 0.1f;
			changed = true;
		}

		if (ImGui::InputScalar("Grid Columns", ImGuiDataType_U32, &gridColumns))
		{
			changed = true;
		}

		if (ImGui::InputScalar("Grid Rows", ImGuiDataType_U32, &gridRows))
		{
			changed = true;
		}

		if (changed)
		{
			billboardModule->SetSpriteAnimation(frameCount, animationDuration, gridColumns, gridRows);
		}

		// 애니메이션 정보 표시
		ImGui::Separator();
		ImGui::Text("Animation Info:");
		ImGui::Text("Total Frames: %u", frameCount);
		ImGui::Text("Animation Duration: %.2f seconds", animationDuration);
		if (frameCount > 0)
		{
			float frameRate = frameCount / animationDuration;
			ImGui::Text("Frame Rate: %.1f FPS", frameRate);
		}

		// 프리뷰 컨트롤 (시뮬레이션용)
		ImGui::Separator();
		ImGui::Text("Preview Control:");
		static float previewTime = 0.0f;

		if (ImGui::SliderFloat("Preview Time", &previewTime, 0.0f, animationDuration, "%.2f"))
		{
			// 프리뷰 시간 변경 - 실제로는 파티클 시스템에서 자동으로 처리됨
		}

		if (ImGui::Button("Reset Preview"))
		{
			previewTime = 0.0f;
		}

		// 현재 프리뷰 프레임 표시
		if (animationDuration > 0.0f && frameCount > 0)
		{
			float normalizedTime = fmod(previewTime, animationDuration) / animationDuration;
			uint32 currentPreviewFrame = static_cast<uint32>(normalizedTime * frameCount) % frameCount;
			ImGui::Text("Current Preview Frame: %u", currentPreviewFrame);
		}
	}
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
	float Scale[3] = { currentTemplate.Scale.x, currentTemplate.Scale.y, currentTemplate.Scale.z };

	if (ImGui::DragFloat3("Min Scale", Scale, 0.01f, 0.01f, 10.0f)) {
		meshSpawnModule->SetParticleScale(
			XMFLOAT3(Scale[0], Scale[1], Scale[2])
		);
	}

	// 3D 회전 속도 범위
	float RotSpeed[3] = { currentTemplate.RotationSpeed.x, currentTemplate.RotationSpeed.y, currentTemplate.RotationSpeed.z };

	if (ImGui::DragFloat3("Min Rotation Speed", RotSpeed, 0.1f, -360.0f, 360.0f)) {
		meshSpawnModule->SetParticleRotationSpeed(
			XMFLOAT3(RotSpeed[0], RotSpeed[1], RotSpeed[2])
		);
	}

	// 3D 초기 회전 범위
	float InitRot[3] = { currentTemplate.InitialRotation.x, currentTemplate.InitialRotation.y, currentTemplate.InitialRotation.z };

	if (ImGui::DragFloat3("Min Initial Rotation", InitRot, 0.1f, -360.0f, 360.0f)) {
		meshSpawnModule->SetParticleInitialRotation(
			XMFLOAT3(InitRot[0], InitRot[1], InitRot[2])
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
	float Vertical = currentTemplate.VerticalVelocity;
	float horizontalRange = currentTemplate.horizontalVelocityRange;

	if (ImGui::DragFloat("Min Vertical Velocity", &Vertical, 0.1f, -100.0f, 100.0f)) {
		meshSpawnModule->SetVelocity(Vertical, horizontalRange);
	}
	if (ImGui::DragFloat("Horizontal Velocity Range", &horizontalRange, 0.1f, 0.0f, 100.0f)) {
		meshSpawnModule->SetVelocity(Vertical, horizontalRange);
	}

	const char* renderModes[] = { "Emissive", "Lit" };
	int currentMode = currentTemplate.renderMode;
	if (ImGui::Combo("Render Mode", &currentMode, renderModes, 2)) {
		meshSpawnModule->SetRenderMode(currentMode);
	}
}

void EffectEditor::RenderMeshColorModuleEditor(MeshColorModuleCS* colorModule)
{
	if (!colorModule) return;

	if (ImGui::CollapsingHeader("Color Module", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 모듈 활성화 체크박스
		bool enabled = colorModule->IsEnabled();
		if (ImGui::Checkbox("Enable Color Module", &enabled))
		{
			colorModule->SetEnabled(enabled);
		}

		if (!enabled) return;

		ImGui::Separator();

		// 색상 전환 모드 선택
		static const char* transitionModes[] = {
			"Gradient",
			"Discrete",
			"Custom"
		};

		static int currentTransitionMode = 0;
		if (ImGui::Combo("Transition Mode", &currentTransitionMode, transitionModes, IM_ARRAYSIZE(transitionModes)))
		{
			colorModule->SetTransitionMode(static_cast<ColorTransitionMode>(currentTransitionMode));
		}

		ImGui::Separator();

		// 그라데이션 모드
		if (currentTransitionMode == 0)
		{
			if (ImGui::TreeNode("Gradient Settings"))
			{
				static std::vector<std::pair<float, Mathf::Vector4>> gradientPoints = {
					{0.0f, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f)},
					{0.3f, Mathf::Vector4(1.0f, 1.0f, 0.0f, 0.9f)},
					{0.7f, Mathf::Vector4(1.0f, 0.0f, 0.0f, 0.7f)},
					{1.0f, Mathf::Vector4(0.5f, 0.0f, 0.0f, 0.0f)}
				};

				for (int i = 0; i < gradientPoints.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Point %d", i);

					if (ImGui::TreeNode(label))
					{
						ImGui::SliderFloat("Time", &gradientPoints[i].first, 0.0f, 1.0f);

						float color[4] = {
							gradientPoints[i].second.x,
							gradientPoints[i].second.y,
							gradientPoints[i].second.z,
							gradientPoints[i].second.w
						};

						if (ImGui::ColorEdit4("Color", color))
						{
							gradientPoints[i].second = Mathf::Vector4(color[0], color[1], color[2], color[3]);
						}

						if (gradientPoints.size() > 2)
						{
							if (ImGui::Button("Delete Point"))
							{
								gradientPoints.erase(gradientPoints.begin() + i);
								ImGui::TreePop();
								ImGui::PopID();
								break;
							}
						}

						ImGui::TreePop();
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Point") && gradientPoints.size() < 16)
				{
					float newTime = gradientPoints.empty() ? 0.5f :
						(gradientPoints.back().first + 0.1f > 1.0f ? 0.5f : gradientPoints.back().first + 0.1f);
					gradientPoints.emplace_back(newTime, Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f));
				}

				if (ImGui::Button("Apply Gradient"))
				{
					std::sort(gradientPoints.begin(), gradientPoints.end(),
						[](const auto& a, const auto& b) { return a.first < b.first; });

					colorModule->SetColorGradient(gradientPoints);
				}

				ImGui::TreePop();
			}
		}
		// 이산 색상 모드
		else if (currentTransitionMode == 1)
		{
			if (ImGui::TreeNode("Discrete Colors"))
			{
				static std::vector<Mathf::Vector4> discreteColors = {
					Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f),
					Mathf::Vector4(0.0f, 1.0f, 0.0f, 1.0f),
					Mathf::Vector4(0.0f, 0.0f, 1.0f, 1.0f)
				};

				for (int i = 0; i < discreteColors.size(); ++i)
				{
					ImGui::PushID(i);

					char label[32];
					sprintf_s(label, "Color %d", i);

					float color[4] = {
						discreteColors[i].x,
						discreteColors[i].y,
						discreteColors[i].z,
						discreteColors[i].w
					};

					if (ImGui::ColorEdit4(label, color))
					{
						discreteColors[i] = Mathf::Vector4(color[0], color[1], color[2], color[3]);
					}

					ImGui::SameLine();
					if (ImGui::Button("Remove") && discreteColors.size() > 1)
					{
						discreteColors.erase(discreteColors.begin() + i);
						ImGui::PopID();
						break;
					}

					ImGui::PopID();
				}

				if (ImGui::Button("Add Color") && discreteColors.size() < 32)
				{
					discreteColors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
				}

				if (ImGui::Button("Apply Colors"))
				{
					colorModule->SetDiscreteColors(discreteColors);
				}

				ImGui::TreePop();
			}
		}
		// 커스텀 함수 모드
		else if (currentTransitionMode == 2)
		{
			if (ImGui::TreeNode("Custom Function"))
			{
				static const char* functionTypes[] = {
					"Pulse",
					"Sine Wave",
					"Flicker"
				};

				static int currentFunction = 0;
				ImGui::Combo("Function Type", &currentFunction, functionTypes, IM_ARRAYSIZE(functionTypes));

				if (currentFunction == 0) // Pulse
				{
					static float baseColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
					static float pulseColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
					static float frequency = 1.0f;

					ImGui::ColorEdit4("Base Color", baseColor);
					ImGui::ColorEdit4("Pulse Color", pulseColor);
					ImGui::SliderFloat("Frequency", &frequency, 0.1f, 10.0f);

					if (ImGui::Button("Apply Pulse"))
					{
						colorModule->SetPulseColor(
							Mathf::Vector4(baseColor[0], baseColor[1], baseColor[2], baseColor[3]),
							Mathf::Vector4(pulseColor[0], pulseColor[1], pulseColor[2], pulseColor[3]),
							frequency
						);
					}
				}
				else if (currentFunction == 1) // Sine Wave
				{
					static float color1[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
					static float color2[4] = { 0.0f, 0.0f, 1.0f, 1.0f };
					static float frequency = 1.0f;
					static float phase = 0.0f;

					ImGui::ColorEdit4("Color 1", color1);
					ImGui::ColorEdit4("Color 2", color2);
					ImGui::SliderFloat("Frequency", &frequency, 0.1f, 10.0f);
					ImGui::SliderFloat("Phase", &phase, 0.0f, 6.28f);

					if (ImGui::Button("Apply Sine Wave"))
					{
						colorModule->SetSineWaveColor(
							Mathf::Vector4(color1[0], color1[1], color1[2], color1[3]),
							Mathf::Vector4(color2[0], color2[1], color2[2], color2[3]),
							frequency,
							phase
						);
					}
				}
				else if (currentFunction == 2) // Flicker
				{
					static std::vector<Mathf::Vector4> flickerColors = {
						Mathf::Vector4(1.0f, 1.0f, 1.0f, 1.0f),
						Mathf::Vector4(1.0f, 1.0f, 0.0f, 1.0f),
						Mathf::Vector4(1.0f, 0.0f, 0.0f, 1.0f)
					};
					static float speed = 5.0f;

					for (int i = 0; i < flickerColors.size(); ++i)
					{
						ImGui::PushID(i);

						char label[32];
						sprintf_s(label, "Flicker Color %d", i);

						float color[4] = {
							flickerColors[i].x,
							flickerColors[i].y,
							flickerColors[i].z,
							flickerColors[i].w
						};

						if (ImGui::ColorEdit4(label, color))
						{
							flickerColors[i] = Mathf::Vector4(color[0], color[1], color[2], color[3]);
						}

						ImGui::SameLine();
						if (ImGui::Button("Remove") && flickerColors.size() > 1)
						{
							flickerColors.erase(flickerColors.begin() + i);
							ImGui::PopID();
							break;
						}

						ImGui::PopID();
					}

					if (ImGui::Button("Add Flicker Color") && flickerColors.size() < 32)
					{
						flickerColors.emplace_back(1.0f, 1.0f, 1.0f, 1.0f);
					}

					ImGui::SliderFloat("Flicker Speed", &speed, 0.1f, 20.0f);

					if (ImGui::Button("Apply Flicker"))
					{
						colorModule->SetFlickerColor(flickerColors, speed);
					}
				}

				ImGui::TreePop();
			}
		}

		ImGui::Separator();

		// 이징 설정
		if (ImGui::TreeNode("Easing Settings"))
		{
			static bool easingEnabled = false;
			ImGui::Checkbox("Enable Easing", &easingEnabled);

			if (easingEnabled)
			{
				static const char* easingTypes[] = {
					"Linear", "InSine", "OutSine", "InOutSine",
					"InQuad", "OutQuad", "InOutQuad",
					"InCubic", "OutCubic", "InOutCubic",
					"InQuart", "OutQuart", "InOutQuart",
					"InQuint", "OutQuint", "InOutQuint",
					"InExpo", "OutExpo", "InOutExpo",
					"InCirc", "OutCirc", "InOutCirc",
					"InBack", "OutBack", "InOutBack",
					"InElastic", "OutElastic", "InOutElastic",
					"InBounce", "OutBounce", "InOutBounce"
				};

				static const char* animationTypes[] = {
					"Once Forward", "Once Back", "Once PingPong",
					"Loop Forward", "Loop Back", "Loop PingPong"
				};

				static int currentEasingType = 0;
				static int currentAnimationType = 0;
				static float duration = 1.0f;

				ImGui::Combo("Easing Type", &currentEasingType, easingTypes, IM_ARRAYSIZE(easingTypes));
				ImGui::Combo("Animation Type", &currentAnimationType, animationTypes, IM_ARRAYSIZE(animationTypes));
				ImGui::SliderFloat("Duration", &duration, 0.1f, 10.0f);

				if (ImGui::Button("Apply Easing"))
				{
					colorModule->SetEasing(
						static_cast<EasingEffect>(currentEasingType),
						static_cast<StepAnimation>(currentAnimationType),
						duration
					);
				}
			}

			ImGui::TreePop();
		}
	}
}

void EffectEditor::RenderMeshModuleGPUEditor(MeshModuleGPU* meshModule)
{
	if (!meshModule) return;

	ImGui::Text("Mesh Render Module Settings");

	// 셰이더 선택 UI
	RenderShaderSelectionUI(meshModule);

	// 렌더 상태 선택 UI  
	RenderStateSelectionUI(meshModule);

	// 다중 텍스처 선택 UI
	RenderTextureSelectionUI(meshModule);

	ImGui::Separator();
	ImGui::Text("Mesh Specific Settings:");

	// 메시 타입 설정
	MeshType currentMeshType = meshModule->GetMeshType();
	int meshTypeIndex = static_cast<int>(currentMeshType);
	const char* meshTypes[] = { "None", "Cube", "Sphere", "Model" };

	if (ImGui::Combo("Mesh Type", &meshTypeIndex, meshTypes, IM_ARRAYSIZE(meshTypes))) {
		meshModule->SetMeshType(static_cast<MeshType>(meshTypeIndex));
	}

	// 현재 로드된 모델 정보를 상단에 표시
	if (currentMeshType == MeshType::Model) {
		Model* currentModel = meshModule->GetCurrentModel();
		if (currentModel) {
			ImGui::Separator();
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.5f, 1.0f), "Current Model: %s", currentModel->name.c_str());

			int meshIndex = meshModule->GetCurrentMeshIndex();
			if (meshIndex >= 0 && meshIndex < currentModel->m_numTotalMeshes) {
				Mesh* currentMesh = currentModel->GetMesh(meshIndex);
				if (currentMesh) {
					ImGui::Text("Using Mesh: %s (Index: %d)", currentMesh->GetName().c_str(), meshIndex);
					ImGui::Text("Vertices: %zu | Indices: %zu",
						currentMesh->GetVertices().size(),
						currentMesh->GetIndices().size());
				}
			}
			ImGui::Separator();
		}
		else {
			ImGui::Separator();
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No Model Loaded");
			ImGui::Separator();
		}
	}

	// Model 타입일 때 모델 설정 UI
	if (currentMeshType == MeshType::Model) {
		ImGui::Text("Model Settings:");

		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Drag & Drop model files here!");
		ImGui::Text("Supported formats: FBX, OBJ, DAE, GLTF");
		ImGui::Text("Or use manual load:");

		// 현재 로드된 모델이 있으면 해당 이름을 기본값으로 설정
		static char modelFilePath[256] = "";
		static bool modelPathInitialized = false;

		Model* currentModel = meshModule->GetCurrentModel();
		if (currentModel && !modelPathInitialized) {
			strcpy_s(modelFilePath, sizeof(modelFilePath), currentModel->name.c_str());
			modelPathInitialized = true;
		}

		ImGui::InputText("Model File Path", modelFilePath, sizeof(modelFilePath));

		ImGui::SameLine();
		if (ImGui::Button("Load Model")) {
			if (strlen(modelFilePath) > 0) {
				Model* loadedModel = DataSystems->LoadCashedModel(modelFilePath);
				if (loadedModel) {
					meshModule->SetModel(loadedModel, 0);
					std::cout << "Model loaded: " << modelFilePath << std::endl;
				}
				else {
					std::cout << "Failed to load model: " << modelFilePath << std::endl;
				}
			}
		}

		// 현재 모델이 설정되어 있다면 메시 선택 UI 표시
		if (currentModel && currentModel->m_numTotalMeshes > 0) {
			ImGui::Separator();
			ImGui::Text("Mesh Selection for %s:", currentModel->name.c_str());

			// 현재 모듈의 실제 메시 인덱스를 가져와서 UI에 반영
			int currentMeshIndex = meshModule->GetCurrentMeshIndex();
			static int selectedMeshIndex = 0;

			// 실제 메시 인덱스와 UI 인덱스 동기화
			if (selectedMeshIndex != currentMeshIndex) {
				selectedMeshIndex = currentMeshIndex;
			}

			if (selectedMeshIndex >= currentModel->m_numTotalMeshes) {
				selectedMeshIndex = 0;
			}

			// 메시 인덱스 선택
			if (ImGui::SliderInt("Mesh Index", &selectedMeshIndex, 0, currentModel->m_numTotalMeshes - 1)) {
				meshModule->SetModel(currentModel, selectedMeshIndex);
			}

			// 현재 선택된 메시 정보 표시
			Mesh* currentMesh = currentModel->GetMesh(selectedMeshIndex);
			if (currentMesh) {
				ImGui::Text("Selected Mesh: %s", currentMesh->GetName().c_str());
				ImGui::Text("Vertices: %zu | Indices: %zu",
					currentMesh->GetVertices().size(),
					currentMesh->GetIndices().size());
			}

			// 메시 이름으로 선택하는 UI
			ImGui::Separator();
			ImGui::Text("Or select by name:");

			static char meshName[256] = "";
			ImGui::InputText("Mesh Name", meshName, sizeof(meshName));

			ImGui::SameLine();
			if (ImGui::Button("Select Mesh")) {
				if (strlen(meshName) > 0) {
					meshModule->SetModel(currentModel, std::string_view(meshName));
					// 선택된 메시의 인덱스를 UI에 반영
					selectedMeshIndex = meshModule->GetCurrentMeshIndex();
				}
			}

			// 사용 가능한 메시 목록 표시
			if (ImGui::CollapsingHeader("Available Meshes")) {
				for (int i = 0; i < currentModel->m_numTotalMeshes; ++i) {
					Mesh* mesh = currentModel->GetMesh(i);
					if (mesh) {
						bool isCurrentMesh = (i == meshModule->GetCurrentMeshIndex());
						if (isCurrentMesh) {
							ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f),
								"[%d] %s (CURRENT)", i, mesh->GetName().c_str());
						}
						else {
							ImGui::Text("[%d] %s", i, mesh->GetName().c_str());
						}

						ImGui::SameLine();
						ImGui::PushID(i);
						if (ImGui::SmallButton("Select")) {
							meshModule->SetModel(currentModel, i);
							selectedMeshIndex = i; // UI 인덱스도 업데이트
						}
						ImGui::PopID();
					}
				}
			}
		}

		// 모델이 바뀌면 초기화 플래그 리셋
		static Model* lastModel = nullptr;
		if (lastModel != currentModel) {
			lastModel = currentModel;
			modelPathInitialized = false;
		}
	}

	ImGui::Separator();

	// 파티클 데이터 설정 정보 표시
	ImGui::Text("Particle Data Info:");

	UINT instanceCount = meshModule->GetInstanceCount();
	ImGui::Text("Instance Count: %u", instanceCount);

	if (instanceCount == 0) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No particle data assigned");
		ImGui::Text("This will be automatically set when connected to a particle system");
	}

	// 요약 상태 정보 (하단)
	ImGui::Separator();
	ImGui::Text("Summary:");
	ImGui::Text("Mesh Type: %s", meshTypeIndex < 4 ? meshTypes[meshTypeIndex] : "Unknown");

	if (currentMeshType == MeshType::Model) {
		Model* model = meshModule->GetCurrentModel();
		if (model) {
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Model File: %s", model->name.c_str());
			int meshIndex = meshModule->GetCurrentMeshIndex();
			Mesh* currentMesh = model->GetMesh(meshIndex);
			if (currentMesh) {
				ImGui::Text("Active Mesh: %s", currentMesh->GetName().c_str());
			}
		}
		else {
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No model file loaded");
		}
	}
	else if (currentMeshType == MeshType::Cube) {
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Built-in Cube Mesh");
	}
	else if (currentMeshType == MeshType::Sphere) {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.5f, 1.0f), "Built-in Sphere (not implemented)");
	}
	else {
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "No mesh selected");
	}

	// 클리핑 설정 UI
	ImGui::Separator();
	ImGui::Text("Polar Clipping Settings:");

	if (meshModule->SupportsClipping()) {
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.8f, 1.0f), "Polar Clipping Supported");

		// Polar 클리핑 활성화/비활성화
		bool polarClippingEnabled = meshModule->IsPolarClippingEnabled();
		if (ImGui::Checkbox("Enable Polar Clipping", &polarClippingEnabled)) {
			meshModule->EnablePolarClipping(polarClippingEnabled);
		}

		if (polarClippingEnabled) {
			ImGui::Indent();

			// Polar 각도 진행도
			const auto& polarParams = meshModule->GetPolarClippingParams();
			float polarProgress = polarParams.polarAngleProgress;
			if (ImGui::SliderFloat("Angle Progress", &polarProgress, 0.0f, 1.0f, "%.2f")) {
				meshModule->SetPolarAngleProgress(polarProgress);
			}

			float progressInDegrees = polarProgress * 360.0f;
			ImGui::SameLine();
			ImGui::Text("(%.0f°)", progressInDegrees);

			// 극좌표 중심점
			float polarCenter[3] = {
				polarParams.polarCenter.x,
				polarParams.polarCenter.y,
				polarParams.polarCenter.z
			};
			if (ImGui::DragFloat3("Center", polarCenter, 0.1f)) {
				meshModule->SetPolarCenter(Mathf::Vector3(polarCenter[0], polarCenter[1], polarCenter[2]));
			}

			// 기준 방향 설정
			float polarReferenceDir[3] = {
				polarParams.polarReferenceDir.x,
				polarParams.polarReferenceDir.y,
				polarParams.polarReferenceDir.z
			};
			if (ImGui::DragFloat3("Reference Direction", polarReferenceDir, 0.01f, -1.0f, 1.0f)) {
				meshModule->SetPolarReferenceDirection(Mathf::Vector3(polarReferenceDir[0], polarReferenceDir[1], polarReferenceDir[2]));
			}

			// 기준 방향 프리셋
			ImGui::Text("Quick Reference:");
			if (ImGui::Button("+X")) meshModule->SetPolarReferenceDirection(Mathf::Vector3(1, 0, 0));
			ImGui::SameLine();
			if (ImGui::Button("-X")) meshModule->SetPolarReferenceDirection(Mathf::Vector3(-1, 0, 0));
			ImGui::SameLine();
			if (ImGui::Button("+Z")) meshModule->SetPolarReferenceDirection(Mathf::Vector3(0, 0, 1));
			ImGui::SameLine();
			if (ImGui::Button("-Z")) meshModule->SetPolarReferenceDirection(Mathf::Vector3(0, 0, -1));

			// Up 축
			float polarUpAxis[3] = {
				polarParams.polarUpAxis.x,
				polarParams.polarUpAxis.y,
				polarParams.polarUpAxis.z
			};
			if (ImGui::DragFloat3("Up Axis", polarUpAxis, 0.01f, -1.0f, 1.0f)) {
				meshModule->SetPolarUpAxis(Mathf::Vector3(polarUpAxis[0], polarUpAxis[1], polarUpAxis[2]));
			}

			// Up 축 프리셋
			ImGui::Text("Up Axis:");
			if (ImGui::Button("Y-Up")) meshModule->SetPolarUpAxis(Mathf::Vector3(0, 1, 0));
			ImGui::SameLine();
			if (ImGui::Button("X-Up")) meshModule->SetPolarUpAxis(Mathf::Vector3(1, 0, 0));
			ImGui::SameLine();
			if (ImGui::Button("Z-Up")) meshModule->SetPolarUpAxis(Mathf::Vector3(0, 0, 1));

			// 시작 각도
			float startAngleRadians = polarParams.polarStartAngle;
			float startAngleDegrees = startAngleRadians * 180.0f / 3.14159265f;
			if (ImGui::SliderFloat("Start Angle", &startAngleDegrees, 0.0f, 360.0f, "%.0f°")) {
				startAngleRadians = startAngleDegrees * 3.14159265f / 180.0f;
				meshModule->SetPolarStartAngle(startAngleRadians);
			}

			// 회전 방향
			float direction = polarParams.polarDirection;
			bool clockwise = (direction > 0.0f);
			if (ImGui::Checkbox("Clockwise", &clockwise)) {
				meshModule->SetPolarDirection(clockwise ? 1.0f : -1.0f);
			}

			// 애니메이션
			ImGui::Separator();
			bool animatePolarClipping = meshModule->IsPolarClippingAnimating();
			if (ImGui::Checkbox("Animate", &animatePolarClipping)) {
				meshModule->SetPolarClippingAnimation(animatePolarClipping, meshModule->GetPolarClippingAnimationSpeed());
			}

			if (animatePolarClipping) {
				float polarAnimationSpeed = meshModule->GetPolarClippingAnimationSpeed();
				if (ImGui::SliderFloat("Speed", &polarAnimationSpeed, 0.1f, 5.0f)) {
					meshModule->SetPolarClippingAnimation(true, polarAnimationSpeed);
				}
			}

			// 프리셋
			ImGui::Separator();
			ImGui::Text("Presets:");
			if (ImGui::Button("Radar Sweep")) {
				meshModule->SetPolarCenter(Mathf::Vector3::Zero);
				meshModule->SetPolarReferenceDirection(Mathf::Vector3(1, 0, 0));
				meshModule->SetPolarUpAxis(Mathf::Vector3(0, 1, 0));
				meshModule->SetPolarDirection(1.0f);
				meshModule->SetPolarClippingAnimation(true, 1.0f);
			}
			ImGui::SameLine();
			if (ImGui::Button("Half Circle")) {
				meshModule->SetPolarAngleProgress(0.5f);
				meshModule->SetPolarStartAngle(0.0f);
			}

			// 도움말
			if (ImGui::CollapsingHeader("Help")) {
				ImGui::BulletText("Progress: 0.0 = point, 1.0 = full circle");
				ImGui::BulletText("Reference Direction: 0° starting point");
				ImGui::BulletText("Up Axis: rotation plane normal");
				ImGui::BulletText("Start Angle: rotates the boundary");
			}

			ImGui::Unindent();
		}
	}
	else {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Polar Clipping Not Supported");
	}
}

void EffectEditor::RenderTrailGenerateModuleEditor(TrailGenerateModule* trailModule)
{
	if (!trailModule)
		return;

	ImGui::PushID("TrailGenerateModule");

	// 모듈 활성화/비활성화
	bool enabled = trailModule->IsEnabled();
	if (ImGui::Checkbox("Enable Trail Generation", &enabled)) {
		trailModule->SetEnabled(enabled);
	}

	if (!enabled) {
		ImGui::PopID();
		return;
	}

	ImGui::Separator();

	// 기본 설정
	if (ImGui::CollapsingHeader("Basic Settings", ImGuiTreeNodeFlags_DefaultOpen))
	{
		int maxPoints = trailModule->GetMaxTrailPoints();
		if (ImGui::SliderInt("Max Trail Points", &maxPoints, 10, 500)) {
			trailModule->SetMaxTrailPoints(maxPoints);
		}

		float lifetime = trailModule->GetTrailLifetime();
		if (ImGui::SliderFloat("Trail Lifetime", &lifetime, 0.0f, 10.0f, "%.2f sec")) {
			trailModule->SetTrailLifetime(lifetime);
		}
		ImGui::SameLine();
		if (ImGui::Button("Infinite")) {
			trailModule->SetTrailLifetime(999999.0f);
		}

		float minDistance = trailModule->GetMinDistance();
		if (ImGui::SliderFloat("Min Distance", &minDistance, 0.001f, 1.0f, "%.4f")) {
			trailModule->SetMinDistance(minDistance);
		}

		// 현재 시간 표시 및 조정
		float currentTime = trailModule->GetCurrentTime();
		if (ImGui::SliderFloat("Current Time", &currentTime, 0.0f, 100.0f, "%.2f")) {
			trailModule->SetCurrentTime(currentTime);
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Time")) {
			trailModule->SetCurrentTime(0.0f);
		}
	}

	// 자동 생성 설정
	if (ImGui::CollapsingHeader("Auto Generation"))
	{
		bool autoGenerate = trailModule->GetAutoGenerateFromPosition();
		float interval = trailModule->GetAutoAddInterval();

		if (ImGui::Checkbox("Auto Generate from Position", &autoGenerate)) {
			trailModule->SetAutoGenerationSettings(autoGenerate, interval);
		}

		if (autoGenerate) {
			if (ImGui::SliderFloat("Add Interval", &interval, 0.001f, 0.5f, "%.4f sec")) {
				trailModule->SetAutoGenerationSettings(autoGenerate, interval);
			}

			// 현재 위치 설정
			Mathf::Vector3 currentPos = trailModule->GetPosition();
			float posArray[3] = { currentPos.x, currentPos.y, currentPos.z };
			if (ImGui::DragFloat3("Current Position", posArray, 0.1f)) {
				trailModule->SetEmitterPosition(Mathf::Vector3(posArray[0], posArray[1], posArray[2]));
			}

			// 위치 오프셋
			Mathf::Vector3 offset = trailModule->GetPositionOffset();
			float offsetArray[3] = { offset.x, offset.y, offset.z };
			if (ImGui::DragFloat3("Position Offset", offsetArray, 0.1f)) {
				trailModule->SetPositionOffset(Mathf::Vector3(offsetArray[0], offsetArray[1], offsetArray[2]));
			}
		}
	}

	// 외형 설정
	if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
	{
		// 너비 설정
		float startWidth = trailModule->GetStartWidth();
		float endWidth = trailModule->GetEndWidth();

		if (ImGui::SliderFloat("Start Width", &startWidth, 0.01f, 20.0f, "%.3f")) {
			trailModule->SetStartWidth(startWidth);
		}
		if (ImGui::SliderFloat("End Width", &endWidth, 0.001f, 10.0f, "%.4f")) {
			trailModule->SetEndWidth(endWidth);
		}

		// 한번에 설정 버튼
		ImGui::SameLine();
		if (ImGui::Button("Set Both")) {
			trailModule->SetWidthCurve(startWidth, endWidth);
		}

		// 색상 설정
		Mathf::Vector4 startColorVec = trailModule->GetStartColor();
		Mathf::Vector4 endColorVec = trailModule->GetEndColor();

		float startColor[4] = { startColorVec.x, startColorVec.y, startColorVec.z, startColorVec.w };
		float endColor[4] = { endColorVec.x, endColorVec.y, endColorVec.z, endColorVec.w };

		if (ImGui::ColorEdit4("Start Color", startColor)) {
			trailModule->SetStartColor(Mathf::Vector4(startColor[0], startColor[1], startColor[2], startColor[3]));
		}
		if (ImGui::ColorEdit4("End Color", endColor)) {
			trailModule->SetEndColor(Mathf::Vector4(endColor[0], endColor[1], endColor[2], endColor[3]));
		}

		// 프리셋 색상
		if (ImGui::Button("White to Transparent")) {
			trailModule->SetColorCurve(Mathf::Vector4(1, 1, 1, 1), Mathf::Vector4(1, 1, 1, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button("Red Trail")) {
			trailModule->SetColorCurve(Mathf::Vector4(1, 0, 0, 1), Mathf::Vector4(1, 0, 0, 0));
		}
		if (ImGui::Button("Blue Trail")) {
			trailModule->SetColorCurve(Mathf::Vector4(0, 0.5f, 1, 1), Mathf::Vector4(0, 0.2f, 1, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button("Gold Trail")) {
			trailModule->SetColorCurve(Mathf::Vector4(1, 0.8f, 0, 1), Mathf::Vector4(1, 0.5f, 0, 0));
		}
	}

	// UV 및 렌더링 설정
	if (ImGui::CollapsingHeader("Rendering Settings"))
	{
		bool useLengthBasedUV = trailModule->IsUsingLengthBasedUV();
		if (ImGui::Checkbox("Use Length Based UV", &useLengthBasedUV)) {
			trailModule->SetUVMode(useLengthBasedUV);
		}

		// Up Vector 설정
		Mathf::Vector3 upVector = trailModule->GetLastUpVector();
		float upArray[3] = { upVector.x, upVector.y, upVector.z };
		if (ImGui::DragFloat3("Up Vector", upArray, 0.1f, -1.0f, 1.0f)) {
			trailModule->SetLastUpVector(Mathf::Vector3(upArray[0], upArray[1], upArray[2]));
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Up")) {
			trailModule->SetLastUpVector(Mathf::Vector3(0.0f, 1.0f, 0.0f));
		}

		// 메시 상태
		bool isDirty = trailModule->IsMeshDirty();
		ImGui::Text("Mesh Status: %s", isDirty ? "Dirty (needs update)" : "Clean");
		if (isDirty && ImGui::Button("Force Update Mesh")) {
			trailModule->ForceUpdateMesh();
		}
	}

	// 수동 제어
	if (ImGui::CollapsingHeader("Manual Control"))
	{
		static float testPos[3] = { 0.0f, 0.0f, 0.0f };
		static float testWidth = 1.0f;
		static float testColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

		ImGui::DragFloat3("Test Position", testPos, 0.1f);
		ImGui::SliderFloat("Test Width", &testWidth, 0.1f, 10.0f);
		ImGui::ColorEdit4("Test Color", testColor);

		if (ImGui::Button("Add Point")) {
			trailModule->AddPoint(
				Mathf::Vector3(testPos[0], testPos[1], testPos[2]),
				testWidth,
				Mathf::Vector4(testColor[0], testColor[1], testColor[2], testColor[3])
			);
		}

		ImGui::SameLine();
		if (ImGui::Button("Add Random Point")) {
			static float time = 0.0f;
			time += 1.0f;
			float x = sinf(time * 0.5f) * 5.0f;
			float y = cosf(time * 0.3f) * 3.0f;
			float z = sinf(time * 0.7f) * 2.0f;

			trailModule->AddPoint(
				Mathf::Vector3(x, y, z),
				testWidth,
				Mathf::Vector4(testColor[0], testColor[1], testColor[2], testColor[3])
			);
		}

		if (ImGui::Button("Add Line Trail")) {
			trailModule->Clear();
			for (int i = 0; i < 10; ++i) {
				trailModule->AddPoint(
					Mathf::Vector3(i * 0.5f, 0.0f, 0.0f),
					testWidth,
					Mathf::Vector4(testColor[0], testColor[1], testColor[2], testColor[3])
				);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button("Add Circle Trail")) {
			trailModule->Clear();
			for (int i = 0; i < 20; ++i) {
				float angle = (i / 20.0f) * 6.28318f;
				trailModule->AddPoint(
					Mathf::Vector3(cosf(angle) * 3.0f, sinf(angle) * 3.0f, 0.0f),
					testWidth,
					Mathf::Vector4(testColor[0], testColor[1], testColor[2], testColor[3])
				);
			}
		}

		// 수명 관리
		ImGui::Separator();
		if (ImGui::Button("Remove Old Points")) {
			trailModule->RemoveOldPoints();
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove Very Old")) {
			trailModule->RemoveOldPoints(1.0f);  // 1초 이상된 포인트만 제거
		}
	}

	// 디버그 정보
	if (ImGui::CollapsingHeader("Debug Info"))
	{
		ImGui::Text("Active Points: %zu", trailModule->GetActivePointCount());
		ImGui::Text("Vertex Count: %d", trailModule->GetVertexCount());
		ImGui::Text("Index Count: %d", trailModule->GetIndexCount());
		ImGui::Text("Has Valid Mesh: %s", trailModule->HasValidMesh() ? "Yes" : "No");
		ImGui::Text("Is Initialized: %s", trailModule->IsInitialized() ? "Yes" : "No");

		// 마지막 위치 표시
		Mathf::Vector3 lastPos = trailModule->GetLastPosition();
		ImGui::Text("Last Position: (%.2f, %.2f, %.2f)", lastPos.x, lastPos.y, lastPos.z);

		ImGui::Separator();

		if (ImGui::Button("Clear Trail")) {
			trailModule->Clear();
		}
		ImGui::SameLine();
		if (ImGui::Button("Reset Module")) {
			trailModule->ResetForReuse();
		}
		ImGui::SameLine();
		if (ImGui::Button("Force Mesh Dirty")) {
			trailModule->SetMeshDirty(true);
		}

		// 설정 프리셋
		ImGui::Separator();
		ImGui::Text("Quick Presets:");

		if (ImGui::Button("Dash Trail")) {
			trailModule->SetTrailLifetime(0.5f);
			trailModule->SetMinDistance(0.01f);
			trailModule->SetWidthCurve(3.0f, 0.1f);
			trailModule->SetColorCurve(Mathf::Vector4(0, 0.7f, 1, 1), Mathf::Vector4(0, 0.3f, 1, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button("Long Trail")) {
			trailModule->SetTrailLifetime(5.0f);
			trailModule->SetMinDistance(0.05f);
			trailModule->SetWidthCurve(1.0f, 0.3f);
			trailModule->SetColorCurve(Mathf::Vector4(1, 1, 1, 0.8f), Mathf::Vector4(1, 1, 1, 0));
		}

		if (ImGui::Button("Thick Trail")) {
			trailModule->SetTrailLifetime(2.0f);
			trailModule->SetMinDistance(0.1f);
			trailModule->SetWidthCurve(5.0f, 2.0f);
			trailModule->SetColorCurve(Mathf::Vector4(1, 0.5f, 0, 1), Mathf::Vector4(1, 0, 0, 0));
		}
		ImGui::SameLine();
		if (ImGui::Button("Thin Trail")) {
			trailModule->SetTrailLifetime(1.0f);
			trailModule->SetMinDistance(0.01f);
			trailModule->SetWidthCurve(0.5f, 0.1f);
			trailModule->SetColorCurve(Mathf::Vector4(1, 1, 0, 1), Mathf::Vector4(1, 1, 0, 0));
		}
	}

	ImGui::PopID();
}

void EffectEditor::RenderTrailRenderModuleEditor(TrailRenderModule* trailRenderModule)
{
	if (!trailRenderModule) return;

	ImGui::Text("Trail Render Module Settings");

	// 새로운 통합 UI들
	RenderShaderSelectionUI(trailRenderModule);
	RenderStateSelectionUI(trailRenderModule);
	RenderTextureSelectionUI(trailRenderModule);

	ImGui::Separator();
	ImGui::Text("Trail Specific Settings:");

	// 트레일 생성 모듈 연결 상태
	TrailGenerateModule* connectedTrailModule = trailRenderModule->GetTrailGenerateModule();
	if (connectedTrailModule) {
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Connected to Trail Generate Module");
	}
	else {
		ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Not connected to Trail Generate Module");
		ImGui::Text("Trail rendering will not work without connection");
	}

	ImGui::Separator();

	// 카메라 위치 설정
	ImGui::Text("Camera Settings");

	auto currentCameraPos = trailRenderModule->GetCameraPosition();
	float cameraPos[3] = { currentCameraPos.x, currentCameraPos.y, currentCameraPos.z };

	if (ImGui::DragFloat3("Camera Position", cameraPos, 0.1f)) {
		trailRenderModule->SetCameraPosition(Mathf::Vector3(cameraPos[0], cameraPos[1], cameraPos[2]));
	}

	ImGui::Separator();

	// 렌더링 상태 정보
	ImGui::Text("Rendering Status");

	bool isEnabled = trailRenderModule->IsEnabled();
	if (ImGui::Checkbox("Enable Rendering", &isEnabled)) {
		trailRenderModule->SetEnabled(isEnabled);
	}

	if (trailRenderModule->IsReadyForReuse()) {
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Ready for rendering");
	}
	else {
		ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Not ready (GPU work pending)");
	}

	ImGui::Separator();

	// 디버그 정보
	if (ImGui::CollapsingHeader("Debug Info")) {
		ImGui::Text("Module Address: %p", trailRenderModule);
		ImGui::Text("Connected Trail Module: %p", connectedTrailModule);
		ImGui::Text("PSO Valid: %s", trailRenderModule->GetPSO() ? "Yes" : "No");

		if (connectedTrailModule) {
			ImGui::Text("Trail Vertex Buffer: %p", connectedTrailModule->GetVertexBuffer());
			ImGui::Text("Trail Index Buffer: %p", connectedTrailModule->GetIndexBuffer());
		}
	}

	// 도움말
	if (ImGui::CollapsingHeader("Help")) {
		ImGui::BulletText("Trail Render Module displays trails generated by Trail Generate Module");
		ImGui::BulletText("Both modules must be added to the same ParticleSystem");
		ImGui::BulletText("Connection happens automatically when both modules are present");
		ImGui::BulletText("Use texture slots to assign different textures for trail effects");
		ImGui::BulletText("Camera position affects lighting calculations");
		ImGui::BulletText("Adjust shader and render states for different visual effects");
	}
}

void EffectEditor::RenderUnifiedDragDropTarget()
{
	// 현재 창의 전체 영역을 드래그 드롭 타겟으로 설정
	ImVec2 availSize = ImGui::GetContentRegionAvail();
	ImVec2 windowPos = ImGui::GetCursorScreenPos();
	ImRect dropRect(windowPos, ImVec2(windowPos.x + availSize.x, windowPos.y + availSize.y));

	if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("UnifiedDropTarget")))
	{
		// 텍스처 파일 드롭 처리
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

		// 모델 파일 드롭 처리
		else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("Model"))
		{
			const char* droppedFilePath = (const char*)payload->Data;
			file::path filename = droppedFilePath;

			// 모델 파일인지 확인
			std::string extension = filename.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

			if (extension == ".fbx" || extension == ".obj" || extension == ".dae" ||
				extension == ".gltf" || extension == ".glb") {

				// LoadCashedModel로 로드 (자동으로 캐시 관리됨)
				Model* loadedModel = DataSystems->LoadCashedModel(filename.filename().string());

				if (loadedModel) {
					// 현재 편집 중인 MeshModule에 자동 할당
					if (m_modifyingSystem) {
						auto& renderModules = m_modifyingSystem->GetRenderModules();
						for (auto& renderModule : renderModules) {
							if (auto* meshModule = dynamic_cast<MeshModuleGPU*>(renderModule)) {
								meshModule->SetMeshType(MeshType::Model);
								meshModule->SetModel(loadedModel, 0);
								std::cout << "Model assigned to MeshModule: " << filename.filename().string() << std::endl;
								break;
							}
						}
					}

					std::cout << "Model loaded successfully: " << filename.filename().string() << std::endl;
				}
				else {
					std::cout << "Failed to load model: " << filename.string() << std::endl;
				}
			}
			else {
				std::cout << "Unsupported model format: " << extension << std::endl;
				std::cout << "Supported formats: .fbx, .obj, .dae, .gltf, .glb" << std::endl;
			}
		}

		ImGui::EndDragDropTarget();
	}
}