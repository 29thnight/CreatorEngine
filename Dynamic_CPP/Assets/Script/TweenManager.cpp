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
    // 1. �� Ʈ�� �߰�
    if (!tweensToAdd.empty()) {
        activeTweens.insert(activeTweens.end(), tweensToAdd.begin(), tweensToAdd.end());
        tweensToAdd.clear();
    }
    // 2. ��� Ʈ�� ������Ʈ
    for (int i = 0; i < activeTweens.size(); ++i) {
        activeTweens[i]->Update(deltaTime);
    }
    // 3. �Ϸ��(Killed) Ʈ������ ����
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
