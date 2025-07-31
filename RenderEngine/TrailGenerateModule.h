#pragma once
#include "Core.Minimal.h"
#include "ParticleModule.h"

struct TrailVertex
{
    Mathf::Vector3 position;
    Mathf::Vector2 texcoord;
    Mathf::Vector4 color;
    Mathf::Vector3 normal;
};

struct TrailPoint
{
    Mathf::Vector3 position;
    float timestamp;
    float width;
    Mathf::Vector4 color;
};

class TrailGenerateModule : public ParticleModule
{
public:
    TrailGenerateModule();

    void Initialize() override;
    void Update(float delta) override;
    void Release() override;
    void ResetForReuse() override;
    bool IsReadyForReuse() const override;
    bool IsGenerateModule() const override { return true; }

    void AddPoint(const Mathf::Vector3& position, float width = 1.0f, const Mathf::Vector4& color = Mathf::Vector4(1, 1, 1, 1));
    void GenerateMesh();
    void Clear();

    void SetTrailLifetime(float lifetime) { m_trailLifetime = lifetime; }
    void SetMinDistance(float distance) { m_minDistance = distance; }
    void SetWidthCurve(float startWidth, float endWidth) { m_startWidth = startWidth; m_endWidth = endWidth; }
    void SetColorCurve(const Mathf::Vector4& startColor, const Mathf::Vector4& endColor) { m_startColor = startColor; m_endColor = endColor; }
    void SetUVMode(bool useLengthBased) { m_useLengthBasedUV = useLengthBased; }

    void SetPosition(const Mathf::Vector3& position)
    {
        m_position = position + m_positionOffset;
    }

    void SetPositionOffset(const Mathf::Vector3& offset)
    {
        m_positionOffset = offset;
    }

    void SetAutoGenerationSettings(bool enable, float interval)
    {
        m_autoGenerateFromPosition = enable;
        m_autoAddInterval = interval;
    }

    bool IsInitialized() const { return m_isInitialized; }

    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetMaxIndexCount() const { return m_indexCount; }
    UINT GetVertexCount() const { return m_vertexCount; }
    int GetMaxTrailPoints() const { return m_maxTrailPoints; }
    float GetTrailLifetime() const { return m_trailLifetime; }
    float GetMinDistance() const { return m_minDistance; }
    bool GetAutoGenerateFromPosition() const { return m_autoGenerateFromPosition; }
    float GetAutoAddInterval() const { return m_autoAddInterval; }
    Mathf::Vector3 GetPositionOffset() const { return m_positionOffset; }

private:
    void UpdateBuffers();
    void CalculateNormals();
    float CalculateTrailLength() const;
    Mathf::Vector3 CalculateUpVector(const Mathf::Vector3& forward, const Mathf::Vector3& lastUp) const;

private:
    std::vector<TrailPoint> m_trailPoints;
    std::vector<TrailVertex> m_vertices;
    std::vector<UINT> m_indices;

    ComPtr<ID3D11Buffer> m_vertexBuffer;
    ComPtr<ID3D11Buffer> m_indexBuffer;

    UINT m_vertexBufferSize;
    UINT m_indexBufferSize;

    UINT m_maxTrailPoints;
    UINT m_vertexCount;
    UINT m_indexCount;

    float m_trailLifetime;
    float m_minDistance;
    float m_startWidth;
    float m_endWidth;
    Mathf::Vector4 m_startColor;
    Mathf::Vector4 m_endColor;
    bool m_useLengthBasedUV;

    Mathf::Vector3 m_lastUpVector;
    bool m_meshDirty;
    float m_currentTime;

    Mathf::Vector3 m_position;
    Mathf::Vector3 m_lastPosition;
    
    Mathf::Vector3 m_positionOffset;

    bool m_autoGenerateFromPosition;
    float m_autoAddInterval;
    float m_lastAutoAddTime;
};