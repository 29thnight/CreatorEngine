#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "PlayerDialogueUI.generated.h"

class PlayerDialogueUI : public ModuleBehavior
{
public:
   ReflectPlayerDialogueUI
    [[ScriptReflectionField]]
    MODULE_BEHAVIOR_BODY(PlayerDialogueUI)
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
    virtual void OnDisable() override {}
    virtual void OnDestroy() override {}

    // 대상 및 비교대상 설정 (중앙 시스템에서 호출)
    void SetTarget(std::shared_ptr<GameObject> target) { m_target = target; }
    void SetCompareTarget(std::shared_ptr<GameObject> compare) { m_compareTarget = compare; }
    void SetPlayerIndex(int index) { m_playerIndex = index; }

    // 표시/숨김/텍스처 교체
    void ShowTexture(int textureIndex);   // textureIndex == TextureID
    void Hide();

    // 배치 파라미터
    [[Property]] 
    Mathf::Vector2 screenOffset = { 0.f, -30.f };
    [[Property]] 
    float          sideOffsetPixels{ 40.f };

private:
    void UpdateScreenPositionAndPivot(float tick);

private:
    std::weak_ptr<GameObject>     m_target;
    std::weak_ptr<GameObject>     m_compareTarget;
    class RectTransformComponent* m_rect = nullptr;
    class ImageComponent*         m_image = nullptr;
    class Camera*                 m_camera = nullptr;
    int                           m_playerIndex = 0;
};
