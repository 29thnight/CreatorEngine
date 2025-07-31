#pragma once
#include "ParticleModule.h"
#include "ISerializable.h"

struct alignas(16) TrailMeshParams
{
    float minDistance;              // Ʈ���� ���� �ּ� �Ÿ�
    float trailWidth;               // Ʈ���� ��
    float deltaTime;                // ������ ��ŸŸ��
    UINT maxParticles;              // �ִ� ��ƼŬ ��

    UINT enableTrail;               // Ʈ���� Ȱ��ȭ ���� (0/1)
    float velocityThreshold;        // �ӵ� �Ӱ谪 (�ʹ� ������ Ʈ���� �Ȼ���)
    float maxTrailLength;           // �ִ� Ʈ���� ����
    float widthOverLength;          // ���̿� ���� �� ���� (0: ����, 1: ��������)

    Mathf::Vector4 trailColor;      // Ʈ���� ����

    float uvTiling;                 // UV Ÿ�ϸ�
    float uvScrollSpeed;            // UV ��ũ�� �ӵ�
    float currentTime;              // ���� �ð�
    float pad1;                     // �е�
};

struct TrailVertex
{
    Mathf::Vector3 position;        // ���� ��ġ
    Mathf::Vector2 texcoord;        // UV ��ǥ
    Mathf::Vector4 color;           // ���� ����
    Mathf::Vector3 normal;          // ���� (�����ÿ�)
    float pad;                      // �е�
};

class TrailGenerateModule : public ParticleModule, public ISerializable
{
public:
    TrailGenerateModule();
    // �������̽�
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;

    // ����ȭ
    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

    // generate �޼���
    virtual bool IsGenerateModule() const override { return true; }

    // Ʈ���� ����
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
    // �ʱ�ȭ
    bool InitializeComputeShader();
    bool CreateBuffers();
    bool CreateMeshBuffers();
    void UpdateConstantBuffer();
    void ReleaseResources();

    // �޽� ����
    void UpdateMeshGeneration();
    void ClearMeshData();

private:
    bool m_needsBufferClear;

    // ��ǻƮ ���̴� �� ����
    ID3D11ComputeShader* m_trailMeshCS;         // Ʈ���� �޽� ���� ��ǻƮ ���̴�
    ID3D11Buffer* m_paramsBuffer;               // Ʈ���� �Ķ���� ��� ����

    // ���� ��ġ ���� ���� ����
    ID3D11Buffer* m_prevPositionBufferA = nullptr;
    ID3D11Buffer* m_prevPositionBufferB = nullptr;
    ID3D11ShaderResourceView* m_prevPositionSRV_A = nullptr;
    ID3D11ShaderResourceView* m_prevPositionSRV_B = nullptr;
    ID3D11UnorderedAccessView* m_prevPositionUAV_A = nullptr;
    ID3D11UnorderedAccessView* m_prevPositionUAV_B = nullptr;

    bool m_usingPrevBufferA = true;

    // ���� �޽�
    ID3D11Buffer* m_vertexBuffer;               // ������ ���� ����
    ID3D11Buffer* m_indexBuffer;                // ������ �ε��� ����
    ID3D11UnorderedAccessView* m_vertexUAV;     // ���� ����� UAV
    ID3D11UnorderedAccessView* m_indexUAV;      // �ε��� ����� UAV

    // ī����
    ID3D11Buffer* m_counterBuffer;              // ī���� ����
    ID3D11UnorderedAccessView* m_counterUAV;    // ī���� UAV

    // ���� �� ����
    TrailMeshParams m_params;
    bool m_paramsDirty;
    UINT m_maxVertices;                         // �ִ� ���� ��
    UINT m_maxIndices;                          // �ִ� �ε��� ��
    float m_totalTime;                          // �� ��� �ð�

    // ���
    static const UINT MAX_VERTICES_PER_PARTICLE = 4;  // ��ƼŬ�� �ִ� 4�� ���� (quad)
    static const UINT MAX_INDICES_PER_PARTICLE = 6;   // ��ƼŬ�� �ִ� 6�� �ε��� (2 triangles)
};

