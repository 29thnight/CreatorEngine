#include "RenderComponent.h"
#include "Object.h"
#include "InputManager.h"
#include "RenderEngine/ImGuiRegister.h"

static uint32 id;
//
//RenderComponent::RenderComponent(Scene* scene) : 
//	_targetScene(scene), 
//	_instancedModel(nullptr)
//{
//}
//
//RenderComponent::~RenderComponent()
//{
//	if (_instancedModel)
//	{
//		_targetScene->RemoveModel(_instancedName);
//	}
//
//	if (_subInstancedModel)
//	{
//		_targetScene->RemoveModel(_subModelName);
//	}
//}
//
//void RenderComponent::Update(float tick)
//{
//	if (!_isDeSerialize)
//	{
//		if (AssetsSystem->Models[_modelName])
//		{
//			_instancedModel = _targetScene->AddModel(_instancedName, AssetsSystem->Models[_modelName]);
//			_instancedModel->position = _notYetSerializeData.position;
//			_instancedModel->SetRotation(_notYetSerializeData.rotation.x, _notYetSerializeData.rotation.y, _notYetSerializeData.rotation.z);
//			_instancedModel->scale = _notYetSerializeData.scale;
//			_instancedModel->outline.color = _notYetSerializeData.outlineColor;
//			_instancedModel->outline.oulineThickness = _notYetSerializeData.outlineThickness;
//			_instancedID = _instancedModel->instancedID;
//			_isDeSerialize = true;
//			progress = 1.0f;
//			ImGui::ContextUnregister("LoadingModel");
//		}
//		else if (AssetsSystem->AnimatedModels[_modelName])
//		{
//			_instancedModel = _targetScene->AddModel(_instancedName, AssetsSystem->AnimatedModels[_modelName]);
//			_instancedModel->position = _notYetSerializeData.position;
//			_instancedModel->SetRotation(_notYetSerializeData.rotation.x, _notYetSerializeData.rotation.y, _notYetSerializeData.rotation.z);
//			_instancedModel->scale = _notYetSerializeData.scale;
//			_instancedModel->outline.color = _notYetSerializeData.outlineColor;
//			_instancedModel->outline.oulineThickness = _notYetSerializeData.outlineThickness;
//			if (_subModelName != "")
//			{
//				_subInstancedModel = _targetScene->AddModel(_subInstancedName, AssetsSystem->AnimatedModels[_subModelName]);
//				_subInstancedModel->position = _notYetSerializeData.position;
//				_subInstancedModel->SetRotation(_notYetSerializeData.rotation.x, _notYetSerializeData.rotation.y, _notYetSerializeData.rotation.z);
//				_subInstancedModel->scale = _notYetSerializeData.scale;
//				_subInstancedModel->outline.color = _notYetSerializeData.outlineColor;
//				_subInstancedModel->outline.oulineThickness = _notYetSerializeData.outlineThickness;
//			}
//
//			if (_axeModelName != "")
//			{
//				_axeInstancedModel = _targetScene->AddModel(_axeInstancedName, AssetsSystem->AnimatedModels[_axeModelName]);
//				_axeInstancedModel->position = _notYetSerializeData.position;
//				_axeInstancedModel->SetRotation(_notYetSerializeData.rotation.x, _notYetSerializeData.rotation.y, _notYetSerializeData.rotation.z);
//				_axeInstancedModel->scale = _notYetSerializeData.scale;
//				_axeInstancedModel->outline.color = _notYetSerializeData.outlineColor;
//				_axeInstancedModel->outline.oulineThickness = _notYetSerializeData.outlineThickness;
//			}
//			_instancedID = _instancedModel->instancedID;
//			_isDeSerialize = true;
//			progress = 1.0f;
//			ImGui::ContextUnregister("LoadingModel");
//		}
//	}
//
//	if (!_instancedModel)
//	{
//		return;
//	}
//
//	_instancedModel->ownerMatrix = _owner->GetWorldMatrix();
//	_instancedModel->isVisiable = _isVisiable;
//	if (_subInstancedModel)
//	{
//		_subInstancedModel->ownerMatrix = _owner->GetWorldMatrix();
//		_subInstancedModel->isVisiable = _isSubVisiable;
//	}
//	if (_axeInstancedModel)
//	{
//		_axeInstancedModel->ownerMatrix = _owner->GetWorldMatrix();
//		_axeInstancedModel->isVisiable = _isAxeVisiable;
//	}
//	_instancedID = _instancedModel ? _instancedModel->instancedID : UINT32_MAX;
//}
//
//void RenderComponent::EditorContext()
//{
//	ImGui::Text("RenderComponent");
//	ImGui::SameLine();
//	ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 50);
//	ImGui::PushID(_instancedID);
//	if (ImGui::SmallButton("delete"))
//	{
//		_destroyMark = true;
//	}
//	ImGui::PopID();
//
//	if (!_instancedModel && !_destroyMark)
//	{
//		auto model = AssetsSystem->GetPayloadModel();
//		auto animModel = AssetsSystem->GetPayloadAnimModel();
//
//		if (!_instancedModel && (!model && !animModel))
//		{
//			ImGui::Text("Drag and Drop Model or Animated Model");
//		}
//
//		if (model && InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
//		{
//			_instancedName = model->name + std::to_string(++id);
//			_instancedModel = _targetScene->AddModel(_instancedName, model);
//			_instancedID = _instancedModel->instancedID;
//			AssetsSystem->ClearPayloadModel();
//		}
//		else if (animModel && InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
//		{
//			_instancedName = animModel->name + std::to_string(++id);
//			_instancedModel = _targetScene->AddModel(_instancedName, animModel);
//			_instancedID = _instancedModel->instancedID;
//			AssetsSystem->ClearPayloadAnimModel();
//		}
//	}
//
//	if (!_subInstancedModel && !_destroyMark)
//	{
//		auto animModel = AssetsSystem->GetPayloadAnimModel();
//
//		if (!_subInstancedModel && !animModel)
//		{
//			ImGui::Text("Drag and Drop Sub Animated Model");
//		}
//
//		if (animModel && InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
//		{
//			_subModelName = animModel->name + std::to_string(++id);
//			_subInstancedModel = _targetScene->AddModel(_subModelName, animModel);
//			AssetsSystem->ClearPayloadAnimModel();
//		}
//	}
//
//	if (!_axeInstancedModel && !_destroyMark)
//	{
//		auto animModel = AssetsSystem->GetPayloadAnimModel();
//		if (!_axeInstancedModel && !animModel)
//		{
//			ImGui::Text("Drag and Drop Axe Animated Model");
//		}
//
//		if (animModel && InputManagement->IsMouseButtonReleased(MouseKey::LEFT))
//		{
//			_axeModelName = animModel->name + std::to_string(++id);
//			_axeInstancedModel = _targetScene->AddModel(_axeModelName, animModel);
//			AssetsSystem->ClearPayloadAnimModel();
//		}
//	}
//
//	if (_instancedModel)
//	{
//		ImGui::Spacing();
//		std::string tempName = "Model" + std::to_string(_instancedID);
//		if(_instancedModel->model)
//		{
//			ImGui::Text(tempName.c_str());
//			ImGui::SameLine();
//			ImGui::PushID(_instancedID);
//			ImGui::Selectable(_instancedModel->model->name.c_str(), false);
//			ImGui::PopID();
//		}
//		else if(_instancedModel->animModel)
//		{
//			ImGui::Text(tempName.c_str());
//			ImGui::SameLine();
//			ImGui::PushID(_instancedID);
//			ImGui::Selectable(_instancedModel->animModel->name.c_str(), false);
//			ImGui::PopID();
//
//		}
//		std::string tempName2 = "R_ClickMenu" + std::to_string(_instancedID);
//		if (ImGui::BeginPopupContextItem(tempName2.c_str()))
//		{
//			if (ImGui::MenuItem("Delete"))
//			{
//				_targetScene->RemoveModel(_instancedName);
//				_instancedModel = nullptr;
//				_instancedID = UINT32_MAX;
//				ImGui::EndPopup();
//				return;
//			}
//			ImGui::EndPopup();
//		}
//
//		ImGui::Spacing();
//		ImGui::Text("Model Inspector");
//
//		Mathf::Vector3 pos = _instancedModel->position;
//		Mathf::Vector3 rot = { _instancedModel->rotationX, _instancedModel->rotationY, _instancedModel->rotationZ };
//		Mathf::Vector3 scale = _instancedModel->scale;
//
//		ImGui::PushID(_instancedID);
//		ImGui::DragFloat3("Position", &pos.x, 0.1f);
//		ImGui::DragFloat3("Rotation", &rot.x, 0.1f);
//		ImGui::DragFloat3("Scale", &scale.x, 0.1f);
//		ImGui::ColorEdit4("Outline Color", &_instancedModel->outline.color.x);
//		ImGui::DragFloat("Outline Tick", &_instancedModel->outline.oulineThickness, 0.1f, 0.0f, 100.0f);
//		if (ImGui::Checkbox("Visiable", &_isVisiable))
//		{
//			_instancedModel->isVisiable = _isVisiable;
//		}
//		if (!(_instancedModel->outline.bitmask & OUTLINE_BIT))
//		{
//			_instancedModel->outline.bitmask |= OUTLINE_BIT;
//		}
//		ImGui::PopID();
//
//		if (_instancedModel->animModel)
//		{
//			ImGui::Text("Animation");
//			ImGui::SameLine();
//			ImGui::PushID(_instancedID);
//			static int currentAnimIndex = 0;
//			ImGui::InputInt("AnimIndex", &currentAnimIndex);
//			if (ImGui::Button("Play"))
//			{
//				_instancedModel->animModel->animator->PlayAnimation(currentAnimIndex);
//			}
//			ImGui::PopID();
//		}
//
//		_instancedModel->position = pos;
//		_instancedModel->SetRotation(rot.x, rot.y, rot.z);
//		_instancedModel->scale = scale;
//	}
//
//	if (_subInstancedModel)
//	{
//		ImGui::Spacing();
//		std::string tempName = "Model" + std::to_string(_subInstancedModel->instancedID);
//
//		if (_subInstancedModel->animModel)
//		{
//			ImGui::Text(tempName.c_str());
//			ImGui::SameLine();
//			ImGui::PushID(_subInstancedModel->instancedID);
//			ImGui::Selectable(_subInstancedModel->animModel->name.c_str(), false);
//			ImGui::PopID();
//		}
//
//		std::string tempName2 = "R_ClickMenu" + std::to_string(_subInstancedModel->instancedID);
//		if (ImGui::BeginPopupContextItem(tempName2.c_str()))
//		{
//			if (ImGui::MenuItem("Delete"))
//			{
//				_targetScene->RemoveModel(_subModelName);
//				_subInstancedModel = nullptr;
//				ImGui::EndPopup();
//				return;
//			}
//			ImGui::EndPopup();
//		}
//
//		ImGui::Spacing();
//		ImGui::Text("Model Inspector Sub");
//
//		Mathf::Vector3 pos = _subInstancedModel->position;
//		Mathf::Vector3 rot = { _subInstancedModel->rotationX, _subInstancedModel->rotationY, _subInstancedModel->rotationZ };
//		Mathf::Vector3 scale = _subInstancedModel->scale;
//
//		ImGui::PushID(_subInstancedModel->instancedID);
//		ImGui::DragFloat3("Position", &pos.x, 0.1f);
//		ImGui::DragFloat3("Rotation", &rot.x, 0.1f);
//		ImGui::DragFloat3("Scale", &scale.x, 0.1f);
//		ImGui::ColorEdit4("Outline Color", &_subInstancedModel->outline.color.x);
//		ImGui::DragFloat("Outline Tick", &_subInstancedModel->outline.oulineThickness, 0.1f, 0.0f, 100.0f);
//		if (ImGui::Checkbox("Visiable", &_isSubVisiable))
//		{
//			_subInstancedModel->isVisiable = _isSubVisiable;
//		}
//		if (!(_subInstancedModel->outline.bitmask & OUTLINE_BIT))
//		{
//			_subInstancedModel->outline.bitmask |= OUTLINE_BIT;
//		}
//		ImGui::PopID();
//
//		if (_subInstancedModel->animModel)
//		{
//			ImGui::Text("Animation");
//			ImGui::SameLine();
//			ImGui::PushID(_subInstancedModel->instancedID);
//			static int currentAnimIndex = 0;
//			ImGui::InputInt("AnimIndex", &currentAnimIndex);
//			if (ImGui::Button("Play"))
//			{
//				_subInstancedModel->animModel->animator->PlayAnimation(currentAnimIndex);
//			}
//			ImGui::PopID();
//		}
//
//		_subInstancedModel->position = pos;
//		_subInstancedModel->SetRotation(rot.x, rot.y, rot.z);
//		_subInstancedModel->scale = scale;
//	}
//
//	if (_axeInstancedModel)
//	{
//		ImGui::Spacing();
//		std::string tempName = "Model" + std::to_string(_axeInstancedModel->instancedID);
//		if (_axeInstancedModel->model)
//		{
//			ImGui::Text(tempName.c_str());
//			ImGui::SameLine();
//			ImGui::PushID(_axeInstancedModel->instancedID);
//			ImGui::Selectable(_axeInstancedModel->model->name.c_str(), false);
//			ImGui::PopID();
//		}
//		else if (_axeInstancedModel->animModel)
//		{
//			ImGui::Text(tempName.c_str());
//			ImGui::SameLine();
//			ImGui::PushID(_axeInstancedModel->instancedID);
//			ImGui::Selectable(_axeInstancedModel->animModel->name.c_str(), false);
//			ImGui::PopID();
//		}
//		std::string tempName2 = "R_ClickMenu" + std::to_string(_axeInstancedModel->instancedID);
//		if (ImGui::BeginPopupContextItem(tempName2.c_str()))
//		{
//			if (ImGui::MenuItem("Delete"))
//			{
//				_targetScene->RemoveModel(_axeInstancedName);
//				_axeInstancedModel = nullptr;
//				ImGui::EndPopup();
//				return;
//			}
//			ImGui::EndPopup();
//		}
//		ImGui::Spacing();
//		ImGui::Text("Model Inspector Axe");
//		Mathf::Vector3 pos = _axeInstancedModel->position;
//		Mathf::Vector3 rot = { _axeInstancedModel->rotationX, _axeInstancedModel->rotationY, _axeInstancedModel->rotationZ };
//		Mathf::Vector3 scale = _axeInstancedModel->scale;
//		ImGui::PushID(_axeInstancedModel->instancedID);
//		ImGui::DragFloat3("Position", &pos.x, 0.1f);
//		ImGui::DragFloat3("Rotation", &rot.x, 0.1f);
//		ImGui::DragFloat3("Scale", &scale.x, 0.1f);
//		ImGui::ColorEdit4("Outline Color", &_axeInstancedModel->outline.color.x);
//		ImGui::DragFloat("Outline Tick", &_axeInstancedModel->outline.oulineThickness, 0.1f, 0.0f, 100.0f);
//		if (ImGui::Checkbox("Visiable", &_isAxeVisiable))
//		{
//			_axeInstancedModel->isVisiable = _isAxeVisiable;
//		}
//		if (!(_axeInstancedModel->outline.bitmask & OUTLINE_BIT))
//		{
//			_axeInstancedModel->outline.bitmask |= OUTLINE_BIT;
//		}
//
//		ImGui::PopID();
//
//		if (_axeInstancedModel->animModel)
//		{
//			ImGui::Text("Animation");
//			ImGui::SameLine();
//			ImGui::PushID(_axeInstancedModel->instancedID);
//			static int currentAnimIndex = 0;
//			ImGui::InputInt("AnimIndex", &currentAnimIndex);
//			if (ImGui::Button("Play"))
//			{
//				_axeInstancedModel->animModel->animator->PlayAnimation(currentAnimIndex);
//			}
//			ImGui::PopID();
//		}
//
//		_axeInstancedModel->position = pos;
//		_axeInstancedModel->SetRotation(rot.x, rot.y, rot.z);
//		_axeInstancedModel->scale = scale;
//
//	}
//
//}
//
//void RenderComponent::Serialize(_inout json& out)
//{
//	json data;
//	if(_instancedModel)
//	{
//		Mathf::Vector3 pos = _instancedModel->position;
//		Mathf::Vector3 rot = {
//			_instancedModel->rotationX,
//			_instancedModel->rotationY,
//			_instancedModel->rotationZ
//		};
//		Mathf::Vector3 scale = _instancedModel->scale;
//
//		data["Transform"]["Scale"] = { scale.x, scale.y, scale.z };
//		data["Transform"]["Rotation"] = { rot.x, rot.y, rot.z };
//		data["Transform"]["Position"] = { pos.x, pos.y, pos.z };
//		data["Outline"]["Color"] = { _instancedModel->outline.color.x, _instancedModel->outline.color.y, _instancedModel->outline.color.z, _instancedModel->outline.color.w };
//		data["Outline"]["Thickness"] = _instancedModel->outline.oulineThickness;
//		if (_instancedModel->model)
//		{
//			data["ModelName"] = _instancedModel->model->name;
//		}
//		else if (_instancedModel->animModel)
//		{
//			data["ModelName"] = _instancedModel->animModel->name;
//		}
//
//		if (_subInstancedModel && _subInstancedModel->animModel)
//		{
//			data["SubModelName"] = _subInstancedModel->animModel->name;
//		}
//
//		if (_axeInstancedModel && _axeInstancedModel->animModel)
//		{
//			data["AxeModelName"] = _axeInstancedModel->animModel->name;
//		}
//
//		data["InstancedName"] = _instancedName;
//		data["SubInstancedName"] = _subModelName;
//		data["AxeInstancedName"] = _axeInstancedName;
//	}
//	data["Type"] = MetaType<RenderComponent>::type;
//
//	out["components"].push_back(data);
//
//}
//
//void RenderComponent::DeSerialize(_in json& in)
//{
//	_isDeSerialize = false;
//	Mathf::Vector3 scale{1.f,1.f,1.f};
//	Mathf::Vector3 rot{};		
//	Mathf::Vector3 pos{};
//	Mathf::Color4 color{ 0,0,0,0 };
//	float thickness{};
//
//	if(!in["ModelName"].empty())
//	{
//		scale = Mathf::jsonToVector3(in["Transform"]["Scale"]);
//		rot = Mathf::jsonToVector3(in["Transform"]["Rotation"]);
//		pos = Mathf::jsonToVector3(in["Transform"]["Position"]);
//		color = Mathf::jsonToColor4(in["Outline"]["Color"]);
//		thickness = in["Outline"]["Thickness"].get<float>();
//		_modelName = in["ModelName"].get<std::string>();
//		_instancedName = in["InstancedName"].get<std::string>();
//
//		if (!in["SubModelName"].empty())
//		{
//			_subModelName = in["SubModelName"].get<std::string>();
//			_subInstancedName = in["SubInstancedName"].get<std::string>();
//		}
//		if (!in["AxeModelName"].empty())
//		{
//			_axeModelName = in["AxeModelName"].get<std::string>();
//			_axeInstancedName = in["AxeInstancedName"].get<std::string>();
//		}
//
//
//		if (AssetsSystem->Models[_modelName])
//		{
//			_instancedModel = _targetScene->AddModel(_instancedName, AssetsSystem->Models[_modelName]);
//			_instancedModel->position = pos;
//			_instancedModel->SetRotation(rot.x, rot.y, rot.z);
//			_instancedModel->scale = scale;
//			_instancedModel->outline.color = color;
//			_instancedModel->outline.oulineThickness = thickness;
//			_instancedID = _instancedModel->instancedID;
//			_isDeSerialize = true;
//		}
//		else if (AssetsSystem->AnimatedModels[_modelName])
//		{
//			_instancedModel = _targetScene->AddModel(_instancedName, AssetsSystem->AnimatedModels[_modelName]);
//			_instancedModel->position = pos;
//			_instancedModel->SetRotation(rot.x, rot.y, rot.z);
//			_instancedModel->scale = scale;
//			_instancedModel->outline.color = color;
//			_instancedModel->outline.oulineThickness = thickness;
//			_instancedID = _instancedModel->instancedID;
//
//			if (_subModelName != "")
//			{
//				_subInstancedModel = _targetScene->AddModel(_subInstancedName, AssetsSystem->AnimatedModels[_subModelName]);
//				_subInstancedModel->position = pos;
//				_subInstancedModel->SetRotation(rot.x, rot.y, rot.z);
//				_subInstancedModel->scale = scale;
//				_subInstancedModel->outline.color = color;
//				_subInstancedModel->outline.oulineThickness = thickness;
//			}
//
//			if (_axeModelName != "")
//			{
//				_axeInstancedModel = _targetScene->AddModel(_axeInstancedName, AssetsSystem->AnimatedModels[_axeModelName]);
//				_axeInstancedModel->position = pos;
//				_axeInstancedModel->SetRotation(rot.x, rot.y, rot.z);
//				_axeInstancedModel->scale = scale;
//				_axeInstancedModel->outline.color = color;
//				_axeInstancedModel->outline.oulineThickness = thickness;
//			}
//
//			_isDeSerialize = true;
//		}
//		else
//		{
//			_notYetSerializeData.scale = scale;
//			_notYetSerializeData.rotation = rot;
//			_notYetSerializeData.position = pos;
//			_notYetSerializeData.outlineColor = color;
//			_notYetSerializeData.outlineThickness = thickness;
//
//			ImGui::ContextRegister("LoadingModel", [&]
//			{
//				ImVec2 windowSize = ImGui::GetWindowSize();
//				ImVec2 progressBarSize = ImVec2(350.0f, 230.0f); // ProgressBar의 크기 지정
//				// 중앙 정렬을 위해 시작 위치 계산
//				ImVec2 centeredPos = ImVec2(
//					(windowSize.x - progressBarSize.x) * 0.5f,  // X 중심
//					(windowSize.y - progressBarSize.y) * 0.5f   // Y 중심
//				);
//
//				// ProgressBar 위치 설정
//				ImGui::SetCursorPos(centeredPos);
//				ImGui::ProgressBar(progress, progressBarSize, "");
//				progress += 0.001f;
//
//			}, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
//		}
//	}
//
//	
//}
