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
}
