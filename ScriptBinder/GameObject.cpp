#include "GameObject.h"
#include "ModuleBehavior.h"
#include "Scene.h"
#include "SceneManager.h"
#include "RenderableComponents.h"
#include "AScriptComponent.h"
#include "AngelScriptManager.h"
#include "TagManager.h"

GameObject::GameObject() :
	Object("GameObject"),
	m_gameObjectType(GameObjectType::Empty),
	m_index(0),
	m_parentIndex(-1)
{
	m_ownerScene = SceneManagers->GetActiveScene();
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(0);
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
}

GameObject::GameObject(Scene* scene, const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
    Object(name),
    m_gameObjectType(type),
    m_index(index), 
    m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
    m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(parentIndex);
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
}

GameObject::GameObject(Scene* scene, size_t instanceID, const std::string_view& name, GameObjectType type, GameObject::Index index, GameObject::Index parentIndex) :
	Object(name, instanceID),
	m_gameObjectType(type),
	m_index(index),
	m_parentIndex(parentIndex),
	m_ownerScene(scene)
{
	m_typeID = { TypeTrait::GUIDCreator::GetTypeID<GameObject>() };
	m_transform.SetOwner(this);
	m_transform.SetParentID(parentIndex);
	m_components.reserve(30); // Reserve space for components to avoid frequent reallocations
}

void GameObject::SetTag(const std::string_view& tag)
{
	if (tag.empty() || tag == "Untagged")
	{
		return; // Avoid adding empty tags
	}

    if (TagManager::GetInstance()->HasTag(tag))
    {
            m_tag = tag.data();
    }
}

void GameObject::SetLayer(const std::string_view& layer)
{
    if (layer.empty())
    {
        return;
    }

    if (TagManager::GetInstance()->HasLayer(layer))
    {
        m_layer = layer.data();
    }
}

void GameObject::Destroy()
{
	if (m_destroyMark)
	{
		return;
	}
	m_destroyMark = true;

	for (auto& component : m_components)
	{
		component->Destroy();
	}

	for (auto& childIndex : m_childrenIndices)
	{
		GameObject* child = FindIndex(childIndex);
		if (child)
		{
			child->Destroy();
		}
	}
}

std::shared_ptr<Component> GameObject::AddComponent(const Meta::Type& type)
{
    if (std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == type.typeID; }) != m_components.end())
    {
        return nullptr;
    }

    std::shared_ptr<Component> component = std::shared_ptr<Component>(Meta::MetaFactoryRegistry->Create<Component>(type.name));
	
    if (component)
    {
		if (auto receiver = std::dynamic_pointer_cast<IRegistableEvent>(component))
		{
			receiver->RegisterOverriddenEvents(this->GetScene());
		}
        m_components.push_back(component);
        component->SetOwner(this);
        m_componentIds[component->GetTypeID()] = m_components.size() - 1;
    }

	return component;
}

ModuleBehavior* GameObject::AddScriptComponent(const std::string_view& scriptName)
{
    std::shared_ptr<ModuleBehavior> component = std::shared_ptr<ModuleBehavior>(
		ScriptManager->CreateMonoBehavior(scriptName.data()), 
		[](ModuleBehavior* ptr) // Custom deleter to ensure proper cleanup
		{
			ScriptManager->DestroyMonoBehavior(ptr);
		}
	);
	if (!component)
	{
		Debug->LogError("Failed to create script component: " + std::string(scriptName));
		return nullptr;
	}
	component->SetOwner(this);

	std::string scriptFile = std::string(scriptName) + ".cpp";

	component->m_scriptGuid = DataSystems->GetFilenameToGuid(scriptFile);

    auto componentPtr = std::reinterpret_pointer_cast<Component>(component);
    m_components.push_back(componentPtr);
    
	size_t index = m_components.size() - 1;
	m_componentIds[component->m_scriptTypeID] = index;

    ScriptManager->CollectScriptComponent(this, index, scriptName.data());

    return component.get();
}


