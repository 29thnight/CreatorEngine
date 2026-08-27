#include <atomic>

namespace
{
    std::atomic_bool g_collectGizmoColliders{ false };
}

#include "EnhancedGizmoSceneBinding.h"
#include "SceneManager.h"
#include "Scene.h"
#include "Entity.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "LightRenderProxy.h"
#include "BoxColliderComponent.h"
#include "SphereColliderComponent.h"
#include "CapsuleColliderComponent.h"
#include "CharacterControllerComponent.h"

#include <mathematics/scalar.hpp>
#include <mathematics/transform.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

namespace
{
    // 기즈모가 화면에서 일정 비율을 차지하게 하는 월드 크기. 스냅샷의 FOV
    // 계약은 도 단위이고 삼각함수 경계에서만 라디안으로 바꾼다.
    float EnhancedGizmoSceneScale(const math::vector3& gizmoPosition,
        const FrameCameraSnapshot& snapshot, float targetScreenHeightRatio)
    {
        const float distanceLength = math::distance(snapshot.eyePosition, gizmoPosition);

        const float verticalFovRadians = math::radians(snapshot.fov);
        const float screenHeight = 2.0f * distanceLength * tanf(verticalFovRadians * 0.5f);

        return screenHeight * targetScreenHeightRatio;
    }

    math::vector3 EnhancedGizmoTransformScale(const math::matrix4x4& transform)
    {
        return math::vector3{ math::length(transform.right()),
            math::length(transform.up()), math::length(transform.forward()) };
    }
}

bool BuildEnhancedGizmoSceneData(const FrameCameraSnapshot& snapshot,
    bool collectColliders,
    std::shared_ptr<const EnhancedGizmoIconTextures> iconTextures,
    EnhancedGizmoLineCollector& lineCollector,
    EnhancedGizmoSceneData& out)
{
    Scene* activeScene = SceneManagers->GetActiveScene();
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
            icon.position = object->Transform_().GetWorldPosition();
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
                // ★ 기즈모는 렌더가 받을 값을 그대로 읽는다.
                //
                //   예전에는 LightComponent::m_direction을 읽었는데, 그 필드는
                //   OnInitialized의 ApplyLightData가 한 번 채우고 그 뒤로 아무도
                //   갱신하지 않는 캐시였다 — 오브젝트를 회전시켜도 기즈모만 초기
                //   방향에 굳어 있었다. 조명은 멀쩡했으니 증상이 기즈모에만 났다.
                //
                //   고치는 방식이 요점이다. 방향식을 여기 한 벌 더 적으면 세 번째
                //   사본이 되고, 규약이 바뀔 때 또 어긋난다. 대신 렌더 프록시가
                //   읽는 바로 그 함수를 부른다 — 몇 줄 뒤 ProxyCommand가 부를
                //   것과 같은 함수이므로 결과가 비트 단위로 같고, 앞으로 방향
                //   규약을 바꿔도 ReadFrom 한 곳만 고치면 둘 다 따라온다.
                //
                //   프록시 **객체**(m_lightProxyMap)를 읽지 않는 이유도 분명하다.
                //   이 수집은 게임 스레드에서 프레임 패킷을 만드는 중에 도는데,
                //   이번 프레임 커맨드는 아직 CapturePending 전이라 큐에 있다.
                //   맵을 뒤지면 한 프레임 늦은 값을 스핀락까지 잡고 읽게 된다.
                const LightRenderProxy::Values lightValues =
                    LightRenderProxy::ReadFrom(lightComponent);

                const math::vector3& worldPosition = lightValues.worldPosition;
                const math::vector3& lightDirection = lightValues.direction;
                const float gizmoScale =
                    EnhancedGizmoSceneScale(worldPosition, snapshot, 0.05f);

                switch (lightValues.lightType)
                {
                case DirectionalLight:
                    lineCollector.AddWireCircleWithDirectionLines(worldPosition, gizmoScale,
                        lightDirection, lightDirection, { 1, 0, 1, 1 });
                    ++out.selectionShapes;
                    break;
                case PointLight:
                    lineCollector.AddWireSphere(worldPosition, lightValues.range,
                        { 1, 1, 0, 1 });
                    ++out.selectionShapes;
                    break;
                case SpotLight:
                    lineCollector.AddWireCone(worldPosition, lightDirection,
                        lightValues.range, lightValues.spotLightAngle,
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
                    if (const auto frustum = cameraComponent->TryGetFrustum())
                    {
                        lineCollector.AddBoundingFrustum(*frustum, { 1, 0, 1, 1 });
                        ++out.selectionShapes;
                    }
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
            const math::matrix4x4& world = box->GetOwner()->Transform_().GetWorldMatrix();
            const math::matrix4x4 offset = math::compose({ 1.f, 1.f, 1.f },
				box->GetRotationOffset(), box->GetPositionOffset());
            lineCollector.AddWireBox(offset * world,
				box->GetExtents(),
                { 1.f, 0.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* sphere : activeScene->GetSphereColliderComponents())
        {
            if (!sphere) continue;
            const math::matrix4x4& world = sphere->GetOwner()->Transform_().GetWorldMatrix();
            const math::matrix4x4 offset = math::compose({ 1.f, 1.f, 1.f },
				sphere->GetRotationOffset(), sphere->GetPositionOffset());
            const math::matrix4x4 transformMatrix = offset * world;
            const math::vector3 center = transformMatrix.translation();
            const math::vector3 scale = EnhancedGizmoTransformScale(transformMatrix);
            const float radius =
                sphere->GetRadius() * (std::max)({ scale.x, scale.y, scale.z });
            lineCollector.AddWireSphere(center, radius, { 0.f, 1.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* capsule : activeScene->GetCapsuleColliderComponents())
        {
            if (!capsule) continue;
            const math::matrix4x4& world = capsule->GetOwner()->Transform_().GetWorldMatrix();
            const math::matrix4x4 offset = math::compose({ 1.f, 1.f, 1.f },
				capsule->GetRotationOffset(), capsule->GetPositionOffset());
            const math::matrix4x4 transformMatrix = offset * world;
            const math::vector3 scale = EnhancedGizmoTransformScale(transformMatrix);
            const float radius = capsule->GetRadius() * (std::max)({ scale.x, scale.z });
            const float height = capsule->GetHeight() * scale.y;
            lineCollector.AddWireCapsule(transformMatrix, radius, height, { 0.f, 0.f, 1.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* characterController : activeScene->GetCharacterControllerComponents())
        {
            if (!characterController) continue;
            const math::matrix4x4& world =
                characterController->GetOwner()->Transform_().GetWorldMatrix();
            const math::matrix4x4 offset = math::compose({ 1.f, 1.f, 1.f },
				characterController->GetRotationOffset(),
				characterController->GetPositionOffset());
            const math::matrix4x4 transformMatrix = offset * world;
            const math::vector3 scale = EnhancedGizmoTransformScale(transformMatrix);
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


void SetCollectGizmoColliders(bool enabled) noexcept
{
    g_collectGizmoColliders.store(enabled, std::memory_order_release);
}

bool ShouldCollectGizmoColliders() noexcept
{
    return g_collectGizmoColliders.load(std::memory_order_acquire);
}
