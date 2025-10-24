#include "KoriEmoteSystem.h"
#include "Core.Mathf.h"
#include "RectTransformComponent.h"
#include "ImageComponent.h"
#include "GameManager.h"
#include "Entity.h"
#include "EntityAsis.h"
#include "Camera.h"
#include "pch.h"

inline int ToIndex(EKoriEmoteState s) {
    return static_cast<int>(s);
}

void KoriEmoteSystem::Start()
{
	m_bgComponent = GetComponent<ImageComponent>();
    m_emoteRectTransform = GetComponent<RectTransformComponent>();

	auto gameManagerObj = GameObject::Find("GameManager");

    if (m_emoteRectTransform && m_bgComponent) {
        m_baseAnchoredPos = GetAnchoredPos();
        SetAnchoredPos(m_baseAnchoredPos + m_emoteOffset);
        SetScale(0.0f);   // 0에서 시작
        PlayHappy();
    }
}

void KoriEmoteSystem::Update(float tick)
{
    if (m_tweenManager && m_targetObject && m_camera) return;

    auto gameManagerObj = GameObject::Find("GameManager");
    if (gameManagerObj)
    {
        m_tweenManager = gameManagerObj->GetComponent<TweenManager>();
        auto manager = gameManagerObj->GetComponent<GameManager>();
        if (manager)
        {
            auto& asisVec = manager->GetAsis();
            auto* asis = asisVec.empty() ? nullptr : asisVec.front();
            if (asis)
            {
                m_targetObject = asis->GetOwner();
            }
        }

        auto cameraPtr = CameraManagement->GetLastCamera();
        if (cameraPtr)
        {
            m_camera = cameraPtr.get();
        }
    }
}

void KoriEmoteSystem::SetEmoteState(EKoriEmoteState newState, bool forcePlay)
{
    if (!m_emoteRectTransform) return;

    // 같은 상태여도 forcePlay면 재생 허용
    //if (!forcePlay && newState == m_currentState) return;

    if (m_targetObject && m_camera)
    {
		Mathf::Vector3 worldPos = m_targetObject->m_transform.GetWorldPosition();
		auto view = m_camera->CalculateView();
		auto proj = m_camera->CalculateProjection();
		auto viewProj = XMMatrixMultiply(view, proj);

		Mathf::xVector pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
		Mathf::xVector clip = XMVector4Transform(pos, viewProj);
		float w = XMVectorGetW(clip);
        if (w <= 0.0f)
            return;

		float x_ndc = XMVectorGetX(clip) / w;
		float y_ndc = XMVectorGetY(clip) / w;

		auto screenSize = m_camera->GetScreenSize();
		float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
		float screenY = (1.0f - (y_ndc + 1.0f) * 0.5f) * screenSize.height;

        m_baseAnchoredPos = Mathf::Vector2{ screenX, screenY } + m_emoteOffset;
    }

    m_previousState = m_currentState;
    m_currentState = newState;

    // 내가 만든 트윈만 정리 (TweenManager는 그대로)
    KillMyTweens();

    // 텍스처 전환 & 등장 페이드(매 호출 시 다시 나타나도록)
    if (m_bgComponent)
    {
        SetAnchoredPos(m_baseAnchoredPos);
        m_bgComponent->SetTexture(static_cast<int>(newState));
        // 빠른 페이드 인 1회(원하면 제거 가능)
        m_bgComponent->color.w = 0.0f;
        AddMyTween(std::make_shared<Tweener<float>>(
            [this] { return m_bgComponent->color.w; },
            [this](float a) { m_bgComponent->color.w = a; },
            1.0f, 0.12f, [](float t) { return Easing::EaseOutSine(t); }
        ));
    }

    // 스케일/회전 기본값 세팅(매번 동일한 출발 상태 보장)
    SetScale(0.0f);
    SetRotationZDeg(0.0f);

    // 상태별 “나타났다→연출→사라짐” 1회 재생
    switch (m_currentState)
    {
    case EKoriEmoteState::Smile:   PlaySmile_Once();   break;
    case EKoriEmoteState::Sick:    PlaySick_Once();    break;
    case EKoriEmoteState::Stunned: PlayStunned_Once(); break;
    case EKoriEmoteState::Happy:   PlayHappy_Once();   break;
    }
}

