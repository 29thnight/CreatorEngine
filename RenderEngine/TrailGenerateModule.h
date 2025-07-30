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

class TrailGenerateModul : public ParticleModule, public ISerializable
{
    virtual void Initialize() override;
    virtual void Update(float deltaTime) override;
    virtual void Release() override;
    virtual void OnSystemResized(UINT maxParticles) override;
    virtual void OnParticleSystemPositionChanged(const Mathf::Vector3& newPosition) override;

    virtual void ResetForReuse();
    virtual bool IsReadyForReuse() const;


    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;


};

