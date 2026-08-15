#pragma once

#ifndef DYNAMICCPP_EXPORTS

#include "../DX12DeviceResources.h"

// DX12 자가 검증이 직접 만든 텍스처의 표 등록 수명.
//
// RenderGraph는 이 핸들을 빌려 쓸 뿐 등록을 소유하지 않는다. 정상 종료부는
// Reset으로 GPU 완료 뒤 명시 해제하고, 조기 실패 경로는 소멸자가 같은 해제를
// 보장한다. resources가 먼저 선언되므로 이 객체는 resource table보다 먼저
// 소멸해야 한다.
class DX12TestTextureRegistration final
{
public:
    DX12TestTextureRegistration() = default;

    DX12TestTextureRegistration(DX12DeviceResources& resources, ID3D12Resource* texture)
    {
        Register(resources, texture);
    }

    ~DX12TestTextureRegistration()
    {
        Reset();
    }

    DX12TestTextureRegistration(const DX12TestTextureRegistration&) = delete;
    DX12TestTextureRegistration& operator=(const DX12TestTextureRegistration&) = delete;

    void Register(DX12DeviceResources& resources, ID3D12Resource* texture)
    {
        Reset();
        m_resources = &resources;
        m_handle = resources.RegisterExternalTexture(texture);
    }

    void Reset()
    {
        if (nullptr != m_resources && m_handle.IsValid())
            m_resources->ReleaseTexture(m_handle);
        m_resources = nullptr;
        m_handle = {};
    }

    RHITextureHandle Handle() const { return m_handle; }
    bool IsValid() const { return m_handle.IsValid(); }

private:
    DX12DeviceResources* m_resources{ nullptr };
    RHITextureHandle     m_handle{};
};

// 같은 규약의 테스트 버퍼 판. raw ID3D12Resource*는 준비 단계에서만 보이고,
// 리드백 인코더에는 RHIBufferHandle만 전달한다.
class DX12TestBufferRegistration final
{
public:
    DX12TestBufferRegistration() = default;

    DX12TestBufferRegistration(DX12DeviceResources& resources, ID3D12Resource* buffer)
    {
        Register(resources, buffer);
    }

    ~DX12TestBufferRegistration()
    {
        Reset();
    }

    DX12TestBufferRegistration(const DX12TestBufferRegistration&) = delete;
    DX12TestBufferRegistration& operator=(const DX12TestBufferRegistration&) = delete;

    void Register(DX12DeviceResources& resources, ID3D12Resource* buffer)
    {
        Reset();
        m_resources = &resources;
        m_handle = resources.RegisterExternalBuffer(buffer);
    }

    void Reset()
    {
        if (nullptr != m_resources && m_handle.IsValid())
            m_resources->ReleaseBuffer(m_handle);
        m_resources = nullptr;
        m_handle = {};
    }

    RHIBufferHandle Handle() const { return m_handle; }
    bool IsValid() const { return m_handle.IsValid(); }

private:
    DX12DeviceResources* m_resources{ nullptr };
    RHIBufferHandle      m_handle{};
};

#endif
