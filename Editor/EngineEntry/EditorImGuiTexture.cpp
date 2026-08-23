#include "EditorImGuiTexture.h"
#include "RHI/IImGuiHost.h"
#include "Texture.h"

namespace EditorImGuiTexture
{
    bool IsHostActive() noexcept
    {
        return GetImGuiHost().IsActive();
    }

    uint64_t From(Texture* texture)
    {
        IImGuiHost& host = GetImGuiHost();
        if (host.IsActive())
        {
            // 널이어도 0을 돌려주지 않는다 — 헤더의 폴백 주석 참조.
            return host.RegisterTexture(texture);
        }

        // ★ 여기 있던 DX11 폴백(texture->m_pSRV)을 걷었다 (T6, 2026-08-08).
        //
        //   Texture가 DX11 SRV를 드는 마지막 외부 소비처였다. 그것을 남겨 두면
        //   T6이 끝나지 않는다 — Editor 셸이 기본으로 켜져 있고(590프레임 실측)
        //   이 경로는 셸 초기화가 실패했을 때만 도는데, 그 예외 상황 하나를
        //   위해 텍스처마다 DX11 뷰를 계속 만들어야 하기 때문이다.
        //
        //   지금 셸이 없으면 그림이 안 나온다. 그것이 잘못된 상태라는 표시이고,
        //   공통 ImGuiHost가 선택된 렌더러 백엔드 실패를 로그로 남긴다.
        return 0;
    }

    // ★ FromRawDx11Srv를 걷었다 (E, 2026-08-09).
    //
    //   "Texture 객체 없이 원시 DX11 SRV만 든 디버그 뷰 부류"를 위한 변환기로,
    //   셸에서는 표시할 방법이 없어 빈 그림을 돌려주고 DX11 백엔드에서는 SRV를
    //   그대로 ImTextureID로 넘겼다.
    //
    //   호출자가 0이다. 그럴 만한 이유가 있다 - 그 '디버그 뷰 부류'였던
    //   RenderDebugManager가 T6에서 걷혔고(패스 결과를 ID3D11DeviceContext로
    //   복사해 두던 물건), DX11 백엔드 자체도 D4에서 사라졌다. 남은 쪽 가지는
    //   빈 그림을 돌려주는 것뿐이라 부를 이유도 없다.
    //
    //   RHI 텍스처를 ImGui에 넘기는 길은 From(Texture*) 하나로 모였다.
}
