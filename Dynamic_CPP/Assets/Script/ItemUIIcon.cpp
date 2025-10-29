#include "ItemUIIcon.h"
#include "RectTransformComponent.h"
#include "Camera.h"
#include "GameObject.h"
#include "ImageComponent.h"
#include "ItemComponent.h"
#include "GameInstance.h"
#include "ItemManager.h"
#include "Player.h"
#include "pch.h"

void ItemUIIcon::Start()
{
	m_rect = m_pOwner->GetComponent<RectTransformComponent>();
	m_image = m_pOwner->GetComponent<ImageComponent>();
	m_rect->SetSizeDelta({ 75.f, 75.f });


	auto gmObj = GameObject::Find("GameManager");
    if (gmObj)
    {
		m_itemManager = gmObj->GetComponent<ItemManager>();
    }

    // �ʿ� �� �ʱ�ȭ
    m_bobTime = 0.f;
}

void ItemUIIcon::Update(float tick)
{
    constexpr int layerOrderFoward = 10;

    // --- 0) ī�޶�/��ũ�� ��ǥ ��� (���� �ڵ�) ---
    auto cameraPtr = CameraManagement->GetLastCamera();
    if (!cameraPtr || !m_rect || !m_target || !m_itemComp) return;
    Camera* camera = cameraPtr.get();

    const Mathf::Vector4 worldPos = m_target->m_transform.GetWorldPosition();
    const auto view = camera->CalculateView();
    const auto proj = camera->CalculateProjection();
    const auto viewProj = XMMatrixMultiply(view, proj);

    const DirectX::XMVECTOR pos = XMVectorSet(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    const DirectX::XMVECTOR clip = XMVector4Transform(pos, viewProj);
    const float w = XMVectorGetW(clip);
    if (w <= 0.0f) return;

    m_playerID = m_itemComp->m_playerID;

    if (0 > itemID) return;

    // ���� ���ŵǾ����� �˾� ���� ���� ����
    if (m_isPurchased) 
    {
        m_isSetPopup = false; // �˾� off�� ����
    }

    const float x_ndc = XMVectorGetX(clip) / w;
    const float y_ndc = XMVectorGetY(clip) / w;

    const auto screenSize = camera->GetScreenSize();
    const float screenX = (x_ndc + 1.0f) * 0.5f * screenSize.width;
    const float screenY = (1.0f - y_ndc) * 0.5f * screenSize.height;

    const float baseX = screenX + screenOffset.x; // ���� ��ǥ
    const float baseY = screenY + screenOffset.y;

    // --- 1) ���� ����(���� ������ ���� ���) ---
    const bool prev = m_prevIsSetPopup;
    const bool cur = m_isSetPopup;

    // false -> true : �˾� ���� Ʈ�� ����
    if (!prev && cur)
    {
        m_phase = PopupPhase::ToPopup;
        m_popupInit = false;
        m_popupElapsed = 0.f;
        m_isPopupComplete = false;   // �˾� �Ϸ� �÷��� �ʱ�ȭ
        m_bobbing = false;   // �˾� ���� ���� ǥ�� ����

        if (!m_popupRaised) { ApplyOrderDelta(+layerOrderFoward); m_popupRaised = true; }
    }
    // true -> false : �˾� ���� Ʈ�� ���� (�˾� �Ϸ�� ���¿�����)
    else if (prev && !cur && m_isPopupComplete)
    {
        m_phase = PopupPhase::ToScreen;
        m_popupInit = false;
        m_popupElapsed = 0.f;
        // ������ Ʈ�� ���� �� �簳
        if (!m_dismissRaised) { ApplyOrderDelta(+layerOrderFoward); m_dismissRaised = true; }
    }

    // --- 2) Ʈ�� ó�� ---
    if (m_phase == PopupPhase::ToPopup)
    {
        if (!m_popupInit)
        {
            m_popupInit = true;
            m_popupElapsed = 0.f;
            m_startPos = { baseX, baseY, 0.f }; // ������: ����(���� ������) ��ġ
        }

        m_popupElapsed += tick;
        const float t = std::clamp(m_popupElapsed / m_duration, 0.f, 1.f);

        const float tx = screenX + popupOffset.x;
        const float ty = screenY + popupOffset.y;

        const float fx = Mathf::Lerp(m_startPos.x, tx, t);
        const float fy = Mathf::Lerp(m_startPos.y, ty, t);

        if (t >= 1.f)
        {
            m_isPopupComplete = true;  // �˾� ���� �Ϸ�
            m_phase = PopupPhase::None;
            m_rect->SetAnchoredPosition({ tx, ty });

            if (m_popupRaised) { ApplyOrderDelta(-layerOrderFoward); m_popupRaised = false; } // �� ����
        }
        else
        {
            m_rect->SetAnchoredPosition({ fx, fy });
        }

        // ���� ���� ���� �� ����
        m_prevIsSetPopup = m_isSetPopup;
        return;
    }
    else if (m_phase == PopupPhase::ToScreen)
    {
        if (!m_popupInit)
        {
            m_popupInit = true;
            m_popupElapsed = 0.f;
            // ������: ���� �˾� ��ġ(���� popupOffset)
            m_startPos = { screenX + popupOffset.x, screenY + popupOffset.y, 0.f };
        }

        m_popupElapsed += tick;
        const float t = std::clamp(m_popupElapsed / m_duration, 0.f, 1.f);

        const float fx = Mathf::Lerp(m_startPos.x, baseX, t);
        const float fy = Mathf::Lerp(m_startPos.y, baseY, t);

        if (t >= 1.f)
        {
            m_phase = PopupPhase::None;
            m_isPopupComplete = false;  // ���� �Ϸ�
            m_bobbing = true;   // ���� �簳 (���ϸ� true�� ������)
            m_rect->SetAnchoredPosition({ baseX, baseY });

            if (m_dismissRaised) { ApplyOrderDelta(-layerOrderFoward); m_dismissRaised = false; }

            if (m_isPurchased && m_image) {
                m_image->SetEnabled(false);
                // ���� �ʿ� �� �ݶ��̴�/������Ʈ�� ��Ȱ��ȭ:
            }
        }
        else
        {
            m_rect->SetAnchoredPosition({ fx, fy });
        }

        m_prevIsSetPopup = m_isSetPopup;
        return;
    }

    // --- 3) Ʈ���� �ƴ� �� ---
    if (m_isSetPopup && m_isPopupComplete)
    {
        // �˾� ����: �˾� ��ġ ����
        m_rect->SetAnchoredPosition({ screenX + popupOffset.x, screenY + popupOffset.y });
    }
    else
    {
        // ����: m_bobbing==true�� ���� ���� ����
        float finalY = baseY;
        if (m_bobbing)
        {
            m_bobTime += tick;
            const float damping = (m_bobDamping > 0.f) ? std::exp(-m_bobDamping * m_bobTime) : 1.f;
            const float omegaT = 2.f * float(Mathf::pi) * m_bobFreq * m_bobTime + m_bobPhase;
            finalY += m_bobAmp0 * damping * std::sinf(omegaT);
        }
        m_rect->SetAnchoredPosition({ baseX, finalY });
    }

    // --- 4) �������� ���� ���¸� prev�� ���� (���� ������ ���� ������) ---
    m_prevIsSetPopup = m_isSetPopup;
}

void ItemUIIcon::SetTarget(GameObject* target)
{
    m_target = target;
    if (m_target)
    {
        m_itemComp = m_target->GetComponent<ItemComponent>();
        if (m_itemComp)
        {
            m_itemComp->SetItemIcon(this);
        }
    }
}
void ItemUIIcon::SetItemID(int id)
{
    itemID = id;
    if (m_image)
    {
    }

}

void ItemUIIcon::SetItemEnhancement(int id)
{
	itemTypeID = id;
    if (m_image && 0 < itemTypeID)
    {
        constexpr int CONVERT_TYPE_OFFSET = 1;
        m_image->SetTexture(itemTypeID - CONVERT_TYPE_OFFSET); // HP ������
    }
}

void ItemUIIcon::SetRarityID(int id)
{
    rarityID = id;
}

void ItemUIIcon::ApplyOrderDelta(int delta)
{
    if (m_image) { int cur = m_image->GetLayerOrder();   m_image->SetOrder(cur + delta); }
    if (m_bgImage) { int cur = m_bgImage->GetLayerOrder(); m_bgImage->SetOrder(cur + delta); }
}
