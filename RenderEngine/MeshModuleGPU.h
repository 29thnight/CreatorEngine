#pragma once
#include "Core.Minimal.h"
#include "Model.h"
#include "Texture.h"
#include "RenderModules.h"

enum class MeshType
{
    None,
    Cube,
    Sphere,
    Model
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
    void Initialize();
    void Release();

    // 메시 타입 설정
    void SetMeshType(MeshType type);

    // 모델 설정 (인덱스 기반)
    void SetModel(Model* model, int meshIndex = 0);

    // 모델 설정 (이름 기반)
    void SetModel(Model* model, const std::string_view& meshName);

    // 파티클 데이터 설정
    void SetParticleData(ID3D11ShaderResourceView* particleSRV, UINT instanceCount);

    // 카메라 위치 설정
    void SetCameraPosition(const Mathf::Vector3& position);

    // 텍스처 설정
    void SetTexture(Texture* texture);

    // 렌더링
    void Render(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection);

    // 상태 조회 함수들 (UI에서 사용)
    MeshType GetMeshType() const { return m_meshType; }
    Model* GetCurrentModel() const { return m_model; }
    int GetCurrentMeshIndex() const { return m_meshIndex; }
    UINT GetInstanceCount() const { return m_instanceCount; }
    Mesh* GetCurrentMesh() const;

private:
    // 내부 함수들
    void CreateCubeMesh();
    void CreateSphereMesh();
    void UpdateConstantBuffer(const Mathf::Matrix& world, const Mathf::Matrix& view,
        const Mathf::Matrix& projection);
 

private:
    std::unique_ptr<PipelineStateObject> m_pso;
    ComPtr<ID3D11Buffer> m_constantBuffer;
    MeshConstantBuffer m_constantBufferData;

    // 메시 관련
    MeshType m_meshType;
    Model* m_model;                    // 현재 사용 중인 모델
    int m_meshIndex;                   // 모델 내 메시 인덱스
    Mesh* m_tempCubeMesh;              // 임시 큐브 메시 (프리미티브용)

    // 파티클 관련
    ID3D11ShaderResourceView* m_particleSRV;
    UINT m_instanceCount;

    // 텍스처
    Texture* m_assignedTexture;        // 수동으로 할당된 텍스처
};