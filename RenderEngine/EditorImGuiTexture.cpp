#ifndef DYNAMICCPP_EXPORTS
#include "EditorImGuiTexture.h"
#include "RHI/DX12/ImGuiDx12Shell.h"
#include "Texture.h"

namespace EditorImGuiTexture
{
    uint64_t From(Texture* texture)
    {
        if (ImGuiDx12Shell::Get().IsActive())
        {
            // 널이어도 0을 돌려주지 않는다 — 헤더의 폴백 주석 참조.
            return ImGuiDx12Shell::Get().RegisterTexture(texture);
        }
        return (nullptr != texture) ? reinterpret_cast<uint64_t>(texture->m_pSRV) : 0;
    }

    uint64_t FromRawDx11Srv(ID3D11ShaderResourceView* srv)
    {
        // DX12 셸에서는 DX11 SRV를 표시할 방법이 없다. 그렇다고 0을 돌려주면
        // 커맨드 리스트가 죽으므로 폴백(빈 그림)을 돌려준다 — 임시 승격의
        // 알려진 한계이고, 완전 통합에서 해소된다.
        if (ImGuiDx12Shell::Get().IsActive())
        {
            return ImGuiDx12Shell::Get().GetFallbackTextureId();
        }
        return reinterpret_cast<uint64_t>(srv);
    }
}

#endif
