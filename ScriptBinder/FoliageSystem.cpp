#include "FoliageSystem.h"
#include "FoliageComponent.h"
#include "GameObject.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include "Camera.h"
#include <algorithm>

void FoliageSystem::Register(FoliageComponent* foliage)
{
    if (nullptr == foliage) return;

    if (std::ranges::find(m_foliages, foliage) != m_foliages.end()) return;
    m_foliages.push_back(foliage);
}

void FoliageSystem::Unregister(FoliageComponent* foliage)
{
    if (nullptr == foliage) return;

    // swap-and-pop — AnimatorSystem::Unregister와 같은 규약.
    for (size_t i = 0; i < m_foliages.size(); ++i)
    {
        if (m_foliages[i] != foliage) continue;
        m_foliages[i] = m_foliages.back();
        m_foliages.pop_back();
        return;
    }
}

void FoliageSystem::Update(float tick)
{
    // 옛 FoliageComponent::Update가 컴포넌트마다 다시 읽던 두 전역 조회를
    // 루프 밖으로 끌어올렸다(트랙 C3 이관 실익 — FoliageSystem.h 상단 주석
    // 참고). 둘 다 프레임 동안 불변이라 컴포넌트마다 다시 볼 이유가 없다.
    //
    // 옛 본문은 "scene && renderScene일 때만 camera를 보고, camera가 있으면
    // UpdateFoliageCullingData"였다 — renderScene 없음/camera 없음 둘 다
    // 조기 반환 조건이었으므로, 순서를 바꿔도(레지시 카메라를 renderScene 뒤에
    // 얻어도) 관측 가능한 동작은 같다.
    auto renderScene = SceneManagers->GetRenderScene();
    if (nullptr == renderScene) return;

    auto camera = CameraManagement->GetLastCamera();
    if (nullptr == camera) return;

    for (FoliageComponent* foliage : m_foliages)
    {
        if (nullptr == foliage) continue;

        GameObject* owner = foliage->GetOwner();
        if (nullptr == owner || owner->IsDestroyMark()) continue;
        if (!foliage->IsEnabled()) continue;

        // ── 이하 옛 FoliageComponent::Update 본문 그대로(트랙 C3 이관, 카메라·
        // renderScene은 위에서 1회 취득) ──
        if (nullptr == owner->m_ownerScene) continue;

        foliage->UpdateFoliageCullingData(camera.get());
    }
}
