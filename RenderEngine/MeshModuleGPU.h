#pragma once
#include "RenderModules.h"
#include "Mesh.h"
#include "ShaderSystem.h"
#include "DeviceState.h"

enum class MeshType
{
    Cube,
    Sphere,
    Custom,
    None,
};

struct MeshConstantBuffer
{
    Mathf::Matrix world;
    Mathf::Matrix view;
    Mathf::Matrix projection;
    Mathf::Vector3 cameraPosition;
    float padding;
};

class MeshModuleGPU : public RenderModules
{
public:
    void Initialize() override;
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection) override;

    // 메시 설정
    void SetMeshType(MeshType type);
    void SetCustomMesh(Mesh* customMesh);  // 포인터로 변경

    // 파티클 데이터 설정 (빌보드 모듈과 동일한 방식)
    void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount) override;
    

    // 카메라 위치 설정 (라이팅용)
    void SetCameraPosition(const Mathf::Vector3& position);

    void Release();

private:
    void CreateCubeMesh();
    void CreateSphereMesh();
    void UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view,
        const Mathf::Matrix& projection);

private:
    // 메시 관리
    Mesh* m_currentMesh;
    bool m_ownsMesh;  // 현재 메시의 소유권 여부
    MeshType m_meshType;

    // DirectX 리소스
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_constantBuffer;

    // 파티클 데이터 (SRV로 받아옴)
    ID3D11ShaderResourceView* m_particleSRV;
    uint32_t m_instanceCount;

    // 상수 버퍼 데이터
    MeshConstantBuffer m_constantBufferData;
};