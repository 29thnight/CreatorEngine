#ifndef DYNAMICCPP_EXPORTS
#include "EditorImGuiTexture.h"
#include "RHI/DX12/ImGuiDx12Shell.h"
#include "Texture.h"

namespace EditorImGuiTexture
{
    uint64_t From(Texture* texture)
    {
        if (nullptr == texture) return 0;
        if (ImGuiDx12Shell::Get().IsActive())
        {
            return ImGuiDx12Shell::Get().RegisterTexture(texture);
        }
        return reinterpret_cast<uint64_t>(texture->m_pSRV);
    }

    uint64_t FromRawDx11Srv(ID3D11ShaderResourceView* srv)
    {
        if (ImGuiDx12Shell::Get().IsActive()) return 0;
        return reinterpret_cast<uint64_t>(srv);
    }
}

#endif
