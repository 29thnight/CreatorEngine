#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "GameObject.h"
#include "LightMapping.h"
#include "MeshRenderer.generated.h"

class Mesh;
class Material;
class Animator;
class Camera;
class MeshRenderer : public Component, public std::enable_shared_from_this<MeshRenderer>
{
public:
   ReflectMeshRenderer
    [[Serializable(Inheritance:Component)]]
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
    [[Property]]
    std::shared_ptr<Material> m_Material{};
    [[Property]]
    std::shared_ptr<Mesh> m_Mesh{};
    [[Property]]
    LightMapping m_LightMapping;
    [[Property]]
    uint32 m_bitflag{ 0 };

private:
	bool m_isNeedUpdateCulling{ false };

public: 
    [[Property]]
    bool m_isSkinnedMesh{ false };
    [[Property]]
    bool m_shadowRecive = true;
    [[Property]]
    bool m_shadowCast = true;
    [[Property]]
    bool m_isEnableLOD{ false };
};
