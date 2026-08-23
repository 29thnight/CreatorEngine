#include <atomic>

namespace
{
    std::atomic_bool g_collectGizmoColliders{ false };
}

#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoSceneBinding.h"
#include "RenderPassData.h"
#include "RenderScene.h"
#include "Scene.h"
#include "Entity.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterControllerComponent.h"

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
    // DX11 GetGizmoScale의 이식 — 기즈모가 화면에서 일정 비율을 차지하게
    // 하는 월드 크기. fov에 Rad2Deg를 곱하는 quirk까지 그대로다(고치면
    // 기즈모 크기가 원본과 달라져 픽셀 대조가 어긋난다).
    float EnhancedGizmoSceneScale(const Mathf::Vector3& gizmoPosition,
        const FrameCameraSnapshot& snapshot, float targetScreenHeightRatio)
    {
        const Mathf::Vector3 cameraPos = snapshot.eyePosition;
        const Mathf::Vector3 distance = XMVector3Length(cameraPos - gizmoPosition);
        const float distanceLength = distance.Length();

        const float verticalFovRadians = snapshot.fov * Mathf::Rad2Deg;
        const float screenHeight = 2.0f * distanceLength * tanf(verticalFovRadians * 0.5f);

        return screenHeight * targetScreenHeightRatio;
    }
}