Mathf::Vector2 KoriEmoteSystem::GetAnchoredPos() const
{
	return m_emoteRectTransform->GetAnchoredPosition();
}

void KoriEmoteSystem::SetAnchoredPos(const Mathf::Vector2& p)
{
	m_emoteRectTransform->SetAnchoredPosition(p);
}

float KoriEmoteSystem::GetScale() const
{
	return m_bgComponent->unionScale;
}

void KoriEmoteSystem::SetScale(const float& s)
{
	m_bgComponent->unionScale = s;
}

float KoriEmoteSystem::GetRotationZDeg() const
{
	return m_bgComponent->rotate;
}

void KoriEmoteSystem::SetRotationZDeg(float deg)
{
	m_bgComponent->rotate = deg;
}

float KoriEmoteSystem::GetEmoteAlpha() const
{
	return 0.0f;
}

void KoriEmoteSystem::SetEmoteAlpha(float a)
{
	m_bgComponent->color.w = a;
}

void KoriEmoteSystem::PlaySmile()
{
    SetEmoteState(EKoriEmoteState::Smile, false);
}

void KoriEmoteSystem::PlaySick()
{
    SetEmoteState(EKoriEmoteState::Sick, false);
}

void KoriEmoteSystem::PlayStunned()
{
    SetEmoteState(EKoriEmoteState::Stunned, false);
}

void KoriEmoteSystem::PlayHappy()
{
    SetEmoteState(EKoriEmoteState::Happy, false);
}

void KoriEmoteSystem::PingPongPosY_Once(float centerY, float amplitude,
    float durUp, float durDown,
    Mathf::Easing::EaseType eUp, Mathf::Easing::EaseType eDown)
{
    auto up = AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
        [this] { return GetAnchoredPos(); },
        [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
        [this, centerY, amplitude] {
            auto p = GetAnchoredPos(); p.y = centerY + amplitude; return p;
        }(),
            durUp,
            [eUp](float t) { return DynamicEasing(eUp)(t); }
            ));

    up->SetOnComplete([this, centerY, amplitude, durDown, eDown]()
    {
        AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
            [this] { return GetAnchoredPos(); },
            [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
            [this, centerY, amplitude] {
                auto p = GetAnchoredPos(); p.y = centerY - amplitude; return p;
            }(),
                durDown,
                [eDown](float t) { return DynamicEasing(eDown)(t); }
                ));
        // 체인 끝(루프 없음)
    });
}

void KoriEmoteSystem::ShakeX_Cycles(float magnitude, float singleDur, int cycles)
{
    if (cycles <= 0) return;

    auto center = GetAnchoredPos();
    auto left = Mathf::Vector2{ center.x - magnitude, center.y };
    auto right = Mathf::Vector2{ center.x + magnitude, center.y };

    auto t1 = AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
        [this] { return GetAnchoredPos(); },
        [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
        left, singleDur,
        [](float t) { return Easing::EaseOutQuad(t); }
    ));

    t1->SetOnComplete([this, right, singleDur, center, magnitude, cycles]()
        {
            auto t2 = AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
                [this] { return GetAnchoredPos(); },
                [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
                right, singleDur,
                [](float t) { return Easing::EaseInOutQuad(t); }
            ));

            t2->SetOnComplete([this, center, singleDur, magnitude, cycles]()
                {
                    auto t3 = AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
                        [this] { return GetAnchoredPos(); },
                        [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
                        center, singleDur,
                        [](float t) { return Easing::EaseOutQuad(t); }
                    ));

                    t3->SetOnComplete([this, magnitude, singleDur, cycles]()
                        {
                            // 남은 횟수만큼 재귀(최종적으로 종료)
                            ShakeX_Cycles(magnitude, singleDur, cycles - 1);
                        });
                });
        });
}

