#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include "LightMapping.h"
#include "Animator.h"
#include "BillboardType.h"

#ifndef DYNAMICCPP_EXPORTS
#include "TerrainBuffers.h"
#include "FoliageType.h"
#include "FoliageInstance.h"

//============================================================
// Forward Decls
//============================================================
class Material;
class Mesh;
class OctreeNode;
class MeshRenderer;
class TerrainMesh;
class TerrainMaterial;
class TerrainComponent;
class FoliageComponent;
class DecalComponent;
class SpriteRenderer;
class Texture;
class ShaderPSO;
class Camera;

enum class PrimitiveProxyType
{
    MeshRenderer,
    FoliageComponent,
    TerrainComponent,
    DecalComponent,
    SpriteRenderer,
    Expired,
};
//분리 작업 중... 적용은 G-Star 이후에 할 예정
class PrimitiveRenderProxyBase
{
public:
    struct ProxyFilter
    {
        HashedGuid animatorGuid{};
        HashedGuid materialGuid{};
        uint32     LODLevel{ 0 };
        uint32     bitflag{ 0 };
        bool       LODEnabled{ false };

        ProxyFilter(size_t animator, size_t material, bool bLOD, uint32 level, uint32 flags)
            : animatorGuid(animator), materialGuid(material), LODEnabled(bLOD), LODLevel(level), bitflag(flags) {
        }
        auto operator<=>(const ProxyFilter&) const = default;
    };

public:
    explicit PrimitiveRenderProxyBase(PrimitiveProxyType type) : m_proxyType(type) {}
    virtual ~PrimitiveRenderProxyBase() = default;

    PrimitiveRenderProxyBase(const PrimitiveRenderProxyBase&) = default;
    PrimitiveRenderProxyBase(PrimitiveRenderProxyBase&&) noexcept = default;
    PrimitiveRenderProxyBase& operator=(const PrimitiveRenderProxyBase&) = default;
    PrimitiveRenderProxyBase& operator=(PrimitiveRenderProxyBase&&) noexcept = default;

    // -------- Lifecycle / Render --------
    virtual void Draw(ID3D11DeviceContext* _deferredContext) {}
    //void DrawShadow(ID3D11DeviceContext* _deferredContext) {};
    //virtual void DrawInstanced(ID3D11DeviceContext* _deferredContext, size_t count) {}
    void DestroyProxy() {}

    // -------- Common flags --------
    bool IsNeedUpdateCulling() const { return m_isNeedUpdateCulling; }
    void SetNeedUpdateCulling(bool able) { m_isNeedUpdateCulling = able; }

public:
    // -------- Common properties (진짜 공통) --------
    Mathf::Vector3      m_worldPosition{ 0.0f, 0.0f, 0.0f };
    PrimitiveProxyType  m_proxyType{ PrimitiveProxyType::Expired };
    HashedGuid          m_instancedID{};
    Mathf::xMatrix      m_worldMatrix{ XMMatrixIdentity() };
    bool                m_isCulled{ false };
    bool                m_isStatic{ false };

private:
    bool                m_isNeedUpdateCulling{ false };
};
#endif // !DYNAMICCPP_EXPORTS