#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "GameObject.h"
#include "LightMapping.h"
#include "IRegistableEvent.h"
#include "MeshRenderer.generated.h"

class Mesh;
class Material;
class Animator;
class Camera;
class OctreeNode;
class MeshRenderer : public Component, public RegistableEvent<MeshRenderer>
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

	void CullGroupInsert(OctreeNode* node);
    void CullGroupClear();
	std::unordered_set<OctreeNode*>& GetCullGroup() { return m_OctreeNodes; }

public:
    [[Property]]
    Material* m_Material{ nullptr };
    [[Property]]
    Mesh* m_Mesh{ nullptr };
    [[Property]]
    LightMapping m_LightMapping;
    [[Property]]
	bool m_isSkinnedMesh{ false };
    [[Property]]
    bool m_shadowRecive = true;
	[[Property]]
    bool m_shadowCast = true;
    [[Property]]
    bool m_isEnableLOD{ false };

private:
	bool m_isNeedUpdateCulling{ false };
	std::unordered_set<OctreeNode*> m_OctreeNodes;
};
