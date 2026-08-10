#include "UIRenderProxy.h"
#include "RenderScene.h"
#include "Texture.h"
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