void KoriEmoteSystem::PumpScale_Once(float minS, float maxS, float restS, float duUp, 
    float duDown, float duRest, 
    Mathf::Easing::EaseType eUp, Mathf::Easing::EaseType eDown, Mathf::Easing::EaseType eRest)
{
    auto up = AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetScale(); },
        [this](float s) { SetScale(s); },
        maxS, duUp, [eUp](float t) { return DynamicEasing(eUp)(t); }
    ));

    up->SetOnComplete([this, minS, restS, duDown, duRest, eDown, eRest]()
        {
            auto down = AddMyTween(std::make_shared<Tweener<float>>(
                [this] { return GetScale(); },
                [this](float s) { SetScale(s); },
                minS, duDown, [eDown](float t) { return DynamicEasing(eDown)(t); }
            ));
            down->SetOnComplete([this, restS, duRest, eRest]()
                {
                    if (duRest > 0.0f)
                    {
                        AddMyTween(std::make_shared<Tweener<float>>(
                            [this] { return GetScale(); },
                            [this](float s) { SetScale(s); },
                            restS, duRest, [eRest](float t) { return DynamicEasing(eRest)(t); }
                        ));
                    }
                    else
                    {
                        SetScale(restS);
                    }
                    // 종료
                });
        });
}

void KoriEmoteSystem::After(float delaySec, std::function<void()> cb)
{
    // getter는 0, setter는 no-op → duration만큼 기다렸다가 OnComplete 실행
    AddMyTween(std::make_shared<Tweener<float>>(
        []() { return 0.0f; },
        [](float) { /* no-op */ },
        1.0f, delaySec,
        [](float t) { return Easing::Linear(t); }
    ))->SetOnComplete(std::move(cb));
}

void KoriEmoteSystem::Disappear(float fadeDur, float endScale, float dropPixels)
{
    // 알파 → 0
    if (m_bgComponent)
    {
        AddMyTween(std::make_shared<Tweener<float>>(
            [this] { return m_bgComponent->color.w; },
            [this](float a) { m_bgComponent->color.w = a; },
            0.0f, fadeDur,
            [](float t) { return Easing::EaseInSine(t); }
        ));
    }

    // 스케일 → endScale
    AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetScale(); },
        [this](float s) { SetScale(s); },
        endScale, fadeDur,
        [](float t) { return Easing::EaseInBack(t); }
    ));

    // 살짝 아래로 떨어지며 사라지기(옵션)
    auto p = GetAnchoredPos();
    AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
        [this] { return GetAnchoredPos(); },
        [this](Mathf::Vector2 v) { SetAnchoredPos(v); },
        Mathf::Vector2{ p.x, p.y - dropPixels }, fadeDur,
        [](float t) { return Easing::EaseInSine(t); }
    ));
}

void KoriEmoteSystem::PlaySmile_Once()
{
    // 등장: 0 → 1.15 → 1.0
    AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetScale(); },
        [this](float s) { SetScale(s); },
        1.15f, 0.16f,
        [](float t) { return Easing::EaseOutBack(t); }
    ))->SetOnComplete([this]()
        {
            AddMyTween(std::make_shared<Tweener<float>>(
                [this] { return GetScale(); },
                [this](float s) { SetScale(s); },
                1.0f, 0.10f,
                [](float t) { return Easing::EaseInBack(t); }
            ));
        });

    // 살짝 Up → Down 1회
    auto p = GetAnchoredPos();
    PingPongPosY_Once(p.y, 6.0f, 0.45f, 0.55f,
        Easing::EaseType::EaseOutSine,
        Easing::EaseType::EaseInSine);

    // 회전 정리
    SetRotationZDeg(0.0f);

    After(1.0f /*life*/, [this] {
        Disappear(0.22f, /*endScale*/0.0f, /*drop*/6.0f);
        });
}

