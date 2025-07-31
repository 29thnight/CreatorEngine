#include "TrailGenerateModule.h"
#include "DeviceState.h"

TrailGenerateModule::TrailGenerateModule()
    : m_maxTrailPoints(100)
    , m_vertexCount(0)
    , m_indexCount(0)
    , m_trailLifetime(2.0f)
    , m_minDistance(0.1f)
    , m_startWidth(1.0f)
    , m_endWidth(0.1f)
    , m_startColor(1.0f, 1.0f, 1.0f, 1.0f)
    , m_endColor(1.0f, 1.0f, 1.0f, 0.0f)
    , m_useLengthBasedUV(true)
    , m_lastUpVector(0.0f, 1.0f, 0.0f)
    , m_meshDirty(false)
    , m_currentTime(0.0f)
    , m_position(0.0f, 0.0f, 0.0f)
    , m_lastPosition(0.0f, 0.0f, 0.0f)
    , m_positionOffset(0.0f, 0.0f, 0.0f)
    , m_autoGenerateFromPosition(true)
    , m_autoAddInterval(0.05f)
    , m_lastAutoAddTime(0.0f)
{
    m_stage = ModuleStage::RENDERING;
}

void TrailGenerateModule::Initialize()
{
    if (m_isInitialized)
        return;

    m_trailPoints.reserve(m_maxTrailPoints);
    m_vertices.reserve(m_maxTrailPoints * 2);
    m_indices.reserve((m_maxTrailPoints - 1) * 6);

    m_currentTime = 0.0f;
    m_meshDirty = false;

    m_vertexBufferSize = 0;
    m_indexBufferSize = 0;
    m_isInitialized = true;
}

void TrailGenerateModule::Release()
{
    m_vertexBuffer.Reset();
    m_indexBuffer.Reset();
    m_trailPoints.clear();
    m_vertices.clear();
    m_indices.clear();
    m_isInitialized = false;
}

void TrailGenerateModule::Update(float delta)
{
    if (!m_enabled || !m_isInitialized)
        return;

    m_currentTime += delta;

    // 자동 트레일 포인트 생성
    if (m_autoGenerateFromPosition &&
        (m_currentTime - m_lastAutoAddTime) >= m_autoAddInterval)
    {
        // 위치가 변했거나 처음 추가하는 경우
        if (m_trailPoints.empty() ||
            Mathf::Vector3::Distance(m_position, m_lastPosition) >= m_minDistance)
        {
            AddPoint(m_position, m_startWidth, m_startColor);
            m_lastPosition = m_position;
            m_lastAutoAddTime = m_currentTime;
        }
    }

    // 기존 Update 로직 (수명 관리, 이징 등)
    auto it = m_trailPoints.begin();
    while (it != m_trailPoints.end())
    {
        if (m_currentTime - it->timestamp > m_trailLifetime)
        {
            it = m_trailPoints.erase(it);
            m_meshDirty = true;
        }
        else
        {
            ++it;
        }
    }

    if (m_useEasing && !m_trailPoints.empty())
    {
        float normalizedTime = fmod(m_currentTime, m_easingDuration) / m_easingDuration;
        float easedValue = ApplyEasing(normalizedTime);

        for (auto& point : m_trailPoints)
        {
            float age = m_currentTime - point.timestamp;
            float lifeRatio = 1.0f - (age / m_trailLifetime);
            point.width *= (1.0f + easedValue * 0.2f);
            point.color.w *= lifeRatio;
        }
        m_meshDirty = true;
    }

    if (m_meshDirty && m_trailPoints.size() >= 2)
    {
        GenerateMesh();
        m_meshDirty = false;
    }
}

void TrailGenerateModule::AddPoint(const Mathf::Vector3& position, float width, const Mathf::Vector4& color)
{
    if (!m_enabled || !m_isInitialized)
        return;

    if (!m_trailPoints.empty())
    {
        Mathf::Vector3 lastPos = m_trailPoints.back().position;
        float distance = Mathf::Vector3::Distance(position, lastPos);
        if (distance < m_minDistance)
            return;
    }

    TrailPoint newPoint;
    newPoint.position = position;
    newPoint.timestamp = m_currentTime;
    newPoint.width = width;
    newPoint.color = color;

    m_trailPoints.push_back(newPoint);

    if (m_trailPoints.size() > m_maxTrailPoints)
    {
        m_trailPoints.erase(m_trailPoints.begin());
    }

    m_meshDirty = true;
}

