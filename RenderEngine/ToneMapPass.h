#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class ToneMapPass final : public IRenderPass
{
public:
    ToneMapPass();
    ~ToneMapPass();
    void Initialize(Texture* color, Texture* dest);
    void Execute(Scene& scene) override;

private:
    Texture* m_ColorTexture{};
    Texture* m_DestTexture{};
};
