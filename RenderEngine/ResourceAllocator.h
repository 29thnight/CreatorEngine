#pragma once
#include "Core.Minimal.h"
#include "ModelLoader.h"
#include "RenderableComponents.h"

class ResourceAllocator : public Singleton<ResourceAllocator>
{
private:
    friend class Singleton;
    ResourceAllocator() = default;
    ~ResourceAllocator() = default;

public:
    template<typename... Args>
    Model* AllocateModel(Args&&... args)
    {
        return m_modelPool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename... Args>
    ModelNode* AllocateNode(Args&&... args)
    {
        return m_nodePool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Mesh* AllocateMesh(Args&&... args)
    {
        return m_meshPool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Material* AllocateMaterial(Args&&... args)
    {
        return m_materialPool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Texture* AllocateTexture(Args&&... args)
    {
        return m_texturePool.allocate_element(std::forward<Args>(args)...);
    }
    template<typename... Args>
    Bone* AllocateBone(Args&&... args)
    {
        return m_bonePool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename... Args>
    Skeleton* AllocateSkeleton(Args&&... args)
    {
        return m_skeletonPool.allocate_element(std::forward<Args>(args)...);
    }

    void DeallocateModel(Model* model)
    {
        m_modelPool.deallocate_element(model);
    }

    void DeallocateNode(ModelNode* node)
    {
        m_nodePool.deallocate_element(node);
    }

    void DeallocateMesh(Mesh* mesh)
    {
        m_meshPool.deallocate_element(mesh);
    }

    void DeallocateMaterial(Material* material)
    {
        m_materialPool.deallocate_element(material);
    }

    void DeallocateTexture(Texture* texture)
    {
        m_texturePool.deallocate_element(texture);
    }

    void DeallocateBone(Bone* bone)
    {
        m_bonePool.deallocate_element(bone);
    }

    void DeallocateSkeleton(Skeleton* skeleton)
    {
        m_skeletonPool.deallocate_element(skeleton);
    }

private:
    MemoryPool<ModelNode, 4096> m_nodePool;
    MemoryPool<Mesh, 8192> m_meshPool;
    MemoryPool<Material, 4096> m_materialPool;
    MemoryPool<Texture, 4096> m_texturePool;
    MemoryPool<Bone, 4096> m_bonePool;
    MemoryPool<Skeleton, 4096> m_skeletonPool;
    MemoryPool<Model, 4096> m_modelPool;
};

static inline auto& ResourceMemoryPools = ResourceAllocator::GetInstance();

template<typename T>
concept ResourceType = std::is_base_of_v<Model, T>
                    || std::is_base_of_v<ModelNode, T>
                    || std::is_base_of_v<Mesh, T>
                    || std::is_base_of_v<Material, T>
                    || std::is_base_of_v<Texture, T>
                    || std::is_base_of_v<Bone, T>
                    || std::is_base_of_v<Skeleton, T>;

template<ResourceType T, typename... Args>
static inline T* AllocateResource(Args&&... args)
{
    if constexpr (std::is_same_v<T, Model>)
    {
        return ResourceMemoryPools->AllocateModel(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, ModelNode>)
    {
        return ResourceMemoryPools->AllocateNode(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, Mesh>)
    {
        return ResourceMemoryPools->AllocateMesh(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, Material>)
    {
        return ResourceMemoryPools->AllocateMaterial(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, Texture>)
    {
        return ResourceMemoryPools->AllocateTexture(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, Bone>)
    {
        return ResourceMemoryPools->AllocateBone(std::forward<Args>(args)...);
    }
    else if constexpr (std::is_same_v<T, Skeleton>)
    {
        return ResourceMemoryPools->AllocateSkeleton(std::forward<Args>(args)...);
    }
    else
    {
        static_assert(false, "Invalid resource type");
    }

}

template<ResourceType T>
static inline void DeallocateResource(T* resource)
{
    if constexpr (std::is_same_v<T, Model>)
    {
        ResourceMemoryPools->DeallocateModel(resource);
    }
    else if constexpr (std::is_same_v<T, ModelNode>)
    {
        ResourceMemoryPools->DeallocateNode(resource);
    }
    else if constexpr (std::is_same_v<T, Mesh>)
    {
        ResourceMemoryPools->DeallocateMesh(resource);
    }
    else if constexpr (std::is_same_v<T, Material>)
    {
        ResourceMemoryPools->DeallocateMaterial(resource);
    }
    else if constexpr (std::is_same_v<T, Texture>)
    {
        ResourceMemoryPools->DeallocateTexture(resource);
    }
    else if constexpr (std::is_same_v<T, Bone>)
    {
        ResourceMemoryPools->DeallocateBone(resource);
    }
    else if constexpr (std::is_same_v<T, Skeleton>)
    {
        ResourceMemoryPools->DeallocateSkeleton(resource);
    }
}