AScriptComponent* GameObject::AddAngelScriptComponent(const std::string_view& scriptName)
{
	std::shared_ptr<AScriptComponent> component = std::make_shared<AScriptComponent>();
	component->m_scriptName = scriptName.data();
	component->SetOwner(this);

	// AngelScript 엔진 가져오기
	asIScriptEngine* asEngine = AngelScriptManagers->GetEngine();
	if (!asEngine)
	{
		Debug->LogError("AngelScript engine is not initialized.");
		return nullptr;
	}

	// 스크립트 모듈 생성 또는 가져오기
	asIScriptModule* module = asEngine->GetModule("AScriptModule", asGM_ALWAYS_CREATE);
	if (!module)
	{
		Debug->LogError("Failed to get AngelScript module.");
		return nullptr;
	}

	// 스크립트 코드 로드 (예시: 파일에서 읽어오는 대신 임시로 문자열 사용)
	// 실제 프로젝트에서는 scriptName을 사용하여 파일에서 스크립트 코드를 읽어와야 합니다.
	std::string shared_script_code = "shared abstract class AScriptBehaviour { AScriptBehaviour() { @m_obj = AScriptBehaviourWrapper(); } void OnStart() { m_obj.OnStart(); } void OnUpdate(float deltaTime) { m_obj.OnUpdate(deltaTime); } private AScriptBehaviourWrapper@ m_obj; };\n";
	std::string scriptCode = "class " + std::string(scriptName) + " : AScriptBehaviour { void OnStart() { Print(\"Script Start!\"); } void OnUpdate(float deltaTime) {} };";

	std::string full_scriptCode = shared_script_code + "\n" + scriptCode;

	// 스크립트 컴파일
	asIScriptContext* ctx = asEngine->RequestContext();
	if (!ctx)
	{
		Debug->LogError("Failed to request AngelScript context.");
		return nullptr;
	}

	module->AddScriptSection(scriptName.data(), full_scriptCode.c_str());
	int r = module->Build();
	if (r < 0)
	{
		Debug->LogError("Failed to build AngelScript module.");
		asEngine->ReturnContext(ctx);
		return nullptr;
	}

	// 스크립트 클래스 타입 가져오기
	asITypeInfo* type = module->GetTypeInfoByName(scriptName.data());
	if (!type)
	{
		Debug->LogError("Failed to get AngelScript type info for " + std::string(scriptName));
		asEngine->ReturnContext(ctx);
		return nullptr;
	}

	// 스크립트 객체 생성
	asIScriptObject* scriptObject = reinterpret_cast<asIScriptObject*>(asEngine->CreateScriptObject(type));
	if (!scriptObject)
	{
		Debug->LogError("Failed to create AngelScript object for " + std::string(scriptName));
		asEngine->ReturnContext(ctx);
		return nullptr;
	}

	AScriptBehaviourWrapper* wrapper = *reinterpret_cast<AScriptBehaviourWrapper**>(
		scriptObject->GetAddressOfProperty(0));
	if (!wrapper)
	{
		Debug->LogError("Failed to get AScriptBehaviourWrapper from AngelScript object for " + std::string(scriptName));
		asEngine->ReturnContext(ctx);
		return nullptr;
	}

	wrapper->AddRef(); // wrapper의 참조 유지
	scriptObject->Release(); // 스크립트 객체는 더이상 직접 관리 안함

	// AScriptComponent에 스크립트 객체와 wrapper 설정
	component->SetScriptBehaviourWrapper(wrapper);

	// OnStart 함수 호출 (이제 ScriptedAScriptBehaviour를 통해 호출됨)
	// 여기서는 직접 호출하지 않고, AScriptBehaviour의 OnStart가 ScriptedAScriptBehaviour를 통해 스크립트 OnStart를 호출하도록 합니다.
	component->Start(); // 이 부분은 이제 AScriptBehaviour의 OnStart에서 처리

	asEngine->ReturnContext(ctx);

	m_components.push_back(component);
	m_componentIds[component->GetTypeID()] = m_components.size() - 1;

	return component.get();
}

std::shared_ptr<Component> GameObject::GetComponent(const Meta::Type& type)
{
    HashedGuid typeID = type.typeID;
    auto iter = m_componentIds.find(typeID);
    if (iter != m_componentIds.end())
    {
        size_t index = m_componentIds[typeID];
        return m_components[index];
    }

    return nullptr;
}

std::shared_ptr<Component> GameObject::GetComponentByTypeID(uint32 id)
{
	if (id >= m_components.size())
	{
		return nullptr;
	}
	auto iter = m_componentIds.find(id);
	if (iter != m_componentIds.end())
	{
		size_t index = iter->second;
		return m_components[index];
	}
	return nullptr;
}

