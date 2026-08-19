#pragma once
#include "Object.h"
#include "GameObject.h"
#include "ReflectionYml.h"
#include "SceneManager.h"
#include "ComponentFactory.h"

class Prefab : public Object
{
   public:
   using meta_identity = meta::identity_descriptor<Prefab, Object>;
   static consteval auto reflect()
   {
       using Self = Prefab;
       return meta::schema<Self>(
           meta::field<&Self::m_fileGuid>);
   }
public:
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

    // inheritedPrefabGuid: 이 노드가 자기 자신의 guid를 갖고 있지 않을 때(=순수
    // 자식) 물려받을 값 — 호출자(부모 프레임)의 확정된 guid다. 최상위 호출
    // (parent==0, Instantiate()가 최초로 넘기는 자리)에서는 무시되고 항상
    // GetFileGuid()로 고정된다(P4-a, Prefab.cpp InstantiateRecursive 본문 주석 참고).
    GameObject* InstantiateRecursive(const MetaYml::Node& node,
                                     Scene* scene,
                                     GameObject::Index parent,
                                     std::string_view overrideName = "",
                                     FileGuid inheritedPrefabGuid = nullFileGuid) const;

    MetaYml::Node m_prefabData{};

	FileGuid m_fileGuid{};
};

