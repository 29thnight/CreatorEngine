#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>

#include "../RHIHandle.h"

// 핸들 → 리소스 표 (PHASE 3-1 재정의, V2-a).
//
// ── 무엇을 하고 무엇을 안 하는가 ──
//
// 한다: 핸들 발급, 조회, 놓기.
// 안 한다: 수명 정책 변경. ComPtr이 하던 참조 세기를 그대로 표가 든다 —
//         패스 소유는 Shutdown까지, transient는 그래프 수명까지, 임포트는
//         소유하지 않는다.
//
// ★ 정책을 이 슬라이스에서 함께 바꾸지 않는 것이 설계다(§7.2.1 ③).
//   표현을 바꾸는 것과 정책을 바꾸는 것을 겹치면, 회귀가 났을 때 어느 쪽
//   때문인지 못 가른다. 지금은 "누가 소유하는가"가 한 줄도 안 바뀐다.
//
// ── 왜 세대(generation)를 안 넣었나 ──
//
// 넣으면 놓은 핸들을 다시 쓰는 것을 잡을 수 있다. 그런데 지금 구조에서는
// 놓는 시점이 셋뿐이고(패스 Shutdown · 그래프 소멸 · 리사이즈) 전부 그
// 핸들을 든 쪽이 함께 죽는다 — 즉 잡을 사고가 아직 없다. 재사용을 시작하는
// 순간(풀링) 필요해지므로, 그때 id의 상위 비트로 넣는다. 지금 넣으면 쓰지
// 않는 검사를 매 조회마다 도는 것이고, 틀려도 아무도 모른다.
class DX12ResourceTable
{
public:
    /// 소유하고 등록한다. 표가 ComPtr을 들고 있으므로 호출부는 핸들만 남긴다.
    RHITextureHandle AddTexture(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (nullptr == resource.Get()) return {};
        m_textures.push_back(std::move(resource));
        return RHITextureHandle{ static_cast<uint32_t>(m_textures.size()) };
    }

    RHIBufferHandle AddBuffer(Microsoft::WRL::ComPtr<ID3D12Resource> resource)
    {
        if (nullptr == resource.Get()) return {};
        m_buffers.push_back(std::move(resource));
        return RHIBufferHandle{ static_cast<uint32_t>(m_buffers.size()) };
    }

    /// 소유하지 않고 등록한다 — 임포트(스왑체인 백버퍼·자가 검증이 만든 텍스처).
    ///
    /// ★ 소유 여부가 표에 남지 않는 것이 의도다. 표는 "이 핸들이 어느
    ///   리소스인가"만 답하고, 누가 살려 두는가는 지금까지대로 호출부의
    ///   책임이다. 그것을 표가 가져가는 것은 수명 정책 변경이고 V2 범위 밖이다.
    RHITextureHandle AddExternalTexture(ID3D12Resource* resource)
    {
        if (nullptr == resource) return {};
        m_textures.emplace_back();          // 소유하지 않는다
        m_external.push_back(resource);
        m_externalIndex.push_back(static_cast<uint32_t>(m_textures.size()));
        return RHITextureHandle{ static_cast<uint32_t>(m_textures.size()) };
    }

    ID3D12Resource* Resolve(RHITextureHandle handle) const
    {
        if (!handle.IsValid() || handle.id > m_textures.size()) return nullptr;
        if (auto* owned = m_textures[handle.id - 1].Get()) return owned;
        return ResolveExternal(handle.id);
    }

    ID3D12Resource* Resolve(RHIBufferHandle handle) const
    {
        if (!handle.IsValid() || handle.id > m_buffers.size()) return nullptr;
        return m_buffers[handle.id - 1].Get();
    }

    void Clear()
    {
        m_textures.clear();
        m_buffers.clear();
        m_external.clear();
        m_externalIndex.clear();
    }

    size_t TextureCount() const { return m_textures.size(); }
    size_t BufferCount() const { return m_buffers.size(); }

private:
    ID3D12Resource* ResolveExternal(uint32_t id) const
    {
        for (size_t i = 0; i < m_externalIndex.size(); ++i)
        {
            if (m_externalIndex[i] == id) return m_external[i];
        }
        return nullptr;
    }

    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_textures;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_buffers;

    // 임포트는 드물다(프레임당 한 자릿수) — 선형 탐색으로 충분하고,
    // 자료구조를 늘리는 것보다 읽기 쉬운 쪽을 골랐다. 임포트가 늘면 그때 잰다.
    std::vector<ID3D12Resource*> m_external;
    std::vector<uint32_t>        m_externalIndex;
};

#endif
