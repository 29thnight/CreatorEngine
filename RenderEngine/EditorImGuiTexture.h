#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstdint>

class Texture;
struct ID3D11ShaderResourceView;

// 에디터 UI가 ImGui::Image에 넘길 ImTextureID의 백엔드 중립 변환기
// (3-9 임시 승격 — ImGui DX12 셸).
//
// 왜 필요한가: DX12 백엔드에서 ImTextureID는 D3D12 GPU 디스크립터 핸들이다.
// 여기에 DX11 SRV 포인터를 그대로 넘기면 SetGraphicsRootDescriptorTable이
// 엉뚱한 주소를 참조해 디바이스가 그 자리에서 죽는다 — 캐스팅 문제가 아니라
// 즉사 문제라, 모든 표시 지점이 이 변환기를 거쳐야 한다.
namespace EditorImGuiTexture
{
    /// Texture 객체 표시. DX11 백엔드면 SRV 포인터를, DX12 셸이면 내용을
    /// 업로드(DX12TextureCache)한 셸 힙 슬롯의 GPU 핸들을 돌려준다.
    /// 셸 모드의 첫 프레임은 업로드 예약만 되어 0(빈 표시)일 수 있다.
    uint64_t From(Texture* texture);

    /// Texture 객체가 없는 원시 DX11 SRV(디버그 뷰 부류). DX12 셸에서는
    /// 표시할 방법이 없어 0(빈 표시)을 돌려준다 — 임시 승격의 알려진 한계고,
    /// 완전 통합에서 해소된다.
    uint64_t FromRawDx11Srv(ID3D11ShaderResourceView* srv);

    /// 스마트 포인터(shared_ptr·Managed::UniquePtr 등)도 그대로 받는다 —
    /// 호출부 21곳의 소유 형태가 제각각이라 여기서 흡수한다.
    template <typename TPtr>
    uint64_t From(const TPtr& pointer)
    {
        return From(pointer ? &*pointer : nullptr);
    }
}

#endif
