#ifndef DYNAMICCPP_EXPORTS
#include "EnhancedGizmoSceneBinding.h"
#include "RenderPassData.h"
#include "RenderScene.h"
#include "DataSystem.h"
#include "EngineSetting.h"
#include "Scene.h"
#include "GameObject.h"
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
    bool collectColliders, EnhancedGizmoLinePass& linePass,
    EnhancedGizmoSceneData& out)
{
    RenderScene* activeRenderScene = RenderPassData::GetActiveRenderScene();
    Scene* activeScene =
        (nullptr != activeRenderScene) ? activeRenderScene->GetScene() : nullptr;
    if (nullptr == activeScene) return false;

    // ── 아이콘 대상 — 카메라·라이트 오브젝트 ──
    //
    // GizmoPass와 같은 스냅샷 복사: 게임 스레드가 목록을 채우는 도중일 수
    // 있어 shared_ptr로 수명까지 붙들고 훑는다(3-0에서 실증된 부류).
    {
        const std::vector<std::shared_ptr<GameObject>> sceneObjects =
            activeScene->m_SceneObjects;

        for (const auto& object : sceneObjects)
        {
            if (!object) continue;

            Texture* iconTexture = nullptr;
            bool isIconTarget = false;

            if (nullptr != object->GetComponent<CameraComponent>())
            {
                // DataSystem이 다른 에디터 아이콘과 함께 CameraGizmo.png를 CPU
                // Texture로 들고 있고, DX12TextureCache가 같은 자산 경로를 GPU에
                // 올린다. nullptr를 넘기면 캐시의 1x1 흰색 폴백이 알파 0.5인
                // 아이콘 크기 사각형으로 보이므로 실제 카메라 자산을 반드시 건넨다.
                iconTexture = DataSystems->CameraIcon;
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
                        ? DataSystems->MainLightIcon : DataSystems->DirectionalLightIcon;
                    break;
                case PointLight:
                    iconTexture = DataSystems->PointLightIcon;
                    break;
                case SpotLight:
                    iconTexture = DataSystems->SpotLightIcon;
                    break;
                default:
                    break;
                }
                isIconTarget = true;
                ++out.lightIcons;
            }

            if (!isIconTarget) continue;

            EnhancedGizmoIconPass::Icon icon{};
            icon.position = Mathf::Vector3(object->m_transform.GetWorldPosition());
            icon.position.y -= 0.5f;   // DX11 호출부의 보정 그대로
            icon.size = 1.f;
            icon.texture = iconTexture;
            out.icons.push_back(icon);
        }
    }

    // ── 선택 오브젝트의 기즈모 — DX11 GizmoLinePass의 스위치 그대로 ──
    if (auto selectedObject = activeScene->GetSelectSceneObject())
    {
        if (GameObjectType::Light == selectedObject->m_gameObjectType)
        {
            if (auto* lightComponent = selectedObject->GetComponent<LightComponent>())
            {
                const Mathf::Vector3 worldPosition =
                    selectedObject->m_transform.GetWorldPosition();
                const Mathf::Vector3 lightDirection =
                    Mathf::Vector3(lightComponent->m_direction);
                const float gizmoScale =
                    EnhancedGizmoSceneScale(worldPosition, snapshot, 0.05f);

                switch (lightComponent->m_lightType)
                {
                case DirectionalLight:
                    linePass.AddWireCircleWithDirectionLines(worldPosition, gizmoScale,
                        lightDirection, lightDirection, { 1, 0, 1, 1 });
                    ++out.selectionShapes;
                    break;
                case PointLight:
                    linePass.AddWireSphere(worldPosition, lightComponent->m_range,
                        { 1, 1, 0, 1 });
                    ++out.selectionShapes;
                    break;
                case SpotLight:
                    linePass.AddWireCone(worldPosition, lightDirection,
                        lightComponent->m_range, lightComponent->m_spotLightAngle,
                        { 0, 1, 1, 1 });
                    ++out.selectionShapes;
                    break;
                default:
                    break;
                }
            }
        }
        else if (GameObjectType::Camera == selectedObject->m_gameObjectType)
        {
            if (auto* cameraComponent = selectedObject->GetComponent<CameraComponent>())
            {
                auto camera = cameraComponent->GetCamera();
                if (nullptr != camera && !camera->m_isOrthographic)
                {
                    linePass.AddBoundingFrustum(cameraComponent->GetFrustum(),
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
            const auto world = box->GetOwner()->m_transform.GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(box->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(box->GetPositionOffset());
            linePass.AddWireBox(offset * world, box->GetExtents(), { 1.f, 0.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* sphere : activeScene->GetSphereColliderComponents())
        {
            if (!sphere) continue;
            const auto world = sphere->GetOwner()->m_transform.GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(sphere->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(sphere->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto center = transformMatrix.Translation();
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius =
                sphere->GetRadius() * (std::max)({ scale.x, scale.y, scale.z });
            linePass.AddWireSphere(center, radius, { 0.f, 1.f, 0.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* capsule : activeScene->GetCapsuleColliderComponents())
        {
            if (!capsule) continue;
            const auto world = capsule->GetOwner()->m_transform.GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(capsule->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(capsule->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius = capsule->GetRadius() * (std::max)({ scale.x, scale.z });
            const float height = capsule->GetHeight() * scale.y;
            linePass.AddWireCapsule(transformMatrix, radius, height, { 0.f, 0.f, 1.f, 1.f });
            ++out.colliderShapes;
        }
        for (auto* characterController : activeScene->GetCharacterControllerComponents())
        {
            if (!characterController) continue;
            const auto world =
                characterController->GetOwner()->m_transform.GetWorldMatrix();
            const auto offset =
                Mathf::Matrix::CreateFromQuaternion(characterController->GetRotationOffset())
                * Mathf::Matrix::CreateTranslation(characterController->GetPositionOffset());
            const auto transformMatrix = offset * world;
            const auto scale = Mathf::ExtractScale(transformMatrix);
            const float radius =
                characterController->m_radius * (std::max)({ scale.x, scale.z });
            const float height = characterController->m_height * scale.y;
            linePass.AddWireCapsule(transformMatrix, radius, height, { 0.f, 1.f, 1.f, 1.f });
            ++out.colliderShapes;
        }
    }

    return true;
}

#endif

bool ShouldCollectGizmoColliders()
{
    // DX11 GizmoLinePass.cpp:86과 같은 판정 — 두 경로가 같은 조건에서
    // 같은 것을 그려야 대조가 성립한다.
    return EngineSettingInstance->IsDebugMode();
}
