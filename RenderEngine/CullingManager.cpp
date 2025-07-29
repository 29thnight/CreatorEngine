#include "CullingManager.h"
#include "Core.OctreeNode.h"
#include "MeshRenderer.h"
#include "Component.h"
#include "GameObject.h"
#include "SceneManager.h"

using namespace DirectX;

BoundingBox CalculateSceneWorldBounds(const std::vector<MeshRenderer*>& sceneMeshes)
{
    BoundingBox bounds = {};
    bool initialized = false;

    for (MeshRenderer* mesh : sceneMeshes)
    {
        if (!mesh) continue;

        const BoundingBox& meshBox = mesh->GetBoundingBox();

        if (!initialized)
        {
            bounds = meshBox;
            initialized = true;
        }
        else
        {
            BoundingBox::CreateMerged(bounds, bounds, meshBox);
        }
    }

    // 최소 크기 또는 메쉬 없음 보정
    if (!initialized)
    {
        // 메쉬가 하나도 없으면 고정 박스 사용
        return BoundingBox({ 0.0f, 0.0f, 0.0f }, { 2000.f, 2000.f, 2000.f });
    }

    const float minExtentThreshold = 2000.f;

    // 각 축의 extents가 최소 기준보다 작은 경우, 고정 박스 사용
    if (bounds.Extents.x < minExtentThreshold * 0.5f ||
        bounds.Extents.y < minExtentThreshold * 0.5f ||
        bounds.Extents.z < minExtentThreshold * 0.5f)
    {
        return BoundingBox({ 0.0f, 0.0f, 0.0f }, { 2000.f, 2000.f, 2000.f });
    }

    return bounds;
}

CullingManager::~CullingManager()
{
    Clear();
}

void CullingManager::Initialize(const BoundingBox& worldBounds, int maxDepth, int maxMeshesPerNode)
{
 //   Clear();
 //   m_maxDepth = maxDepth;
 //   m_maxMeshesPerNode = maxMeshesPerNode;

	//void* voidPtr = malloc(sizeof(OctreeNode));
	//m_root = new (voidPtr) OctreeNode(worldBounds, 0);

	//m_threadPool = new ThreadPool;

 //   SceneManagers->activeSceneChangedEvent.AddLambda([&]()
 //   {
	//	std::vector<MeshRenderer*> sceneMeshes = SceneManagers->GetAllMeshRenderers();
	//	BoundingBox newWorldBounds = CalculateSceneWorldBounds(sceneMeshes);

	//	Rebuild(sceneMeshes, newWorldBounds);
 //   });

}

void CullingManager::Rebuild(const std::vector<MeshRenderer*>& sceneMeshes, const DirectX::BoundingBox& newWorldBounds)
{
    Clear(); // 기존 옥트리 제거

    // 새로운 옥트리 루트 생성
    void* voidPtr = malloc(sizeof(OctreeNode));
    m_root = new (voidPtr) OctreeNode(newWorldBounds, 0);

    // 모든 메쉬에 대해 재삽입
    for (MeshRenderer* mesh : sceneMeshes)
    {
        if (mesh)
        {
            mesh->CullGroupClear(); // 이전 정보 제거 (안전성 보장)
            Insert(mesh);
        }
    }
}

void CullingManager::Insert(MeshRenderer* mesh)
{
    if (m_root)
        m_root->Insert(mesh, m_maxDepth, m_maxMeshesPerNode);
}

