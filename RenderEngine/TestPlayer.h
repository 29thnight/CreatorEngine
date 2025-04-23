#pragma once
#include "GameObject.h"
#include "Transform.h"

class TestPlayer
{

public:
	void GetPlayer(GameObject* _player);
	void Update(float deltaTime);
	GameObject* player;
	float speed = 0.1f;

	float maxSpeed = 1.5f;
};

