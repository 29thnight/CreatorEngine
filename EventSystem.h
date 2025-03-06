//전역 이벤트 시스템을 구현한 헤더 파일입니다.
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

//사용 예시
//해더 추가 #include "EventSystem.h"
//이벤트 등록
//Event->Subscribe("EventName", [](void* data) {
//	//이벤트 처리 코드
//});
//이벤트 발생
//Event->Publish("EventName", nullptr);
//이벤트 처리 코드가 실행됩니다.