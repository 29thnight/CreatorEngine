#pragma once
#include "AuthoringNodeView.h"
#include "Core.Minimal.h"
#include "Component.h"
#include "Entity.h"
#include "LightMapping.h"
#include <mathematics/bounds.hpp>

class Mesh;
namespace YAML { class Node; } // CT6-d: OnDeserialized(node) 전방 선언용

class Material;
class Animator;
class Camera;
// K2: 죽은 enable_shared_from_this<MeshRenderer> 제거 — shared_from_this() 호출부 0(확증).
class MeshRenderer : public meta::identity<MeshRenderer, Component>
{
   public:
   // CT4 파일럿 — P2996 유사 빌더 표기(매크로 0). shared_ptr·중첩 구조체·
   // 비트플래그 혼합 케이스의 대표. 멤버 순서 = 구 generated.h(골든 전제).
   // I5-M5 S2c-2a: m_Material의 reflect 퇴출은 2b로 미룬다 — 프리팹 패치
   // 경로(Meta::Deserialize, postLoad 없음)가 typed 읽기에 의존해서, 지금
   // 빼면 프리팹 재질 오버라이드가 조용히 소실된다. base 참조(ref) 표기의
   // 읽기/쓰기는 훅이 전담한다(typed는 ref 노드에서 기본값 재질을 만들고
   // postLoad가 교체).
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_Material>,
           meta::field<&Self::m_Mesh>,
           meta::field<&Self::m_LightMapping>,
           meta::field<&Self::m_bitflag>,
           meta::field<&Self::m_isSkinnedMesh>,
           meta::field<&Self::m_shadowRecive>,
           meta::field<&Self::m_shadowCast>,
           meta::field<&Self::m_isEnableLOD>,
           meta::field<&Self::m_modelGuid>);
   }
public:

   MeshRenderer();

   // CT6-d: 머티리얼·메시 GUID 해석과 텍스처 로드(구 팩토리 분기 이동)
   void OnDeserialized(const Authoring::NodeView& node); // D3-a-4

   // I5-M5 S2b: typed 리플렉션이 legacy 형상으로 적은 m_Material 서브트리를
   // 정본 writer(SerializeMaterialPayload)로 교체한다 — 씬·프리팹 embed 공용.
   void OnAfterSerialize(YAML::Node& node);

   virtual ~MeshRenderer() override;

   bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
   void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

   virtual void OnInitialized() override;
   virtual void OnUninitializing() override;

   void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
   bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

    [[nodiscard]] math::aabb GetBoundingBox() const;

public:
    // 에셋을 공동 소유한다.
    //
    // 예전에는 원시 포인터라 DataSystem이 캐시에서 지우면 그대로 댕글링이 됐다.
    // shared_ptr로 두면 이 컴포넌트가 참조하는 동안 에셋이 살아 있으므로,
    // UnloadUnusedAssets를 켜도 사용 중인 리소스가 파괴되지 않는다(12.2 보충 분석).
    //
    // 리플렉션은 shared_ptr을 포인터와 동등하게 다루므로(ReflectionFunction.h)
    // 직렬화·인스펙터 경로는 그대로 동작한다.
    std::shared_ptr<Material> m_Material{};
    std::shared_ptr<Mesh> m_Mesh{};
    LightMapping m_LightMapping;
    uint32 m_bitflag{ 0 };

private:
	bool m_isNeedUpdateCulling{ false };

public: 
    bool m_isSkinnedMesh{ false };
    bool m_shadowRecive = true;
    bool m_shadowCast = true;
    bool m_isEnableLOD{ false };

    // I5-M5 S2c-1: 메시의 출처 모델을 자기 필드로 갖는다. 예전에는 인라인
    // 재질의 m_fileGuid가 모델 GUID를 나르는 편법이었다(재질 것처럼 보이지만
    // 모델 주소다 — SceneCookProducer 실측). nil이면 OnDeserialized가 legacy
    // 편법으로 폴백하고 읽는 즉시 여기로 이주한다.
    FileGuid m_modelGuid{};

    // I5-M5 S2c-2a: base 재질 자산 링크. 피커가 자산 재질을 고르면 여기에
    // 자산 GUID가 남고, 저장은 인라인 embed 대신 base 참조+인스턴스 diff를
    // 적는다(ref 표기). nil이면 인라인 소유(기존 S2b writer). reflect에는
    // 없다 — 영속은 m_Material 노드의 ref 키가 진다(훅 전담).
    FileGuid m_materialBaseGuid{};
};
