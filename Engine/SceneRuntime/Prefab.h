#pragma once
#include "AuthoringDocument.h"
#include "AuthoringNodeView.h" // D3-a-5b
#include "Object.h"
#include "Entity.h"
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
    Prefab(std::string_view name, const Entity* source);
    ~Prefab() override = default;

    static Prefab* CreateFromGameObject(const Entity* source, std::string_view name = "");

    Entity* Instantiate(std::string_view newName = "") const;
    Entity* Instantiate(Scene* targetScene, std::string_view newName = "") const;

    // D3-a-3c: 정의는 Prefab.cpp에 있다. 문서 소유 타입을 헤더에서 풀지 않기
    // 위해서다 — 인라인으로 두면 이 헤더가 `AuthoringDocumentAccess.h`를
    // include해야 하고, 그러면 소유자 헤더가 다시 포맷을 알게 된다.
    const MetaYml::Node& GetPrefabData() const;
    void SetPrefabData(const Authoring::NodeView& node);
    FileGuid GetFileGuid() const { return m_fileGuid; }
    void SetFileGuid(const FileGuid& guid) { m_fileGuid = guid; }

public:
    // 중첩 프리팹 참조 노드의 마커 키 (트랙 P · P4-b). Entity::reflect()의 필드가
    // 아니므로 Meta::Deserialize는 이 키를 무시한다(모르는 키는 그냥 지나간다) —
    // 즉 형상에 마커를 하나 얹어도 역직렬화 경로에 부작용이 없다.
    //
    // 암묵 규칙("컴포넌트가 없고 guid가 있으면 참조")을 쓰지 않는 이유: 나중에
    // 다른 사유로 컴포넌트 없는 노드가 생기면 조용히 참조로 오인된다. 명시 마커는
    // 그 오인이 성립하지 않게 한다.
    static constexpr const char* kNestedRefKey = "m_nestedPrefabRef";

private:
	struct InstantiateContext;

    // ownerPrefabGuid: 지금 굽고 있는 프리팹이 "자기 것"으로 보는 guid. 자식의
    // m_prefabFileGuid가 이 값과 다르고 유효하면 그 자식은 **다른 프리팹의
    // 인스턴스 루트**이므로 펼치지 않고 참조 노드로 굽는다(P4-b).
    static MetaYml::Node SerializeRecursive(const Entity* obj, FileGuid ownerPrefabGuid);

    // 중첩 인스턴스를 {정의 GUID + 오버라이드}로 굽는다 (P4-b). Entity 필드는
    // 그대로 싣고(m_name·m_tag·m_prefabOverrides 등) m_components와 children만
    // 뺀다 — 그 둘은 소환 시점에 정의에서 온다.
    static MetaYml::Node SerializeNestedReference(const Entity* obj);

    // 참조 노드를 해석해 그 자리에 중첩 프리팹의 **현재 정의**를 인스턴스화한다.
    // 실패하면 nullptr을 돌려주고 호출자가 로그를 남긴다.
    Entity* InstantiateNestedReference(const MetaYml::Node& node,
                                       Scene* scene,
                                       Entity::Index parent) const;

    // inheritedPrefabGuid: 이 노드가 자기 자신의 guid를 갖고 있지 않을 때(=순수
    // 자식) 물려받을 값 — 호출자(부모 프레임)의 확정된 guid다. 최상위 호출
    // (parent==0, Instantiate()가 최초로 넘기는 자리)에서는 무시되고 항상
    // GetFileGuid()로 고정된다(P4-a, Prefab.cpp InstantiateRecursive 본문 주석 참고).
    Entity* InstantiateRecursive(const MetaYml::Node& node,
                                     Scene* scene,
                                     Entity::Index parent,
	                                 InstantiateContext& context,
                                     std::string_view overrideName = "",
                                     FileGuid inheritedPrefabGuid = nullFileGuid) const;

    // D3-a-3c: 장기 보관 root의 마지막 하나. backend 노드를 값으로 들지 않으므로
    // D3-b가 backend를 바꿀 때 이 클래스는 손대지 않는다(§3.3).
    Authoring::Document m_prefabData{};

	FileGuid m_fileGuid{};
};

