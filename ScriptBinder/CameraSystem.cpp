#include "CameraSystem.h"
#include "LifecycleTrace.h"
#include "CameraComponent.h"
#include "Camera.h"
#include "GameObject.h"
#include <algorithm>

void CameraSystem::Register(CameraComponent* camera)
{
    if (nullptr == camera) return;

    if (std::ranges::find(m_cameras, camera) != m_cameras.end()) return;
    m_cameras.push_back(camera);
}

void CameraSystem::Unregister(CameraComponent* camera)
{
    if (nullptr == camera) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약(단일 조밀 벡터라
    // 순서 보존은 애초에 불필요하고, erase는 O(n) 시프트라 씬 전환마다 비용이
    // 쌓인다).
    for (size_t i = 0; i < m_cameras.size(); ++i)
    {
        if (m_cameras[i] != camera) continue;
        m_cameras[i] = m_cameras.back();
        m_cameras.pop_back();
        return;
    }
}

void CameraSystem::Update(float tick)
{
    // 옛 Scene::RegistryTick이 공통으로 해주던 가드(owner 없음/파괴 표시/
    // 비활성 스킵)를 이 시스템이 대신 적용한다 — CameraComponent가 더 이상
    // m_schedule.UpdateList()를 거치지 않으므로 그 가드도 함께 옮겨왔다.
    for (CameraComponent* camera : m_cameras)
    {
        if (nullptr == camera) continue;

        GameObject* owner = camera->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!camera->IsEnabled()) continue;
        // 트랙 렌더 — 틱이 시스템으로 옮겨오면서 생명주기 트레이스의 발생지도
        // 함께 옮긴다. 안 남기면 이관할수록 기준선의 커버리지가 조용히 준다
        // (같은 문자열을 써야 대조가 성립하므로 Lifecycle::Trace::TypeNameOf
        // 공용 함수를 쓴다).
        LIFECYCLE_TRACE(Lifecycle::Phase::Update, Lifecycle::Trace::TypeNameOf(camera),
            owner->m_name.ToString().c_str(), camera->GetInstanceID());

        // ── 이하 옛 CameraComponent::Update 본문 그대로(트랙 렌더 이관) ──
        // m_pCamera는 CameraComponent의 사유 멤버라 이미 공개된 GetCamera()로
        // 얻는다 — 전용 진입점을 새로 만들지 않은 이유는 CameraSystem.h 상단
        // 주석 참고. HasTransform() 방어를 넣지 않은 이유도 같은 곳에 있다.
        Camera* cam = camera->GetCamera();
        if (nullptr == cam) continue;

        cam->m_eyePosition = owner->Transform_().GetWorldPosition();
        XMVECTOR rotationQuat = owner->Transform_().GetWorldQuaternion();
        rotationQuat = XMQuaternionNormalize(rotationQuat);

        static const XMVECTOR FORWARD = XMVectorSet(0, 0, 1, 0);
        static const XMVECTOR UP = XMVectorSet(0, 1, 0, 0);
        static const XMVECTOR RIGHT = XMVectorSet(1, 0, 0, 0);

        cam->m_forward = XMVector3Normalize(XMVector3Rotate(FORWARD, rotationQuat));
        cam->m_up = XMVector3Normalize(XMVector3Rotate(UP, rotationQuat));
        cam->m_right = XMVector3Normalize(XMVector3Rotate(RIGHT, rotationQuat));
        cam->m_lookAt = cam->m_eyePosition + cam->m_forward;
    }
}