bool BuildEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders,
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures,
    EnhancedGizmoLineCollector& lineCollector,
    EnhancedGizmoSceneData& out)
{
    RenderScene* activeRenderScene = RenderPassData::GetActiveRenderScene();
    Scene* activeScene =
        (nullptr != activeRenderScene) ? activeRenderScene->GetScene() : nullptr;
    if (nullptr == activeScene) return false;

    // packet이 raw Texture*를 쓰는 pass 형식과 공유 소유권을 함께 싣는다.
    // EditorAssetPresentation이 shutdown되어도 이미 발행된 packet은 안전하다.
    out.iconTextures = std::move(iconTextures);
    const EnhancedGizmoIconTextures* textures = out.iconTextures.get();

    // ── 아이콘 대상 — 카메라·라이트 오브젝트 ──
    //
	// Scene 구조 변경은 렌더 배리어 안에서만 적용된다. 이 함수가 라이브 렌더
	// 스냅샷을 만드는 동안 슬롯 unique_ptr은 이동/파괴되지 않으므로 Scene의
	// 단독 소유 배열을 비소유로 직접 읽는다.
    {
		for (const auto& object : activeScene->m_Entities)
        {
            if (!object) continue;

            Texture* iconTexture = nullptr;
            bool isIconTarget = false;

            if (nullptr != object->GetComponent<CameraComponent>())
            {
                // nullptr를 넘기면 캐시의 1x1 흰색 폴백이 알파 0.5인 아이콘
                // 크기 사각형으로 보인다. Host가 주입한 실제 자산을 건넨다.
                iconTexture = textures ? textures->camera.get() : nullptr;
                isIconTarget = true;
                ++out.cameraIcons;
            }
            else if (auto* light = object->GetComponent<LightComponent>())
            {
                constexpr int kMainLightIndex = 0;
                const bool isMainLight = (kMainLightIndex == light->m_lightIndex);
                switch (light->m_lightType)
                {
                case DirectionalLight:
                    iconTexture = isMainLight
                        ? (textures ? textures->mainLight.get() : nullptr)
                        : (textures ? textures->directionalLight.get() : nullptr);
                    break;
                case PointLight:
                    iconTexture = textures ? textures->pointLight.get() : nullptr;
                    break;
                case SpotLight:
                    iconTexture = textures ? textures->spotLight.get() : nullptr;
                    break;
                default:
                    break;
                }
                isIconTarget = true;
                ++out.lightIcons;
            }

            if (!isIconTarget) continue;

            EnhancedGizmoIcon icon{};
            icon.position = Mathf::Vector3(object->Transform_().GetWorldPosition());
            icon.position.y -= 0.5f;   // DX11 호출부의 보정 그대로
            icon.size = 1.f;
            icon.texture = iconTexture;
            out.icons.push_back(icon);
        }
    }

    // ── 선택 오브젝트의 기즈모 — DX11 GizmoLinePass의 스위치 그대로 ──
    if (auto selectedObject = activeScene->GetSelectedEntity())
    {
        // ★ E7 — 옛 GameObjectType::Light / ::Camera 검사를 걷었다. 두 분기 다
        // 바로 아래에서 이미 해당 컴포넌트를 조회하고 있어 타입 검사가 중복이었다.
        // "라이트인가"의 정본은 저장된 enum이 아니라 컴포넌트 보유 여부다
        // (K1-a 마스크로 O(1)). 배타(else if)도 함께 풀었다 — 컴포넌트가 정본이면
        // 둘 다 가진 오브젝트에 둘 다 그리는 것이 옳고, 저작 자산에 그런 조합은 없다.
        {
            if (auto* lightComponent = selectedObject->GetComponent<LightComponent>())
            {
                const Mathf::Vector3 worldPosition =
                    selectedObject->Transform_().GetWorldPosition();
                const Mathf::Vector3 lightDirection =
                    Mathf::Vector3(lightComponent->m_direction);
                const float gizmoScale =
                    EnhancedGizmoSceneScale(worldPosition, snapshot, 0.05f);

                switch (lightComponent->m_lightType)
                {
                case DirectionalLight:
                    lineCollector.AddWireCircleWithDirectionLines(worldPosition, gizmoScale,
                        lightDirection, lightDirection, { 1, 0, 1, 1 });
                    ++out.selectionShapes;
                    break;
                case PointLight:
                    lineCollector.AddWireSphere(worldPosition, lightComponent->m_range,
                        { 1, 1, 0, 1 });
                    ++out.selectionShapes;
                    break;
                case SpotLight:
                    lineCollector.AddWireCone(worldPosition, lightDirection,
                        lightComponent->m_range, lightComponent->m_spotLightAngle,
                        { 0, 1, 1, 1 });
                    ++out.selectionShapes;
                    break;
                default:
                    break;
                }
            }
        }
        {
            if (auto* cameraComponent = selectedObject->GetComponent<CameraComponent>())
            {
                auto camera = cameraComponent->GetCamera();
                if (nullptr != camera && !camera->m_isOrthographic)
                {
                    lineCollector.AddBoundingFrustum(cameraComponent->GetFrustum(),
                        { 1, 0, 1, 1 });
                    ++out.selectionShapes;
                }
            }
        }
    }

    // ── 콜라이더 와이어 — DX11의 디버그 모드 수집 그대로 ──
    if (collectColliders)
    {
        for (auto* box : activeScene->GetBoxColliderComponents())
        {
            if (!box) continue;
            const auto world = box->GetOwner()->Transform_().GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(box->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(box->GetPositionOffset());
            lineCollector.AddWireBox(offset * world, box->GetExtents(), { 1.f, 0.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* sphere : activeScene->GetSphereColliderComponents())
        {
            if (!sphere) continue;
            const auto world = sphere->GetOwner()->Transform_().GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(sphere->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(sphere->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto center = transformMatrix.Translation();
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius =
                sphere->GetRadius() * (std::max)({ scale.x, scale.y, scale.z });
            lineCollector.AddWireSphere(center, radius, { 0.f, 1.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* capsule : activeScene->GetCapsuleColliderComponents())
        {
            if (!capsule) continue;
            const auto world = capsule->GetOwner()->Transform_().GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(capsule->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(capsule->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius = capsule->GetRadius() * (std::max)({ scale.x, scale.z });
            const float height = capsule->GetHeight() * scale.y;
            lineCollector.AddWireCapsule(transformMatrix, radius, height, { 0.f, 0.f, 1.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* characterController : activeScene->GetCharacterControllerComponents())
        {
            if (!characterController) continue;
            const auto world =
                characterController->GetOwner()->Transform_().GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(characterController->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(characterController->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius =
                characterController->m_radius * (std::max)({ scale.x, scale.z });
            const float height = characterController->m_height * scale.y;
            lineCollector.AddWireCapsule(transformMatrix, radius, height, { 0.f, 1.f, 1.f, 1.f });
            ++out.colliderShapes;
        }
    }

    return true;
}

bool CaptureEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders,
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures,
    EnhancedGizmoSceneData& out)
{
    // 에디터 패스가 아니라 Core 수집기다(E4-3a) — GT가 표시 계층을
    // 인스턴스화하던 마지막 경로가 이것이었다.
    EnhancedGizmoLineCollector lineCollector;
    if (!BuildEnhancedGizmoSceneData(snapshot, collectColliders,
        std::move(iconTextures), lineCollector, out))
    {
        return false;
    }
    out.lineVertices = lineCollector.GetVertices();
    return true;
}

#endif

void SetCollectGizmoColliders(bool enabled) noexcept
{
    g_collectGizmoColliders.store(enabled, std::memory_order_release);
}

bool ShouldCollectGizmoColliders() noexcept
{
    return g_collectGizmoColliders.load(std::memory_order_acquire);
}
