#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "LetterboxController.generated.h"

class PlayerDialogueUI;
class DialogueConductor;
class LetterboxController : public ModuleBehavior
{
public:
   ReflectLetterboxController
	[[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(LetterboxController)
	virtual void Awake() override {}
	virtual void Start() override;
	virtual void FixedUpdate(float fixedTick) override {}
	virtual void OnTriggerEnter(const Collision& collision) override {}
	virtual void OnTriggerStay(const Collision& collision) override {}
	virtual void OnTriggerExit(const Collision& collision) override {}
	virtual void OnCollisionEnter(const Collision& collision) override {}
	virtual void OnCollisionStay(const Collision& collision) override {}
	virtual void OnCollisionExit(const Collision& collision) override {}
	virtual void Update(float tick) override;
	virtual void LateUpdate(float tick) override {}
	virtual void OnDisable() override  {}
	virtual void OnDestroy() override  {}

	// === Inspector ===
	[[Property]] 
	float barHeight = 177.0f;     // 1920x1080 기준 177
	[[Property]] 
	float animDuration = 0.6f;    // 슬라이드 인/아웃 시간(초)
	[[Property]] 
	bool  startInCinema = false;  // 시작 시 영화모드로 배치할지
	[[Property]] 
	std::string topBarName = "LetterboxTop";
	[[Property]] 
	std::string bottomBarName = "LetterboxBottom";

	// === 외부 제어 API ===
	[[Method]]
	void EnterCinemaMode();
	[[Method]]
	void ExitCinemaMode();
	[[Method]]
	void TestCinemaMode();
	[[Method]]
	void Stap1();
	[[Method]]
	void Stap2();

	bool IsCinema() const { return m_isCinema; }
	void SetBubbleVisible(bool visible);

	PlayerDialogueUI* m_p1Bubble = nullptr;
	PlayerDialogueUI* m_p2Bubble = nullptr;

private:
	struct Size { float width; float height; };

	struct Bar {
		class GameObject* obj = nullptr;
		class RectTransformComponent* rect = nullptr;
		class ImageComponent* img = nullptr;
	};

	// 내부 도움 함수
	Size GetScreenSize() const;
	Bar  FindBar(const std::string& name);
	void SetupBar(Bar& b, bool isTop, const Size& screen);
	void PlaceBarsImmediate(bool inView, const Size& screen);
	void StartAnim(bool toInView);

private:
	Bar   m_top{};
	Bar   m_bottom{};

	bool  m_isCinema = false;
	bool  m_isAnimating = false;
	bool  m_animFromInView = false;
	bool  m_animToInView = false;
	float m_elapsed = 0.0f;
	DialogueConductor* m_dialogueConductor = nullptr;

	class GameObject* m_p1BubbleObj = nullptr;
	class GameObject* m_p2BubbleObj = nullptr;
};
