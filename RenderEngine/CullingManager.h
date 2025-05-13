#pragma once
#include "ClassProperty.h"
#include "Core.Thread.hpp"
#include <vector>
#include <DirectXCollision.h>

class MeshRenderer;
class OctreeNode;

class CullingManager : public Singleton<CullingManager>
{
private:
	friend class Singleton;
    CullingManager() = default;
    ~CullingManager();

public:
    void Initialize(const DirectX::BoundingBox& worldBounds, int maxDepth = 5, int maxMeshesPerNode = 10);
    void Rebuild(const std::vector<MeshRenderer*>& sceneMeshes, const DirectX::BoundingBox& newWorldBounds);
    void Insert(MeshRenderer* mesh);
    void SmartCullMeshes(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const;
    void CullMeshes(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const;
	void CullMeshesMultithread(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const;
    bool Remove(MeshRenderer* mesh);
	void RemoveAll();
    void UpdateMesh(MeshRenderer* mesh);
    void Clear();

private:
    ThreadPool* m_threadPool{ nullptr };
    OctreeNode* m_root{ nullptr };
    int m_maxDepth = 5;
    int m_maxMeshesPerNode = 10;

    void CullRecursive(const DirectX::BoundingFrustum& frustum, OctreeNode* node, std::vector<MeshRenderer*>& out) const;
};

static auto& CullingManagers = CullingManager::GetInstance();