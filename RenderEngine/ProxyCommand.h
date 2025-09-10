#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "MeshRendererProxy.h"

class FoliageComponent;
class TerrainComponent;
class ImageComponent;
class TextComponent;
class MeshRenderer;
class DecalComponent;
class SpriteSheetComponent;
using Invokable = std::function<void()>;
class ProxyCommand
{
public:
    ProxyCommand() = default;
    ~ProxyCommand() = default;

    ProxyCommand(MeshRenderer* pComponent);
	ProxyCommand(TerrainComponent* pComponent);
    ProxyCommand(FoliageComponent* pComponent);
	ProxyCommand(ImageComponent* pComponent);
	ProxyCommand(TextComponent* pComponent);
    ProxyCommand(DecalComponent* pComponent);

	ProxyCommand(const ProxyCommand& other);
	ProxyCommand(ProxyCommand&& other) noexcept;

	ProxyCommand& operator=(const ProxyCommand& other);
	ProxyCommand& operator=(ProxyCommand&& other) noexcept;

public:
	void ProxyCommandExecute();

private:
	HashedGuid	m_proxyGUID{};
	Invokable	m_updateFunction{};
};
#endif // !DYNAMICCPP_EXPORTS