void TrailGenerateModule::GenerateMesh()
{
    if (m_trailPoints.size() < 2)
    {
        m_vertexCount = 0;
        m_indexCount = 0;
        return;
    }

    m_vertices.clear();
    m_indices.clear();

    float totalLength = CalculateTrailLength();
    float currentLength = 0.0f;

    for (size_t i = 0; i < m_trailPoints.size(); ++i)
    {
        const TrailPoint& point = m_trailPoints[i];

        float lifeRatio = 1.0f;
        if (m_trailLifetime > 0.0f)
        {
            float age = m_currentTime - point.timestamp;
            lifeRatio = 1.0f - (age / m_trailLifetime);
            lifeRatio = std::max(0.0f, std::min(1.0f, lifeRatio));
        }

        float width = Mathf::Lerp(m_endWidth, m_startWidth, lifeRatio) * point.width;
        Mathf::Vector4 color = Mathf::Vector4::Lerp(m_endColor, m_startColor, lifeRatio);
        color = Mathf::Vector4::Lerp(color, point.color, 0.5f);

        Mathf::Vector3 forward = Mathf::Vector3::Zero;
        if (i == 0 && m_trailPoints.size() > 1)
        {
            forward = m_trailPoints[i + 1].position - point.position;
            forward.Normalize();
        }
        else if (i == m_trailPoints.size() - 1)
        {
            forward = point.position - m_trailPoints[i - 1].position;
            forward.Normalize();
        }
        else
        {
            Mathf::Vector3 toNext = m_trailPoints[i + 1].position - point.position;
            toNext.Normalize();
            Mathf::Vector3 fromPrev = point.position - m_trailPoints[i - 1].position;
            fromPrev.Normalize();
            forward = toNext + fromPrev;
            forward.Normalize();
        }

        m_lastUpVector = CalculateUpVector(forward, m_lastUpVector);
        Mathf::Vector3 right;
        forward.Cross(m_lastUpVector, right);
        right.Normalize();
        right *= width * 0.5f;

        float u = 0.0f;
        if (m_useLengthBasedUV && totalLength > 0.0f)
        {
            u = currentLength / totalLength;
        }
        else
        {
            u = static_cast<float>(i) / static_cast<float>(m_trailPoints.size() - 1);
        }

        TrailVertex leftVertex, rightVertex;
        leftVertex.position = point.position - right;
        leftVertex.texcoord = Mathf::Vector2(u, 0.0f);
        leftVertex.color = color;
        leftVertex.normal = m_lastUpVector;

        rightVertex.position = point.position + right;
        rightVertex.texcoord = Mathf::Vector2(u, 1.0f);
        rightVertex.color = color;
        rightVertex.normal = m_lastUpVector;

        m_vertices.push_back(leftVertex);
        m_vertices.push_back(rightVertex);

        if (i < m_trailPoints.size() - 1)
        {
            float nextDistance = 0.0f;
            if (i + 1 < m_trailPoints.size())
            {
                nextDistance = Mathf::Vector3::Distance(point.position, m_trailPoints[i + 1].position);
            }
            currentLength += nextDistance;
        }

        if (i > 0)
        {
            UINT baseIndex = static_cast<UINT>((i - 1) * 2);

            m_indices.push_back(baseIndex);
            m_indices.push_back(baseIndex + 1);
            m_indices.push_back(baseIndex + 2);

            m_indices.push_back(baseIndex + 1);
            m_indices.push_back(baseIndex + 3);
            m_indices.push_back(baseIndex + 2);
        }
    }

    CalculateNormals();
    UpdateBuffers();

    m_vertexCount = static_cast<UINT>(m_vertices.size());
    m_indexCount = static_cast<UINT>(m_indices.size());
}

void TrailGenerateModule::CalculateNormals()
{
    for (size_t i = 0; i < m_indices.size(); i += 3)
    {
        if (i + 2 >= m_indices.size()) break;

        UINT i0 = m_indices[i];
        UINT i1 = m_indices[i + 1];
        UINT i2 = m_indices[i + 2];

        if (i0 >= m_vertices.size() || i1 >= m_vertices.size() || i2 >= m_vertices.size())
            continue;

        Mathf::Vector3 v0 = m_vertices[i0].position;
        Mathf::Vector3 v1 = m_vertices[i1].position;
        Mathf::Vector3 v2 = m_vertices[i2].position;

        Mathf::Vector3 edge1 = v1 - v0;
        Mathf::Vector3 edge2 = v2 - v0;
        Mathf::Vector3 normal;
        edge1.Cross(edge2, normal);
        normal.Normalize();

        m_vertices[i0].normal = normal;
        m_vertices[i1].normal = normal;
        m_vertices[i2].normal = normal;
    }
}

float TrailGenerateModule::CalculateTrailLength() const
{
    float totalLength = 0.0f;
    for (size_t i = 1; i < m_trailPoints.size(); ++i)
    {
        totalLength += Mathf::Vector3::Distance(m_trailPoints[i - 1].position, m_trailPoints[i].position);
    }
    return totalLength;
}

