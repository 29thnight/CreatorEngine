#pragma once

// Only RenderProxy's unrelated runtime identity/logging dependency is replaced.
// LightRenderProxy, its enabled/type rules and all light values remain production
// code. The light-packing path reads only this inherited position member.
#include <mathematics/vector3.hpp>
class RenderProxy
{
  public:
    virtual ~RenderProxy() = default;
    math::vector3 m_worldPosition{};
};
