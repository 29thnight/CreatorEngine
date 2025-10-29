#include "SettingWindowUI.h"
#include "ImageComponent.h"
#include "InputManager.h"
#include "RectTransformComponent.h"
#include "UIManager.h"
#include "pch.h"
void SettingWindowUI::Start()
{
	m_backgroundImageComponent = GetComponent<ImageComponent>();
	m_settingCanvasObj = GameObject::Find("SettingCanvas");
    m_optionsObj = GetOwner();
	m_howToPlayObj = GameObject::Find("HowToPlayPanel");
	m_settingButtonObj = GameObject::Find("SettingsButton");
	m_mainCanvasObj = GameObject::Find("MainSceneCanvas");
    if (!m_mainCanvasObj)
    {
		m_mainCanvasObj = GameObject::Find("MenuSettingCanvas");
    }

    if (m_optionsObj)
        m_optionsRect = m_optionsObj->GetComponent<RectTransformComponent>();
    if (m_howToPlayObj)
		m_howToRect = m_howToPlayObj->GetComponent<RectTransformComponent>();

    ComputeOffscreenPositions();

    // �ʱ� ��ġ: �ɼ� = �߾�, ���۹� = ������ �ٱ�
    if (m_optionsRect)   m_optionsRect->SetAnchoredPosition(m_centerPos);
    if (m_howToRect)     m_howToRect->SetAnchoredPosition(m_offRightPos);

    m_active = SettingWindowType::Options;
    m_isTransitioning = false;
}

void SettingWindowUI::OnEnable()
{
}

void SettingWindowUI::Update(float tick)
{
    if (!m_backgroundImageComponent || !m_settingCanvasObj) return;

    // �ݱ�(X / B)
    bool PrepareToClose = false;
    const bool isKClosePressed = InputManagement->IsKeyDown(KeyBoard::X);
    const bool isP1ClosePressed = InputManagement->IsControllerButtonDown(0, ControllerButton::B);
    const bool isP2ClosePressed = InputManagement->IsControllerButtonDown(1, ControllerButton::B);
    if (isKClosePressed || isP1ClosePressed || isP2ClosePressed)
        PrepareToClose = true;

    if (PrepareToClose)
    {
        m_settingCanvasObj->SetEnabled(false);
        return;
    }

    // �׺���̼� ��Ŀ�� üũ
    if (!m_backgroundImageComponent->IsNavigationThis())
        return;

    // ��ȯ �Է�: LT/RT (LB/RB�� ��� Ű�� ���)
    const bool lt =
        InputManagement->IsControllerTriggerL(0) ||
        InputManagement->IsControllerTriggerL(1);

    const bool rt =
        InputManagement->IsControllerTriggerR(0) ||
        InputManagement->IsControllerTriggerR(1);

    // Ű���� ���(Q/E)
    const bool q = InputManagement->IsKeyDown(KeyBoard::LeftArrow);
    const bool e = InputManagement->IsKeyDown(KeyBoard::RightArrow);

    if (!m_isTransitioning)
    {
        if (lt || q)       SetActive(SettingWindowType::Options);
        else if (rt || e)  SetActive(SettingWindowType::HowToPlay);
    }

    // ��ǥ ��ġ�� �����̵�
    ApplyTargets(tick);
}

void SettingWindowUI::OnDisable()
{
	m_active = SettingWindowType::Options;
    if(m_settingButtonObj && m_mainCanvasObj && m_isEntering)
    {
		UIManagers->CurCanvas = m_mainCanvasObj->weak_from_this();
        UIManagers->SelectUI = m_settingButtonObj->weak_from_this();
        m_isEntering = false;
    }
}

void SettingWindowUI::ComputeOffscreenPositions()
{
    float screenW = 1920.f;
    float screenH = 1080.f;

    float halfW_options = 0.f;
    float halfW_howTo = 0.f;

    if (m_optionsRect)
        halfW_options = m_optionsRect->GetSizeDelta().x * 0.5f;
    if (m_howToRect)
        halfW_howTo = m_howToRect->GetSizeDelta().x * 0.5f;

    const float offRightX = screenW + (screenW * 0.5f) + std::max(halfW_options, halfW_howTo) + m_margin;
    const float offLeftX = -(screenW * 0.5f) - std::max(halfW_options, halfW_howTo) - m_margin;

    m_offLeftPos = { offLeftX,  screenH * 0.5f };
    m_offRightPos = { offRightX, screenH * 0.5f };
}

void SettingWindowUI::ApplyTargets(float tick)
{
    if (!m_optionsRect || !m_howToRect) return;

    // Ÿ�� ��ǥ ���
    Mathf::Vector2 targetOpt = m_centerPos;
    Mathf::Vector2 targetHow = m_centerPos;

    if (m_active == SettingWindowType::Options)
    {
        // �ɼ� �߾�, ���۹� ���� �ٱ�����
        targetOpt = m_centerPos;
        targetHow = m_offRightPos; // ���� �ɼ��� �߾��̶�� ���۹��� ������ �ٱ��̾�� �Ѵٰ� �߾�����
        // ��ȯ ������ LT/RT�� �ϹǷ�, 
        // ���⼭�� "Ȱ��=�ɼ�"�� �� �ݴ� �г�=������ �ٱ����� �о�д�.
    }
    else // HowToPlay
    {
        // ���۹� �߾�, �ɼ� ������ �ٱ�����
        targetOpt = m_offLeftPos;     // �ɼ��� �������� ����
        targetHow = m_centerPos;      // ���۹� �߾�
    }

    // ���� (t�� 0~1 ����, tick���� ����)
    const float t = 1.f - std::exp(-m_slideSpeed * tick); // ���� ����(�ӵ� �Һ� ����)
    const auto currOpt = m_optionsRect->GetAnchoredPosition();
    const auto currHow = m_howToRect->GetAnchoredPosition();

    const auto nextOpt = Mathf::Lerp(currOpt, targetOpt, t);
    const auto nextHow = Mathf::Lerp(currHow, targetHow, t);

    m_optionsRect->SetAnchoredPosition(nextOpt);
    m_howToRect->SetAnchoredPosition(nextHow);

    // ���� üũ(�ȼ� ����)
    const float eps = 0.5f;
    bool optDone = (std::abs(nextOpt.x - targetOpt.x) < eps) && (std::abs(nextOpt.y - targetOpt.y) < eps);
    bool howDone = (std::abs(nextHow.x - targetHow.x) < eps) && (std::abs(nextHow.y - targetHow.y) < eps);

    if (optDone && howDone)
    {
        m_optionsRect->SetAnchoredPosition(targetOpt);
        m_howToRect->SetAnchoredPosition(targetHow);
        m_isTransitioning = false;
    }

}

void SettingWindowUI::SetActive(SettingWindowType next)
{
    if (m_active == next) return;
    m_active = next;
    m_isTransitioning = true;

    if (!m_backgroundImageComponent) return;

    const bool wantLock = (next != SettingWindowType::Options);
    if (m_backgroundImageComponent->IsNavLock() != wantLock)
        m_backgroundImageComponent->SetNavLock(wantLock);
}
