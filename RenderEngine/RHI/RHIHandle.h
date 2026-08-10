#pragma once
#include <cstdint>

// 리소스 핸들 — 백엔드 중립 (PHASE 3-1 재정의, V2).
//
// ── 왜 포인터가 아닌가 ──
//
// 계획서 §3.2가 적어 둔 셋이 그대로다: 백엔드가 리소스를 재배치·풀링해도
// 상위가 모르고, 값 복사가 싸며, 잘못된 포인터를 역참조할 길이 없다.
//
// ★ Vulkan이 이 결정을 강제한다. 거기서 텍스처 하나는 VkImage +
//   VkDeviceMemory + VkImageView가 한 덩어리이고, 포인터 하나로는 그 셋을
//   가리킬 수 없다. 핸들이 그 셋을 묶는 자리가 된다 — DX12에서는
//   ID3D12Resource* 하나로 충분하지만, 그 사실이 어휘를 정하면 안 된다.
//
// ── 0이 무효인 이유 ──
//
// 값 초기화(RHITextureHandle{})가 곧 "없음"이 되게 한다. 표의 첫 칸을 비워
// 두는 값이고, 그래서 "핸들을 안 넣었다"와 "0번을 넣었다"가 구분된다.
//
// ── id 안에 무엇이 들어 있나 (V2-c1) ──
//
//     상위 16비트 = 세대(generation)   하위 16비트 = 칸 번호 + 1
//
// V2-a가 세대를 안 넣으면서 그 조건을 적어 뒀다: "재사용을 시작하는
// 순간(풀링) 필요해지므로, 그때 id의 상위 비트로 넣는다."
//
// ★ V2-c가 그 순간이다. 그래프는 프레임마다 새로 서는데(실측:
//   EnhancedSceneRendererLive가 프레임·뷰마다 make_unique) 그래프 리소스를
//   표에 넣으려면 표가 놓을 줄 알아야 하고, 놓은 칸은 다음 프레임이 다시
//   쓴다. 세대가 없으면 지난 프레임 핸들이 이번 프레임의 남의 리소스로
//   조용히 풀린다 — 틀린 텍스처로 그리는데 아무도 안 죽는 종류의 사고다.
//
// 세대 16비트는 같은 칸이 65536번 재사용되면 한 바퀴 돈다(60fps에서 약
// 18분). 그때 정확히 65536프레임 묵은 핸들이 남아 있어야 오탐을 놓치므로,
// 실질적으로는 못 잡는 경우가 없다. 32비트를 통째로 세대에 주려면 핸들이
// 8바이트가 되는데, 값으로 굴리는 타입이라 4바이트를 지켰다.
namespace RHIHandleBits
{
    constexpr uint32_t kIndexBits = 16;
    constexpr uint32_t kIndexMask = (1u << kIndexBits) - 1u;

    /// 칸 번호(0-기반)와 세대로 id를 만든다. 칸 번호에 1을 더해 0을 비워 둔다.
    constexpr uint32_t Encode(uint32_t slot, uint32_t generation)
    {
        return ((generation & kIndexMask) << kIndexBits) | ((slot + 1u) & kIndexMask);
    }
    constexpr uint32_t SlotOf(uint32_t id) { return (id & kIndexMask) - 1u; }
    constexpr uint32_t GenerationOf(uint32_t id) { return (id >> kIndexBits) & kIndexMask; }

    /// 표가 가질 수 있는 칸 수. 넘으면 발급이 실패한다(조용히 겹치지 않는다).
    constexpr uint32_t kMaxSlots = kIndexMask - 1u;
}

struct RHITextureHandle
{
    uint32_t id{ 0 };

    bool IsValid() const { return 0 != id; }
    bool operator==(const RHITextureHandle& other) const { return id == other.id; }
    bool operator!=(const RHITextureHandle& other) const { return id != other.id; }
};

struct RHIBufferHandle
{
    uint32_t id{ 0 };

    bool IsValid() const { return 0 != id; }
    bool operator==(const RHIBufferHandle& other) const { return id == other.id; }
    bool operator!=(const RHIBufferHandle& other) const { return id != other.id; }
};

// ★ 텍스처와 버퍼를 나눈 이유: 둘은 만들 수 있는 뷰가 다르고(RTV/DSV는
//   텍스처만), Vulkan에서는 타입 자체가 갈린다(VkImage vs VkBuffer).
//   한 타입으로 두면 "버퍼를 렌더 타깃으로 걸었다" 같은 실수가 컴파일된다 —
//   R3가 RHISamplerTable을 RHIBindingTable과 갈라 둔 것과 같은 이유다.
