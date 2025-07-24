#include "TweenManager.h"
#include "pch.h"
#include <algorithm>
#include <omp.h>
void TweenManager::Start()
{
}

void TweenManager::Update(float tick)
{
}

void TweenManager::OnDestroy()
{
    KillAll();
}

void TweenManager::AddTween(std::shared_ptr<ITween> tween)
{
	tweensToAdd.push_back(tween);
}

void TweenManager::UpdateTween(float deltaTime)
{
    // 1. 새 트윈 추가
    if (!tweensToAdd.empty()) {
        activeTweens.insert(activeTweens.end(), tweensToAdd.begin(), tweensToAdd.end());
        tweensToAdd.clear();
    }
    // 2. 모든 트윈 업데이트
    for (int i = 0; i < activeTweens.size(); ++i) {
        activeTweens[i]->Update(deltaTime);
    }
    // 3. 완료된(Killed) 트윈들을 제거
    activeTweens.erase(
        std::remove_if(activeTweens.begin(), activeTweens.end(),
            [](const std::shared_ptr<ITween>& tween) {
                return !tween->IsAlive();
            }),
        activeTweens.end()
    );
}

void TweenManager::KillAll()
{
    for (int i = 0; i < activeTweens.size(); ++i) {
        activeTweens[i]->Kill();
    }
    activeTweens.clear();
}
