#pragma once
#include "Core.Minimal.h"
#include "Camera.h"
#include "Light.h"

class Scene
{
public:
	Scene();
	~Scene();
	PerspacetiveCamera m_MainCamera;
	LightController m_LightController;
};