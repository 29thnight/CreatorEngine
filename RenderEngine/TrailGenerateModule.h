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

