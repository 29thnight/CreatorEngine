#include "PauseMenuButton.h"
#include "Canvas.h"
#include "UIManager.h"
#include "ImageComponent.h"
#include "InputManager.h"
#include "RectTransformComponent.h"
#include "pch.h"

void PauseMenuButton::Start()
{
	m_boxImageComponent = GetOwner()->GetComponent<ImageComponent>();
	int parentIndex = GetOwner()->m_parentIndex;
	if (parentIndex != -1)
	{
		m_pauseMenuCanvasObject = GameObject::FindIndex(parentIndex);
	}

	m_settingPanelObject = GameObject::Find("SettingPanel");
	m_howToPlayPanelObject = GameObject::Find("HowToPlayPanel");

	if (m_settingPanelObject)
		m_settingsRect = m_settingPanelObject->GetComponent<RectTransformComponent>();

	if (m_howToPlayPanelObject)
		m_howToRect = m_howToPlayPanelObject->GetComponent<RectTransformComponent>();

	m_selfRect = GetOwner()->GetComponent<RectTransformComponent>();

	ComputeOffscreenPositions();
}

void PauseMenuButton::OnEnable()
{
	m_isMenuOpened = true;
}

void PauseMenuButton::Update(float tick)
{
	// ĵ���� ���ų� ��׶��� ������ ���� �� ��
	if (!m_selfRect || !m_settingsRect || !m_howToRect) return;

	if (m_isMenuOpened)
	{
		if (m_pauseMenuCanvasObject)
		{
			UIManagers->SelectUI = GetOwner()->weak_from_this();
		}
		m_isMenuOpened = false;
	}

	// �ݱ�(B / X)
	bool wantClose = false;
	if (InputManagement->IsKeyDown(KeyBoard::X) ||
		InputManagement->IsControllerButtonDown(0, ControllerButton::B) ||
		InputManagement->IsControllerButtonDown(1, ControllerButton::B))
	{
		wantClose = true;
	}
	if (wantClose)
	{
		if (m_pauseMenuCanvasObject) m_pauseMenuCanvasObject->SetEnabled(false);
		return;
	}

	// �׺���̼� ��Ŀ�� üũ(���õ� UI�� �� ��׶�������)
	//if (m_boxImageComponent && !m_boxImageComponent->IsNavigationThis())
	//	return;

	// ��/�� �Է�(Ʈ���� + Ű���� ���)
	const bool lt = InputManagement->IsControllerTriggerL(0) ||
		InputManagement->IsControllerTriggerL(1) ||
		InputManagement->IsKeyDown(KeyBoard::LeftArrow);

	const bool rt = InputManagement->IsControllerTriggerR(0) ||
		InputManagement->IsControllerTriggerR(1) ||
		InputManagement->IsKeyDown(KeyBoard::RightArrow);

	// ��ٿ: ���� �����ӿ��� ��ȯ
	if (!m_isTransitioning)
	{
		if (lt && !m_leftHeld) { Prev(); }
		if (rt && !m_rightHeld) { Next(); }
	}
	m_leftHeld = lt;
	m_rightHeld = rt;

	// ��ǥ ��ġ�� �����̵�
	ApplyTargets(tick);
}

void PauseMenuButton::OnDisable()
{
	// ������ �⺻������ ����
	m_active = PauseWindowType::Self;
	m_isTransitioning = false;
	// �ʿ��ϸ� �׺� ��� ����
	if (m_boxImageComponent && m_boxImageComponent->IsNavLock())
		m_boxImageComponent->SetNavLock(false);
}

void PauseMenuButton::ComputeOffscreenPositions()
{
	// ȭ�� ����(�ʿ��ϸ� ���� �ػ󵵷� �ٲ㵵 ��)
	float screenW = 1920.f;
	float screenH = 1080.f;

	auto halfW = [&](RectTransformComponent* r) -> float
		{
			return r ? r->GetSizeDelta().x * 0.5f : 0.f;
		};

	const float maxHalfW = std::max({ halfW(m_selfRect), halfW(m_settingsRect), halfW(m_howToRect) });

	const float offRightX = screenW + (screenW * 0.5f) + maxHalfW + m_margin;
	const float offLeftX = -(screenW * 0.5f) - maxHalfW - m_margin;

	m_centerPos = { screenW * 0.5f, screenH * 0.5f };
	m_offLeftPos = { offLeftX,       screenH * 0.5f };
	m_offRightPos = { offRightX,      screenH * 0.5f };
}

