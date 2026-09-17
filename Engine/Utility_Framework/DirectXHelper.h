#pragma once
#include "Core.Definition.h"

// COM/HRESULT 오류 처리 (2026-08-10 재작성).
//
// ── 무엇이 사라졌나 ──
//
// 이 헤더는 DX11 시절의 잡동사니 주머니였다. 소비자를 세 보니 살아 있는 것이
// ThrowIfFailed 하나였고 나머지는 전부 0이었다:
//
//   namespace DirectX11 — ShaderMacroTerminator · ReadDataAsync ·
//     ConvertDipsToPixels · ReleaseResources · SharedMap · SdkLayersAvailable
//   namespace DirectX   — CreateTGATextureFormFile(Ex) · CreateTextureFromFile ×2 ·
//     SetName · PointerToString · NoSRGB · MTGuard · DXObjects 개념
//
// ★ SetName은 19건이 잡혀 오래 살아 있는 줄 알았는데, 세어 보니 전부
//   ID3D12Object::SetName 멤버 호출이었다. 이 헤더의 자유 함수를 부르는 곳은
//   0이다 — 이름이 같아서 죽은 것이 살아 보였다.
//
// 그 죽은 것들이 <d3d11.h> · <DirectXTex.h> · <ppltasks.h>를 끌고 있었다.
// 셋 다 함께 사라졌다.
//
// ── 왜 namespace Win32인가 ──
//
// 예전 이름은 DirectX11이었다. 그런데 여기 남은 둘은 DirectX가 아니라 COM의
// HRESULT를 다룬다 — DX12 코드도 같은 것을 쓴다. 백엔드 이름을 붙여 두면
// "DX11을 지우려면 이것도 지워야 한다"는 잘못된 신호를 계속 낸다.
namespace Win32
{
    // ★ 예전에는 `static std::exception CreateException(HRESULT)` 가 이 타입을
    //   **값으로** std::exception 에 담아 돌려줬고 ThrowIfFailed 가 그것을 던졌다.
    //   던지는 순간 잘려(slicing) what() 이 MSVC 기본값 "Unknown exception" 이 됐고
    //   HRESULT 는 사라졌다. dx12.selftest 가 9-14 부터 그 한 줄만 남기고 붉었다 —
    //   원인(WIC 가 .shadermeta 를 그림으로 못 읽음)은 cdb 스택으로만 보였다.
    //   파생 타입 그대로 던진다. 문장은 객체가 들고 있다(정적 버퍼는 스레드끼리 덮는다).
    class ComException : public std::exception
    {
    public:
        explicit ComException(HRESULT hr) : m_result(hr)
        {
            sprintf_s(m_message, "Failure with HRESULT of %08X", static_cast<unsigned long>(hr));
        }

        const char* what() const override { return m_message; }
        HRESULT Result() const noexcept { return m_result; }

    private:
        HRESULT m_result{};
        char m_message[48]{};
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw ComException(hr);
        }
    }
}
