#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "GameObject.h"
#include "IRenderable.h"
#include "LightMapping.h"
#include "IOnDistroy.h"
#include "../RenderEngine/Mesh.h"
#include "../RenderEngine/Material.h"
#include "MeshRenderer.generated.h"

class Animator;
class OctreeNode;
class MeshRenderer : public Component, public IOnDistroy
{
public:
   ReflectMeshRenderer
    [[Serializable(Inheritance:Component)]]
   MeshRenderer();
   virtual ~MeshRenderer() override;

   bool IsNeedUpdateCulling() const { return m_isNeedUptateCulling; }
   void SetNeedUpdateCulling(bool able) { m_isNeedUptateCulling = able; }

   virtual void OnDistroy() override;

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

private:
	bool m_isNeedUptateCulling{ false };
	std::unordered_set<OctreeNode*> m_OctreeNodes;
};
