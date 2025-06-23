#pragma once

class IAIComponent
{
public:
	virtual ~IAIComponent() = default;

	virtual void Initialize() = 0;
	virtual void Tick(float deltaTime) = 0;

	//직렬화 / 역직렬화 인터페이스 In / Out struct 부터 차후 정의
	//virtual void Serialize(struct) = 0;
	//virtual void Deserialize(struct) = 0;
};