Mathf::Vector3 TrailGenerateModule::CalculateUpVector(const Mathf::Vector3& forward, const Mathf::Vector3& lastUp) const
{
    if (abs(forward.Dot(lastUp)) > 0.99f)
    {
        Mathf::Vector3 fallback = Mathf::Vector3(0.0f, 1.0f, 0.0f);
        if (abs(forward.Dot(fallback)) > 0.99f)
        {
            fallback = Mathf::Vector3(1.0f, 0.0f, 0.0f);
        }
        return fallback;
    }

    Mathf::Vector3 right = forward.Cross(lastUp);
    right.Normalize();

    Mathf::Vector3 up = right.Cross(forward);
    up.Normalize();

    return up;
}

void TrailGenerateModule::RemoveOldPoints(float maxAge)
{
    if (maxAge < 0) maxAge = m_trailLifetime;

    auto it = m_trailPoints.begin();
    while (it != m_trailPoints.end())
    {
        if (m_currentTime - it->timestamp > maxAge)
        {
            it = m_trailPoints.erase(it);
            m_meshDirty = true;
        }
        else
        {
            ++it;
        }
    }
}

void TrailGenerateModule::UpdateBuffers()
{
    if (m_vertices.empty() || m_indices.empty())
        return;

    auto& deviceContext = DeviceState::g_pDeviceContext;
    UINT requiredVertexSize = static_cast<UINT>(m_vertices.size() * sizeof(TrailVertex));
    UINT requiredIndexSize = static_cast<UINT>(m_indices.size() * sizeof(UINT));

    // 버텍스 버퍼 처리
    if (!m_vertexBuffer || requiredVertexSize > m_vertexBufferSize)
    {
        // 새 버퍼가 필요한 경우 (약간의 여유공간 확보)
        m_vertexBufferSize = requiredVertexSize + (requiredVertexSize / 4);

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = m_vertexBufferSize;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ID3D11Buffer* buffer = nullptr;
        DirectX11::ThrowIfFailed(
            DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &buffer)
        );
        m_vertexBuffer.Attach(buffer);

        // 새 버퍼에 데이터 복사
        D3D11_MAPPED_SUBRESOURCE mapped;
        DirectX11::ThrowIfFailed(
            deviceContext->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)
        );
        memcpy(mapped.pData, m_vertices.data(), requiredVertexSize);
        deviceContext->Unmap(m_vertexBuffer.Get(), 0);
    }
    else
    {
        // 기존 버퍼 업데이트
        D3D11_MAPPED_SUBRESOURCE mapped;
        DirectX11::ThrowIfFailed(
            deviceContext->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)
        );
        memcpy(mapped.pData, m_vertices.data(), requiredVertexSize);
        deviceContext->Unmap(m_vertexBuffer.Get(), 0);
    }

    // 인덱스 버퍼 처리
    if (!m_indexBuffer || requiredIndexSize > m_indexBufferSize)
    {
        // 새 버퍼가 필요한 경우 (약간의 여유공간 확보)
        m_indexBufferSize = requiredIndexSize + (requiredIndexSize / 4);

        D3D11_BUFFER_DESC bufferDesc = {};
        bufferDesc.ByteWidth = m_indexBufferSize;
        bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
        bufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
        bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        ID3D11Buffer* buffer = nullptr;
        DirectX11::ThrowIfFailed(
            DeviceState::g_pDevice->CreateBuffer(&bufferDesc, nullptr, &buffer)
        );
        m_indexBuffer.Attach(buffer);

        // 새 버퍼에 데이터 복사
        D3D11_MAPPED_SUBRESOURCE mapped;
        DirectX11::ThrowIfFailed(
            deviceContext->Map(m_indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)
        );
        memcpy(mapped.pData, m_indices.data(), requiredIndexSize);
        deviceContext->Unmap(m_indexBuffer.Get(), 0);
    }
    else
    {
        // 기존 버퍼 업데이트
        D3D11_MAPPED_SUBRESOURCE mapped;
        DirectX11::ThrowIfFailed(
            deviceContext->Map(m_indexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)
        );
        memcpy(mapped.pData, m_indices.data(), requiredIndexSize);
        deviceContext->Unmap(m_indexBuffer.Get(), 0);
    }
}

void TrailGenerateModule::Clear()
{
    m_trailPoints.clear();
    m_vertices.clear();
    m_indices.clear();
    m_vertexCount = 0;
    m_indexCount = 0;
    m_meshDirty = false;
}

void TrailGenerateModule::ResetForReuse()
{
    if (!m_enabled)
        return;

    Clear();
    m_currentTime = 0.0f;
    m_lastUpVector = Mathf::Vector3(0.0f, 1.0f, 0.0f);
}

bool TrailGenerateModule::IsReadyForReuse() const
{
    return m_trailPoints.empty() && m_isInitialized;
}