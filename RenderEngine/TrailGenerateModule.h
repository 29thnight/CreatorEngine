#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

struct alignas(16) TrailMeshParams
{
    float minDistance;              // 트레일 생성 최소 거리
    float trailWidth;               // 트레일 폭
    float deltaTime;                // 프레임 델타타임
    UINT maxParticles;              // 최대 파티클 수

    UINT enableTrail;               // 트레일 활성화 여부 (0/1)
    float velocityThreshold;        // 속도 임계값 (너무 느리면 트레일 안생성)
    float maxTrailLength;           // 최대 트레일 길이
    float widthOverLength;          // 길이에 따른 폭 감소 (0: 일정, 1: 선형감소)

    Mathf::Vector4 trailColor;      // 트레일 색상

    float uvTiling;                 // UV 타일링
    float uvScrollSpeed;            // UV 스크롤 속도
    float currentTime;              // 현재 시간
    float pad1;                     // 패딩
};

struct TrailVertex
{
    Mathf::Vector3 position;        // 정점 위치
    Mathf::Vector2 texcoord;        // UV 좌표
    Mathf::Vector4 color;           // 정점 색상
    Mathf::Vector3 normal;          // 법선 (라이팅용)
    float pad;                      // 패딩
};

class TrailGenerateModule : public ParticleModule, public ISerializable
{
public:
    TrailGenerateModule();
    // 인터페이스
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;

    // 직렬화
    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

    // generate 메서드
    virtual bool IsGenerateModule() const override { return true; }

    // 트레일 설정
    void SetMinDistance(float distance);
    void SetTrailWidth(float width);
    void SetVelocityThreshold(float threshold);
    void SetMaxTrailLength(float length);
    void SetWidthOverLength(float curve);
    void SetTrailColor(const Mathf::Vector4& color);
    void SetUVTiling(float tiling);
    void SetUVScrollSpeed(float speed);
    void EnableTrail(bool enable);

    // Getter
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer; }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer; }
    UINT GetVertexStride() const { return sizeof(TrailVertex); }
    const TrailMeshParams& GetTrailParams() const { return m_params; }
    UINT GetMaxVertexCount() const { return m_maxVertices; }
    UINT GetMaxIndexCount() const { return m_maxIndices; }

private:
    // 초기화
    bool InitializeComputeShader();
    bool CreateBuffers();
    bool CreateMeshBuffers();
    void UpdateConstantBuffer();
    void ReleaseResources();

    // 메쉬 생성
    void UpdateMeshGeneration();
    void ClearMeshData();

private:
    bool m_needsBufferClear;

    // 컴퓨트 셰이더 및 버퍼
    ID3D11ComputeShader* m_trailMeshCS;         // 트레일 메쉬 생성 컴퓨트 셰이더
    ID3D11Buffer* m_paramsBuffer;               // 트레일 파라미터 상수 버퍼

    // 이전 위치 저장 더블 버퍼
    ID3D11Buffer* m_prevPositionBufferA = nullptr;
    ID3D11Buffer* m_prevPositionBufferB = nullptr;
    ID3D11ShaderResourceView* m_prevPositionSRV_A = nullptr;
    ID3D11ShaderResourceView* m_prevPositionSRV_B = nullptr;
    ID3D11UnorderedAccessView* m_prevPositionUAV_A = nullptr;
    ID3D11UnorderedAccessView* m_prevPositionUAV_B = nullptr;

    bool m_usingPrevBufferA = true;

    // 동적 메쉬
    ID3D11Buffer* m_vertexBuffer;               // 생성된 정점 버퍼
    ID3D11Buffer* m_indexBuffer;                // 생성된 인덱스 버퍼
    ID3D11UnorderedAccessView* m_vertexUAV;     // 정점 쓰기용 UAV
    ID3D11UnorderedAccessView* m_indexUAV;      // 인덱스 쓰기용 UAV

    // 카운터
    ID3D11Buffer* m_counterBuffer;              // 카운터 버퍼
    ID3D11UnorderedAccessView* m_counterUAV;    // 카운터 UAV

    // 설정 및 상태
    TrailMeshParams m_params;
    bool m_paramsDirty;
    UINT m_maxVertices;                         // 최대 정점 수
    UINT m_maxIndices;                          // 최대 인덱스 수
    float m_totalTime;                          // 총 경과 시간

    // 상수
    static const UINT MAX_VERTICES_PER_PARTICLE = 4;  // 파티클당 최대 4개 정점 (quad)
    static const UINT MAX_INDICES_PER_PARTICLE = 6;   // 파티클당 최대 6개 인덱스 (2 triangles)
};