void CullingManager::SmartCullMeshes(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const
{
    if (!m_root)
        return;

    constexpr int ParallelThreshold = 3;

    const int actualDepth = m_root->GetMaxDepth();
    if (actualDepth >= ParallelThreshold)
    {
        CullMeshesMultithread(frustum, outVisibleMeshes);
    }
    else
    {
        CullMeshes(frustum, outVisibleMeshes);
    }
}

void CullingManager::CullMeshes(const BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const
{
	std::vector<MeshRenderer*> visibleMeshes;

    if (m_root)
        CullRecursive(frustum, m_root, visibleMeshes);

	// 중복 제거
	std::set<MeshRenderer*> uniqueMeshes(visibleMeshes.begin(), visibleMeshes.end());
	for (const auto& result : uniqueMeshes)
	{
		if (result)
		{
			outVisibleMeshes.push_back(result);
		}
	}
}

void CullingManager::CullMeshesMultithread(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& outVisibleMeshes) const
{
    if (!m_root)
        return;

	std::vector<std::vector<MeshRenderer*>> results(m_threadPool->GetThreadCount());
	std::atomic<uint32> jobIndex{ 0 };

	std::array<OctreeNode*, 8> rootChildren = m_root->children;

	for (DWORD i = 0; i < m_threadPool->GetThreadCount(); ++i)
	{
        uint32_t index = jobIndex.fetch_add(1);
        if (index >= rootChildren.size())
            break;

        OctreeNode* child = rootChildren[index];
        if (!child)
            continue;

		m_threadPool->Enqueue([&, i]()
		{
            CullRecursive(frustum, child, results[i]);
		});
	}

    // 루트 노드 자체 검사
    ContainmentType rootContainment = frustum.Contains(m_root->boundingBox);
    if (rootContainment != DISJOINT)
    {
        CullRoot(frustum, outVisibleMeshes);
    }

    m_threadPool->NotifyAllAndWait();

	std::set<MeshRenderer*> uniqueMeshes;
    for (const auto& result : results)
    {
		uniqueMeshes.insert(result.begin(), result.end());
    }

    for (const auto& result : uniqueMeshes)
    {
        if (result)
        {
            outVisibleMeshes.push_back(result);
        }
    }
}

void CullingManager::CullRoot(const DirectX::BoundingFrustum& frustum, std::vector<MeshRenderer*>& out) const
{
    ContainmentType result = frustum.Contains(m_root->boundingBox);

	if (result == DISJOINT)
		return;

	if (result == CONTAINS)
	{
		out.insert(out.end(), m_root->objects.begin(), m_root->objects.end());
	}
	else
	{
		for (MeshRenderer* renderer : m_root->objects)
		{
			if (frustum.Intersects(renderer->GetBoundingBox()) && !renderer->m_isSkinnedMesh)
            {
                out.push_back(renderer);
            }
		}
	}
}

void CullingManager::CullRecursive(const BoundingFrustum& frustum, OctreeNode* node, std::vector<MeshRenderer*>& out) const
{
    ContainmentType result = frustum.Contains(node->boundingBox);

    if (result == DISJOINT)
        return;

    if (result == CONTAINS)
    {
        if (node->isLeaf)
        {
            out.insert(out.end(), node->objects.begin(), node->objects.end());
        }
        else
        {
            for (OctreeNode* child : node->children)
                if (child)
                    CullRecursive(frustum, child, out);
        }
    }
    else
    {
        if (node->isLeaf)
        {
            for (MeshRenderer* renderer : node->objects)
            {
                if (frustum.Intersects(renderer->GetBoundingBox()) && !renderer->m_isSkinnedMesh)
                    out.push_back(renderer);
            }
        }
        else
        {
            for (OctreeNode* child : node->children)
                if (child)
                    CullRecursive(frustum, child, out);
        }
    }
}

void CullingManager::Clear()
{
	if (m_root)
	{
		m_root->~OctreeNode();
		free(m_root);
		m_root = nullptr;
	}
    m_root = nullptr;
}

void CullingManager::UpdateMesh(MeshRenderer* mesh)
{
    if(!mesh->IsNeedUpdateCulling())
		return;

    if (!Remove(mesh))
        return;

    Insert(mesh); // 현재 바운딩박스로 재삽입
	mesh->SetNeedUpdateCulling(false); // 업데이트 완료
}

bool CullingManager::Remove(MeshRenderer* mesh)
{
    if (!m_root)
        return false;

    for (OctreeNode* node : mesh->GetCullGroup())
    {
        auto& vec = node->objects;
        vec.erase(std::remove(vec.begin(), vec.end(), mesh), vec.end());
    }

    mesh->CullGroupClear();
    return true;
}

void CullingManager::RemoveAll()
{

}
