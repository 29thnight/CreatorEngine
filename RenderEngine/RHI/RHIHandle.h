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
