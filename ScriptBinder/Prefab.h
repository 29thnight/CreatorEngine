#pragma once
#include "Object.h"
#include "GameObject.h"
#include "ReflectionYml.h"
#include "SceneManager.h"
#include "ComponentFactory.h"

class Prefab : public Object
{
public:
   static consteval auto describe()
   {
       return meta::describe<Prefab>(
           meta::base<Object>(),
           meta::member<&Prefab::m_fileGuid>());
   }
    Prefab() = default;
    Prefab(std::string_view name, const GameObject* source);
    ~Prefab() override = default;

    static Prefab* CreateFromGameObject(const GameObject* source, std::string_view name = "");

    GameObject* Instantiate(std::string_view newName = "") const;
    GameObject* Instantiate(Scene* targetScene, std::string_view newName = "") const;

    const MetaYml::Node& GetPrefabData() const { return m_prefabData; }
    void SetPrefabData(const MetaYml::Node& node) { m_prefabData = node; }
    FileGuid GetFileGuid() const { return m_fileGuid; }
    void SetFileGuid(const FileGuid& guid) { m_fileGuid = guid; }

private:
    static MetaYml::Node SerializeRecursive(const GameObject* obj);
    GameObject* InstantiateRecursive(const MetaYml::Node& node,
                                     Scene* scene,
                                     GameObject::Index parent,
                                     std::string_view overrideName = "") const;

    MetaYml::Node m_prefabData{};

	FileGuid m_fileGuid{};
};

