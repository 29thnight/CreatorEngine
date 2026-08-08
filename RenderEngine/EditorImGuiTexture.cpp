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

        // ★ 여기 있던 DX11 폴백(texture->m_pSRV)을 걷었다 (T6, 2026-08-08).
        //
        //   Texture가 DX11 SRV를 드는 마지막 외부 소비처였다. 그것을 남겨 두면
        //   T6이 끝나지 않는다 — 셸이 기본으로 켜져 있고(EngineSetting, 590프레임
        //   실측) 이 경로는 셸 초기화가 실패했을 때만 도는데, 그 예외 상황 하나를
        //   위해 텍스처마다 DX11 뷰를 계속 만들어야 하기 때문이다.
        //
        //   지금 셸이 없으면 그림이 안 나온다. 그것이 잘못된 상태라는 표시이고,
        //   ImGuiRenderer가 셸 실패를 로그로 남긴다.
        return 0;
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
