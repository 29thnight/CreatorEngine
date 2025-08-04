#pragma once
#include "Core.Minimal.h"
#include "ParticleModule.h"
#include "ISerializable.h"

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

class TrailGenerateModule : public ParticleModule, public ISerializable
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
    void SetEmitterPosition(const Mathf::Vector3& position) { m_position = position + m_positionOffset; }
    void SetPositionOffset(const Mathf::Vector3& offset) { m_positionOffset = offset; }
    void SetAutoGenerationSettings(bool enable, float interval) { m_autoGenerateFromPosition = enable; m_autoAddInterval = interval; }
    void SetMaxTrailPoints(UINT maxPoints) { m_maxTrailPoints = maxPoints; }
    void SetCurrentTime(float time) { m_currentTime = time; }
    void SetLastPosition(const Mathf::Vector3& position) { m_lastPosition = position; }
    void SetMeshDirty(bool dirty) { m_meshDirty = dirty; }
    void SetLastUpVector(const Mathf::Vector3& upVector) { m_lastUpVector = upVector; }
    void SetStartWidth(float width) { m_startWidth = width; }
    void SetEndWidth(float width) { m_endWidth = width; }
    void SetStartColor(const Mathf::Vector4& color) { m_startColor = color; }
    void SetEndColor(const Mathf::Vector4& color) { m_endColor = color; }

    bool IsInitialized() const { return m_isInitialized; }
    ID3D11Buffer* GetVertexBuffer() const { return m_vertexBuffer.Get(); }
    ID3D11Buffer* GetIndexBuffer() const { return m_indexBuffer.Get(); }
    UINT GetIndexCount() const { return m_indexCount; }
    UINT GetMaxIndexCount() const { return m_indexCount; }
    UINT GetVertexCount() const { return m_vertexCount; }
    UINT GetMaxTrailPoints() const { return m_maxTrailPoints; }
    float GetTrailLifetime() const { return m_trailLifetime; }
    float GetMinDistance() const { return m_minDistance; }
    bool GetAutoGenerateFromPosition() const { return m_autoGenerateFromPosition; }
    float GetAutoAddInterval() const { return m_autoAddInterval; }
    Mathf::Vector3 GetPositionOffset() const { return m_positionOffset; }
    Mathf::Vector3 GetPosition() const { return m_position; }
    Mathf::Vector3 GetLastPosition() const { return m_lastPosition; }
    float GetCurrentTime() const { return m_currentTime; }
    bool IsMeshDirty() const { return m_meshDirty; }
    Mathf::Vector3 GetLastUpVector() const { return m_lastUpVector; }
    float GetStartWidth() const { return m_startWidth; }
    float GetEndWidth() const { return m_endWidth; }
    Mathf::Vector4 GetStartColor() const { return m_startColor; }
    Mathf::Vector4 GetEndColor() const { return m_endColor; }
    bool IsUsingLengthBasedUV() const { return m_useLengthBasedUV; }
    size_t GetActivePointCount() const { return m_trailPoints.size(); }
    bool HasValidMesh() const { return m_indexCount > 0 && m_vertexCount > 0; }

    void ForceUpdateMesh() { m_meshDirty = true; GenerateMesh(); }

    void RemoveOldPoints(float maxAge = -1.0f);
    

    const std::vector<TrailPoint>& GetTrailPoints() const { return m_trailPoints; }
    const std::vector<TrailVertex>& GetVertices() const { return m_vertices; }
    const std::vector<UINT>& GetIndices() const { return m_indices; }

    virtual nlohmann::json SerializeData() const override;
    virtual void DeserializeData(const nlohmann::json& json) override;
    virtual std::string GetModuleType() const override;

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