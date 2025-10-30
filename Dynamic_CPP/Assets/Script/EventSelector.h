#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "EventSelector.generated.h"

class TextComponent;
class ImageComponent;
class RectTransformComponent;
class EffectComponent;
class EventSelector : public ModuleBehavior
{
public:
   ReflectEventSelector
    [[ScriptReflectionField]]
	MODULE_BEHAVIOR_BODY(EventSelector)
	virtual void Start() override;
	virtual void Update(float tick) override;

private:
    enum class UiState { Hidden, Entering, Displaying, Completing };

    void HandleActiveEvent(const std::optional<int>& activeId, const std::string& activeText);
    void BeginEnter(int eventId, std::string message);
    void BeginCompletion();
    void FinishCompletion();
    void UpdateState(float tick);
    void ApplyAnimation(float alpha);
    void SetVisualAlpha(float alpha);

    TextComponent*          m_textComponent{ nullptr };
    ImageComponent*         m_imageComponent{ nullptr };
	ImageComponent*         m_iconComponent{ nullptr };
    ImageComponent*         m_chkboxIconComponent{ nullptr };
    ImageComponent*         m_chkIconComponent{ nullptr };
	RectTransformComponent* m_textRectTransform{ nullptr };
    RectTransformComponent* m_rectTransform{ nullptr };
	RectTransformComponent* m_chkboxRectTransform{ nullptr };
	RectTransformComponent* m_chkIconRectTransform{ nullptr };

    //EffectComponent*      m_effectComponent{ nullptr };

    [[Property]]
    std::string m_defaultMessage{ "null" };
    UiState m_state{ UiState::Hidden };
    [[Property]]
    float m_slideInDuration{ 0.35f };
    [[Property]]
    float m_slideOutDuration{ 0.25f };
    [[Property]]
    float m_hiddenOffset{ 910.f };
    float m_stateTimer{ 0.f };
	float m_effectTimer{ 0.f };

    bool m_effectPlayed{ false };
    bool m_initializedPositions{ false };
    bool m_hasBaseImageColor{ false };
    bool m_hasBaseTextColor{ false };

    std::optional<int> m_currentEventId{};
    std::optional<int> m_pendingEventId{};
    std::string m_currentMessage{};
    std::string m_pendingMessage{};

    Mathf::Vector2 m_visiblePosition{};
    Mathf::Vector2 m_hiddenPosition{};
    Mathf::Vector2 m_animStartPos{};
    Mathf::Vector2 m_animTargetPos{};
	Mathf::Vector2 m_bgSize{ 400.f, 47.f };

    Mathf::Color4 m_baseImageColor{ 1.f, 1.f, 1.f, 1.f };
    Mathf::Color4 m_baseTextColor{ 1.f, 1.f, 1.f, 1.f };
};
