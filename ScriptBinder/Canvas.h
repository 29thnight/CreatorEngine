#pragma once
#include "GameObject.h"
class Canvas 
{
public:
	Canvas();
	~Canvas();

	void AddUIObject(GameObject* obj);
	
	bool m_IsEnabled = true;
	int CanvasOrder = 0;
	std::vector<GameObject*> UIObjs;
};

