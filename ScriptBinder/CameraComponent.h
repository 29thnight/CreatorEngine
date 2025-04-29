#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "IRenderable.h"

class Camera;
class CameraComponent : public Component, public IRenderable
{
public:
	GENERATED_BODY(CameraComponent)

	bool IsEnabled() const override
	{
		return m_IsEnabled;
	}

	void SetEnabled(bool able) override
	{
		m_IsEnabled = able;
	}

private:
	Camera* m_pCamera{ nullptr };
	bool m_IsEnabled{ false };
};