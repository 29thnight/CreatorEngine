#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

// 컴파일된 셰이더 바이트코드 — 백엔드 중립 (V5).
//
// ── 왜 이것이 필요한가 ──
//
// 소스는 §7.2.3 에서 파일이 됐고 컴파일은 RHIShaderCompiler 한 곳으로
// 모였다. 그런데 **결과**가 아직 `ComPtr<ID3DBlob>` 이라, 패스 39곳이
// 그 타입을 들고 다닌다(실측 66건). 즉 컴파일러를 갈아 끼워도 패스가
// 안 따라온다 — 경계가 반쪽이다.
//
// ★ V1 의 포맷, V4 의 루트 시그니처와 같은 부류다: 인터페이스만 중립이고
//   오가는 값이 백엔드 타입이면 두 번째 백엔드가 그 경계를 못 읽는다.
//
// ── 왜 소유(복사)하는가 ──
//
// ID3DBlob 을 불투명 포인터로 감싸 들 수도 있지만, 그러면 이 헤더가 COM
// 수명 규약을 알아야 한다. SPIR-V 는 COM 이 아니라 그냥 바이트 배열이라
// 그 규약이 Vulkan 쪽에서 뜻을 잃는다.
//
// ★ 비용은 초기화 때의 memcpy 한 번뿐이다. 셰이더 컴파일은 프레임 루프가
//   아니라 Initialize 에서 일어나고, 크기도 수 KB 다. 매 프레임 도는 것을
//   재는 자리가 아니므로 단순한 쪽을 고른다.

class RHIShaderBlob
{
public:
    RHIShaderBlob() = default;

    /// 백엔드가 컴파일 결과를 옮겨 담는다. DX12 는 ID3DBlob 에서,
    /// Vulkan 은 SPIR-V 워드 배열에서 온다.
    void Assign(const void* data, size_t size)
    {
        m_bytes.resize(size);
        if (0 != size && nullptr != data)
        {
            std::memcpy(m_bytes.data(), data, size);
        }
    }

    const void* Data() const { return m_bytes.empty() ? nullptr : m_bytes.data(); }
    size_t      Size() const { return m_bytes.size(); }
    bool        IsValid() const { return !m_bytes.empty(); }

private:
    std::vector<std::uint8_t> m_bytes;
};

#endif // !DYNAMICCPP_EXPORTS
