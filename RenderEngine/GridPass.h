#pragma once
#include "IRenderPass.h"
#include "Texture.h"

class GridPass final : public IRenderPass
{
public:
	GridPass();
	~GridPass();
	void Execute(Scene& scene) override;
	void Initialize(Texture* color);
};