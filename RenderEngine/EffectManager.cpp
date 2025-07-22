#include "EffectManager.h"
#include "../ShaderSystem.h"
#include "ImGuiRegister.h"
#include "imgui-node-editor/imgui_node_editor.h"
#include "EffectProxyController.h"
#include "EffectSerializer.h"
#include "EffectBase.h"

void EffectManager::Initialize()
{
	std::filesystem::path effectPath = PathFinder::Relative("Effect\\");
	
	// ���丮 ���翩��
	if (!std::filesystem::exists(effectPath) || !std::filesystem::is_directory(effectPath)) {
		std::cout << "Effect folder does not exist" << '\n';
		return; 
	}

	try {

		for (const auto& entry : std::filesystem::directory_iterator(effectPath))
		{
			if (entry.is_regular_file() && entry.path().extension() == ".json")
			{
				try {
					std::ifstream file(entry.path());
					if (file.is_open()) {
						nlohmann::json effectJson;
						file >> effectJson;
						file.close();
						auto effect = EffectSerializer::DeserializeEffect(effectJson);
						if (effect) {
							std::string effectName = entry.path().stem().string();
							effects[effectName] = std::move(effect);
						}
					}
					else {
						std::cerr << "Failed to open file: " << entry.path() << std::endl;
					}
				}
				catch (const std::exception& e) {
					std::cerr << "Error loading effect: " << entry.path() << " - " << e.what() << std::endl;
				}
			}
		}
	}
	// json�� ������
	catch (const std::filesystem::filesystem_error& e) {
		std::cout << "Effect json does not exist" << '\n';
		return;
	}
}

void EffectManager::Execute(RenderScene& scene, Camera& camera)
{
	EffectProxyController::GetInstance()->ExecuteEffectCommands();
	for (auto& [key, effect] : effects) {
		effect->Render(scene, camera);
	}
}	

void EffectManager::Update(float delta)
{
	for (auto& [key, effect] : effects) {
		effect->Update(delta);
		std::cout << effect->GetName();
	}
}

EffectBase* EffectManager::GetEffect(std::string_view name)
{
	auto it = effects.find(name.data());
	if (it != effects.end()) {
		return it->second.get();
	}
	return nullptr;
}

bool EffectManager::RemoveEffect(std::string_view name)
{
	return effects.erase(name.data()) > 0;
}

void EffectManager::RegisterCustomEffect(const std::string& name, const std::vector<std::shared_ptr<ParticleSystem>>& emitters)
{
	if (!emitters.empty()) {
		// EffectBase ���� ����
		auto effect = std::make_unique<EffectBase>();
		effect->SetName(name);

		// �� ������ �߰�
		for (auto& emitter : emitters) {
			effect->AddParticleSystem(emitter);
		}

		effects[name] = std::move(effect);
	}
}

void EffectManager::CreateEffectInstance(const std::string& templateName, const std::string& instanceName)
{
	if(templateName.empty() || instanceName.empty()) {
		return; // ���ø� �̸��̳� �ν��Ͻ� �̸��� ��������� �ƹ� �۾��� ���� ����
	}

	auto instnaceIt = effects.find(instanceName);
	if (instnaceIt != effects.end()) {
		// �̹� ���� �̸��� �ν��Ͻ��� �����ϸ� �ƹ� �۾��� ���� ����
		return;
	}

	auto templateIt = effects.find(templateName);
	if (templateIt != effects.end()) {
		// ���ø��� �����ؼ� �� �ν��Ͻ� ����
		auto newEffect = std::make_unique<EffectBase>(*templateIt->second);
		newEffect->SetName(instanceName);
		effects[instanceName] = std::move(newEffect);
	}
}