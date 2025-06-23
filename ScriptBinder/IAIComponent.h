#pragma once

class IAIComponent
{
public:
	virtual ~IAIComponent() = default;

	virtual void Initialize() = 0;
	virtual void Tick(float deltaTime) = 0;

	//����ȭ / ������ȭ �������̽� In / Out struct ���� ���� ����
	//virtual void Serialize(struct) = 0;
	//virtual void Deserialize(struct) = 0;
};

