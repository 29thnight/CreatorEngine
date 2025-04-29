#pragma once
class aniFsm;
class AniBehaviour
{
public:
	AniBehaviour() {};
	virtual void Enter() {};
	virtual void Update(float deltaTime) {};
	virtual void Exit() {};
	aniFSM* Owner{};
	std::string name{};
};