static inline int WrapIndex(int v, int n)
{
	v %= n;
	if (v < 0) v += n;
	return v;
}

void PauseMenuButton::ApplyTargets(float tick)
{
	// Ȱ�� �ε����� 0:Self,1:Settings,2:HowTo
	const int active = static_cast<int>(m_active);
	const int prev = WrapIndex(active - 1, 3);
	const int next = WrapIndex(active + 1, 3);

	// �� �г��� Ÿ�� ����
	Mathf::Vector2 targetSelf = m_offRightPos;
	Mathf::Vector2 targetSettings = m_offRightPos;
	Mathf::Vector2 targetHowTo = m_offRightPos;

	auto place = [&](int idx)->Mathf::Vector2
		{
			if (idx == active) return m_centerPos;
			if (idx < active)   return m_offLeftPos;
			/* idx == next */  return m_offRightPos;
		};

	targetSelf = place(0);
	targetSettings = place(1);
	targetHowTo = place(2);

	// ���� ����
	const float t = 1.f - std::exp(-m_slideSpeed * tick);

	const Mathf::Vector2 curSelf = m_selfRect->GetAnchoredPosition();
	const Mathf::Vector2 curSettings = m_settingsRect->GetAnchoredPosition();
	const Mathf::Vector2 curHowTo = m_howToRect->GetAnchoredPosition();

	const Mathf::Vector2 nextSelf = Mathf::Lerp(curSelf, targetSelf, t);
	const Mathf::Vector2 nextSettings = Mathf::Lerp(curSettings, targetSettings, t);
	const Mathf::Vector2 nextHowTo = Mathf::Lerp(curHowTo, targetHowTo, t);

	m_selfRect->SetAnchoredPosition(nextSelf);
	m_settingsRect->SetAnchoredPosition(nextSettings);
	m_howToRect->SetAnchoredPosition(nextHowTo);

	// ���� üũ
	const float eps = 0.5f;
	auto close2 = [&](const Mathf::Vector2& a, const Mathf::Vector2& b)
		{
			return std::abs(a.x - b.x) < eps && std::abs(a.y - b.y) < eps;
		};

	const bool done =
		close2(nextSelf, targetSelf) &&
		close2(nextSettings, targetSettings) &&
		close2(nextHowTo, targetHowTo);

	if (done)
	{
		m_selfRect->SetAnchoredPosition(targetSelf);
		m_settingsRect->SetAnchoredPosition(targetSettings);
		m_howToRect->SetAnchoredPosition(targetHowTo);
		m_isTransitioning = false;
	}
}

void PauseMenuButton::SetActive(PauseWindowType next)
{
	if (m_active == next) return;
	m_active = next;
	m_isTransitioning = true;

	// Ȱ�� �г��� ������ �ƴ� ���� �׺� �� (�ʿ� ��å�� �°� ����)
	if (m_boxImageComponent)
	{
		const bool wantLock = (next != PauseWindowType::Self);
		if (m_boxImageComponent->IsNavLock() != wantLock)
			m_boxImageComponent->SetNavLock(wantLock);
	}

	switch (next)
	{
	case PauseWindowType::Self:
	{
		if (m_settingPanelObject)
		{
			UIManagers->SelectUI = GetOwner()->weak_from_this();
		}
		break;
	}
	case PauseWindowType::HowTo:
		break;
	case PauseWindowType::Settings:
	{
		if (m_settingPanelObject)
		{
			UIManagers->SelectUI = m_settingPanelObject->weak_from_this();
		}
		break;
	}
	};

}

void PauseMenuButton::Next()
{
	int cur = static_cast<int>(m_active);
	if (cur >= 2) return; // 3(HowTo)���� �� �̻� ���� ����
	SetActive(static_cast<PauseWindowType>(cur + 1));
}

void PauseMenuButton::Prev()
{
	int cur = static_cast<int>(m_active);
	if (cur <= 0) return; // 1(Self)���� �� �̻� �ڷ� ����
	SetActive(static_cast<PauseWindowType>(cur - 1));
}