void GameObject::RefreshComponentIdIndices()
{
	std::unordered_map<HashedGuid, size_t> newMap;

	for (size_t i = 0; i < m_components.size(); ++i)
	{
		const auto& component = m_components[i];
		if (!component)
			continue;

		auto scriptComponent = std::dynamic_pointer_cast<ModuleBehavior>(m_components[i]);
		if (scriptComponent)
		{
			ScriptManager->UnCollectScriptComponent(this, i, scriptComponent->m_name.ToString());
			newMap[scriptComponent->m_scriptTypeID] = i;
			ScriptManager->CollectScriptComponent(this, i, scriptComponent->m_name.ToString());
		}
		else
		{
			newMap[component->GetTypeID()] = i;
		}
	}

	m_componentIds = std::move(newMap);
}

void GameObject::RemoveComponentIndex(uint32 id)
{
	if (id >= m_components.size())
	{
		return;
	}

	auto iter = m_componentIds.find(m_components[id]->GetTypeID());
	if (iter != m_componentIds.end())
	{
		m_componentIds.erase(iter);
	}

	if(nullptr != m_components[id])
	{
		m_components[id]->Destroy();
	}
}

void GameObject::RemoveComponentTypeID(uint32 typeID)
{
	auto iter = m_componentIds.find(typeID);
	if (iter != m_componentIds.end())
	{
		size_t index = iter->second;
		m_components[index]->Destroy();
		m_componentIds.erase(iter);
	}
}

void GameObject::RemoveScriptComponent(const std::string_view& scriptName)
{
	auto iter = std::ranges::find_if(m_components, [&](std::shared_ptr<Component> component) { return component->GetTypeID() == TypeTrait::GUIDCreator::GetTypeID<ModuleBehavior>(); });
	if (iter != m_components.end())
	{
		auto scriptComponent = std::dynamic_pointer_cast<ModuleBehavior>(*iter);
		if (scriptComponent && scriptComponent->m_name == scriptName)
		{
			ScriptManager->UnbindScriptEvents(scriptComponent.get(), scriptName);
			ScriptManager->UnCollectScriptComponent(this, m_componentIds[scriptComponent->m_scriptTypeID], scriptComponent->m_name.ToString());
			scriptComponent->Destroy();
			RemoveComponentTypeID(scriptComponent->m_scriptTypeID);
		}
	}
}

void GameObject::RemoveScriptComponent(ModuleBehavior* ptr)
{
	auto iter = m_componentIds.find(ptr->m_scriptTypeID);
	if (iter != m_componentIds.end())
	{
		auto scriptComponent = ptr;
		auto scriptName = scriptComponent->m_name.ToString();
		if (scriptComponent && iter != m_componentIds.end())
		{
			size_t index = iter->second;
			ScriptManager->UnbindScriptEvents(scriptComponent, scriptName);
			ScriptManager->UnCollectScriptComponent(this, index, scriptName);
			scriptComponent->Destroy();
			RemoveComponentTypeID(scriptComponent->m_scriptTypeID);
		}
	}
}

void GameObject::RemoveComponent(Meta::Type& type)
{
}

GameObject* GameObject::Find(const std::string_view& name)
{
    Scene* scene = SceneManagers->GetActiveScene();
    if (scene)
    {
        return scene->GetGameObject(name).get();
    }
    return nullptr;
}

GameObject* GameObject::FindIndex(GameObject::Index index)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		return scene->GetGameObject(index).get();
	}
	return nullptr;
}

GameObject* GameObject::FindInstanceID(const HashedGuid& guid)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=] (std::shared_ptr<GameObject>& object)
		{
			return object->m_instanceID == guid;
		});

		return (*it).get();
	}

	return nullptr;
}

GameObject* GameObject::FindAttachedID(const HashedGuid& guid)
{
	Scene* scene = SceneManagers->GetActiveScene();
	if (scene)
	{
		auto& gameObjects = scene->m_SceneObjects;
		auto it = std::find_if(gameObjects.begin(), gameObjects.end(), [=](std::shared_ptr<GameObject>& object)
		{
			return object->m_attachedSoketID == guid;
		});
		if (it != gameObjects.end())
			return (*it).get();
	}

	return nullptr;
}

void GameObject::SetEnabled(bool able)
{
	if (m_isEnabled == able)
	{
		return;
	}
	m_isEnabled = able;

	for (auto& component : m_components)
	{
		if (component)
		{
			component->SetEnabled(able);
		}
	}
}