void KoriEmoteSystem::PlaySick_Once()
{
    // 등장: 0 → 1.15 → 1.0
    AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetScale(); },
        [this](float s) { SetScale(s); },
        1.15f, 0.16f,
        [](float t) { return Easing::EaseOutBack(t); }
    ))->SetOnComplete([this]()
        {
            AddMyTween(std::make_shared<Tweener<float>>(
                [this] { return GetScale(); },
                [this](float s) { SetScale(s); },
                1.0f, 0.10f,
                [](float t) { return Easing::EaseInBack(t); }
            ));
        });

    // 살짝 Up → Down 1회
    auto p = GetAnchoredPos();
    PingPongPosY_Once(p.y, 6.0f, 0.45f, 0.55f,
        Easing::EaseType::EaseOutSine,
        Easing::EaseType::EaseInSine);

    // 회전 정리
    SetRotationZDeg(0.0f);

    After(1.0f /*life*/, [this] {
        Disappear(0.22f, /*endScale*/0.0f, /*drop*/6.0f);
        });
}

void KoriEmoteSystem::PlayStunned_Once()
{
    // 위치 리셋(+오프셋)
    auto base = GetAnchoredPos();
    SetAnchoredPos({ base.x , base.y });

    // 빠른 X 셰이크 3회
    ShakeX_Cycles(8.0f, 0.04f, 3);

    // 회전: -10 → +10 → 0 (1회)
    AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetRotationZDeg(); },
        [this](float d) { SetRotationZDeg(d); },
        -10.0f, 0.07f, [](float t) { return Easing::EaseOutQuad(t); }
    ))->SetOnComplete([this]()
        {
            AddMyTween(std::make_shared<Tweener<float>>(
                [this] { return GetRotationZDeg(); },
                [this](float d) { SetRotationZDeg(d); },
                +10.0f, 0.12f, [](float t) { return Easing::EaseInOutQuad(t); }
            ))->SetOnComplete([this]()
                {
                    AddMyTween(std::make_shared<Tweener<float>>(
                        [this] { return GetRotationZDeg(); },
                        [this](float d) { SetRotationZDeg(d); },
                        0.0f, 0.07f, [](float t) { return Easing::EaseOutQuad(t); }
                    ));
                });
        });

    // 스케일 펀치: 0.9 → 1.0 (1회)
    SetScale(0.9f);
    AddMyTween(std::make_shared<Tweener<float>>(
        [this] { return GetScale(); },
        [this](float s) { SetScale(s); },
        1.0f, 0.10f,
        [](float t) { return Easing::EaseOutBack(t); }
    ));

    After(1.0f /*life*/, [this] {
        Disappear(0.22f, /*endScale*/0.0f, /*drop*/6.0f);
        });
}

void KoriEmoteSystem::PlayHappy_Once()
{
    // Up → Down 1회
    auto up = AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
        [this] { return GetAnchoredPos(); },
        [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
        [this] { auto p = GetAnchoredPos(); p.y += 14.0f; return p; }(),
        0.15f, [](float t) { return Easing::EaseOutCubic(t); }
    ));

    up->SetOnComplete([this]()
        {
            AddMyTween(std::make_shared<Tweener<Mathf::Vector2>>(
                [this] { return GetAnchoredPos(); },
                [this](Mathf::Vector2 p) { SetAnchoredPos(p); },
                [this] { auto p = GetAnchoredPos(); p.y -= 10.0f; return p; }(),
                0.25f, [](float t) { return Easing::EaseInCubic(t); }
            ));
        });

    // 펌핑 스케일 1회: 1.0 → 1.10 → 1.0
    SetScale(1.0f);
    PumpScale_Once(1.0f, 1.10f, 1.0f,
        0.18f, 0.24f, 0.0f,
        Easing::EaseType::EaseOutBack,
        Easing::EaseType::EaseInBack,
        Easing::EaseType::Linear);

    // 회전 정리
    SetRotationZDeg(0.0f);

    After(1.0f /*life*/, [this] {
        Disappear(0.22f, /*endScale*/0.0f, /*drop*/6.0f);
        });
}