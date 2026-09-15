#pragma once
#include <cstdint>

class Texture;

// 에디터 UI가 ImGui::Image에 넘길 ImTextureID의 백엔드 중립 변환기
// (DX12 GPU descriptor handle / Vulkan descriptor set을 공통 Host 뒤로 숨긴다).
//
// 왜 필요한가: DX12 백엔드에서 ImTextureID는 D3D12 GPU 디스크립터 핸들이다.
// 여기에 DX11 SRV 포인터를 그대로 넘기면 SetGraphicsRootDescriptorTable이
// 엉뚱한 주소를 참조해 디바이스가 그 자리에서 죽는다 — 캐스팅 문제가 아니라
// 즉사 문제라, 모든 표시 지점이 이 변환기를 거쳐야 한다.
namespace EditorImGuiTexture
{
    /// 선택된 UI Host의 context와 renderer backend이 모두 살아 있는지 본다.
    bool IsHostActive() noexcept;

    /// Texture 객체 표시. 선택된 ImGui RHI backend가 내용을 업로드하고 해당
    /// API의 ImTextureID를 돌려준다. Host가 꺼져 있으면 0이다.
    ///
    /// FromRawDx11Srv가 여기 있었다 — 호출자 0으로 걷었다(E). .cpp 주석 참고.
    uint64_t From(Texture* texture);

    /// 스마트 포인터(shared_ptr·std::unique_ptr 등)도 그대로 받는다 —
    /// 호출부 21곳의 소유 형태가 제각각이라 여기서 흡수한다.
    template <typename TPtr>
    uint64_t From(const TPtr& pointer)
    {
        return From(pointer ? &*pointer : nullptr);
    }

    // ── 비동기 썸네일이 쓰는 둘 (PHASE 21 W7) ──
    //
    // ★ From 의 반환값은 "준비됐다" 가 아니다. DX12 는 표시 프레임 밖 호출에서
    //   슬롯만 예약하고 null SRV 를 써 두므로 업로드 전에도 0 이 아닌 ID 가
    //   나온다. 그것을 Ready 로 읽으면 한 프레임 빈 그림이 나오고, 그것이
    //   계약이 금지한 상태다.

    /// 그림을 내지 않은 채 **업로드만 시킨다.** 표시 프레임 안에서 불러야 한다.
    ///
    /// 왜 필요한가: 업로드는 RegisterTexture 가 열린 프레임에서 한다. 준비될
    /// 때까지 유형 아이콘만 그리는 타일은 썸네일 텍스처를 한 번도 등록하지
    /// 않으므로, 이 창구가 없으면 **영원히 안 올라간다** — 준비를 기다리는
    /// 쪽이 준비를 막는 교착이다.
    void Prime(Texture* texture);

    /// 픽셀이 실제로 GPU 에 올라갔는가. 부수 효과가 없다.
    bool IsReady(Texture* texture);

    template <typename TPtr>
    bool IsReady(const TPtr& pointer)
    {
        return IsReady(pointer ? &*pointer : nullptr);
    }
}
