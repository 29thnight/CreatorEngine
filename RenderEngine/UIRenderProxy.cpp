#include "UIRenderProxy.h"
#include "RenderScene.h"
#include "Texture.h"
#include "ShaderSystem.h"
#include "SpriteSheet.h"
#include <DirectXMath.h>
#include <algorithm>

// 클리핑 계산은 UIClipping.h로 옮겼다. DX12 UI 패스가 같은 계산을
// 해야 하는데, 두 곳에 따로 두면 하나가 틀려도 알 수 없다.
#include "UIClipping.h"

UIRenderProxy::~UIRenderProxy()
{
    std::visit(
        [](auto& info)
        {
            using T = std::decay_t<decltype(info)>;
            if constexpr (std::is_same_v<T, ImageData>)
            {
                info.texture.reset();
                info.textures.clear();
            }
            else if constexpr (std::is_same_v<T, TextData>)
            {
                info.message.clear();
            }
            else if constexpr (std::is_same_v<T, SpriteSheetData>)
            {
                info.spriteSheetPath.clear();
			}
        },
        m_data);

    m_spriteSheet.reset();
	m_texture.reset();
}

// ★ Draw(SpriteBatch&)를 걷었다 (T6, 2026-08-08).
//
//   호출자가 0곳이었다. 그리고 DirectX::SpriteBatch 객체를 만드는 코드도
//   저장소 전체에 0곳이다 — 즉 이 경로는 이미 아무것도 그리지 않았다.
//   DX12 EnhancedUIPass가 GetData()로 자료만 받아 자기 인스턴싱으로 그린다.
//
//   그 함수가 Texture의 DX11 SRV를 만지던 마지막 UI 자리였다.

void UIRenderProxy::DestroyProxy()
{
    RenderScene::RegisteredDestroyUIProxyGUIDs.push(m_instancedID);
}

void UIRenderProxy::SetCustomPixelShader(std::string_view shaderPath)
{
    auto& shader = ShaderSystem->PixelShaders[shaderPath.data()];
    if (!shader.IsCompiled() && !shader.HasBlob())
    {
        std::cout << "Failed to load custom pixel shader: " << shaderPath << std::endl;
        return;
    }
	m_customPixelShader = &shader;

    Microsoft::WRL::ComPtr<ID3D11ShaderReflection> reflector;
    if (FAILED(D3DReflect(shader.GetBufferPointer(),
        shader.GetBufferSize(),
        IID_ID3D11ShaderReflection,
        reinterpret_cast<void**>(reflector.GetAddressOf()))))
    {
        std::cout << "Failed to reflect pixel shader: " << shaderPath << std::endl;
        return;
    }

    auto* constantBuffer = reflector->GetConstantBufferByIndex(0);
    D3D11_SHADER_BUFFER_DESC cbDesc{};
    if (constantBuffer && SUCCEEDED(constantBuffer->GetDesc(&cbDesc)))
    {
        m_customPixelBufferSize = cbDesc.Size;
    }
    else
    {
        m_customPixelBufferSize = 16; // 기본 최소 크기
    }

    m_customPixelBufferSize = ((m_customPixelBufferSize + 15) / 16) * 16;

    // ★ 여기 있던 DX11 상수 버퍼 생성을 걷었다 (T6). 그 버퍼를 GPU에 거는
    //   유일한 코드가 UpdateShaderBuffer였고 그 함수의 호출자가 0이었다.
    //   지금 필요한 것은 크기뿐이다 - 저작 값(m_customPixelCPUBuffer)이
    //   그 크기와 맞는지 검사하는 데 쓴다. 올리는 것은 DX12 UI 패스가
    //   커스텀 셰이더를 받을 때 그쪽에서 한다.
}

void UIRenderProxy::SetCustomPixelBuffer(const std::vector<std::byte>& cpuBuffer)
{
    if (0 == m_customPixelBufferSize)
    {
        // 셰이더가 아직 안 붙었거나 리플렉션이 실패한 상태다.
        std::cout << "Custom pixel buffer size is unknown." << std::endl;
        return;
    }
	m_customPixelCPUBuffer = cpuBuffer;
}
