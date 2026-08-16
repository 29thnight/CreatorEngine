#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "GameObject.h"
#include "LightMapping.h"

class Mesh;
class Material;
class Animator;
class Camera;
class MeshRenderer : public Component, public std::enable_shared_from_this<MeshRenderer>
{
public:
   // CT4 파일럿 — 명시 메타(멤버 포인터 + 문자열화 이름). shared_ptr·중첩
   // 구조체·비트플래그 혼합 케이스의 대표. 순서 = 구 generated.h.
   ReflectionMetaFieldInheritance(MeshRenderer, Component,
       ct_property(m_Material),
       ct_property(m_Mesh),
       ct_property(m_LightMapping),
       ct_property(m_bitflag),
       ct_property(m_isSkinnedMesh),
       ct_property(m_shadowRecive),
       ct_property(m_shadowCast),
       ct_property(m_isEnableLOD))

   MeshRenderer();
   virtual ~MeshRenderer() override;

   bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
   void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

   virtual void Awake() override;
   virtual void OnDestroy() override;

   void SetSkinnedMesh(bool isSkinned) { m_isSkinnedMesh = isSkinned; }
   bool IsSkinnedMesh() const { return m_isSkinnedMesh; }

    BoundingBox GetBoundingBox() const;

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
};
