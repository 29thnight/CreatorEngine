#include "StoryStaging.h"
#include "TweenManager.h"
#include "pch.h"

void StoryStaging::Start()
{
	auto& childs = GetOwner()->m_childrenIndices;

	int index = 0;
	for (auto& c : childs) {
		stagingPositions[index] = GameObject::FindIndex(c)->m_transform.GetWorldPosition();
		index++;
	}
}

void StoryStaging::OnTriggerEnter(const Collision& collision)
{
	if (collision.otherObj->m_tag == "Player") {
		players.push_back(collision.otherObj);
		if (players.size() == 2) {
			StartAction();
			/*for (auto& p : players)
			{
				p->m_transform.SetPosition(Mathf::Vector3::Zero);
			}*/
		}
	}
}

void StoryStaging::OnTriggerExit(const Collision& collision)
{
	if (collision.otherObj->m_tag == "Player") {
		players.erase(
			std::remove_if(players.begin(), players.end(),
				[&](const GameObject* g) {
					return collision.otherObj == g;
				}),
			players.end()
		);
	}
}

void StoryStaging::StartAction()
{
	TweenManager* tw = GameObject::Find("GameManager")->GetComponent<TweenManager>();
	if (tw == nullptr) return;

	for (auto& player : players) {
		auto tween = std::make_shared<Tweener<float>>(
			[=]() { return 0.f; },
			[=](float val) {

			},
			1.f,
			1.f,
			[&](float t) {
				return Easing::Linear(t);
			}
		);

		tw->AddTween(tween);
	}
}
