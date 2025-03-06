//���� �̺�Ʈ �ý����� ������ ��� �����Դϴ�.
#pragma once
#include "Utility_Framework/Core.Definition.h"

class EventSystem : public Singleton<EventSystem>
{
private:
	friend class Singleton;

public:
	using EventCallback = std::function<void(void*)>;

	void Subscribe(const std::string& eventName, EventCallback callback) {
		eventListners[eventName].push_back(callback);
	}

	void Publish(const std::string& eventName, void* data) {
		if (eventListners.find(eventName)!= eventListners.end())
		{
			for (const auto& callback : eventListners[eventName]) 
			{
				callback(data);
			}
		}
	}

private:
	friend class World;
	std::unordered_map<std::string, std::vector<EventCallback>> eventListners;
};

inline static auto& Event = EventSystem::GetInstance();

//��� ����
//�ش� �߰� #include "EventSystem.h"
//�̺�Ʈ ���
//Event->Subscribe("EventName", [](void* data) {
//	//�̺�Ʈ ó�� �ڵ�
//});
//�̺�Ʈ �߻�
//Event->Publish("EventName", nullptr);
//�̺�Ʈ ó�� �ڵ尡 ����˴ϴ�.