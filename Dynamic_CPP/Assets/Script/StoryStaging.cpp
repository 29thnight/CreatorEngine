#include "StoryStaging.h"
#include "TweenManager.h"
#include "pch.h"
#include "Player.h"
#include "LetterboxController.h"

void StoryStaging::Start()
{
	auto& childs = GetOwner()->m_childrenIndices;

	int index = 0;
	for (auto& c : childs) {
		stagingPositions[index] = GameObject::FindIndex(c)->m_transform.GetWorldPosition();
		index++;
	}
	auto diaConductorObj = GameObject::Find("MovieModeController");
	if (diaConductorObj)
	{
		m_letterboxController = diaConductorObj->GetComponent<LetterboxController>();
	}
}

void StoryStaging::OnTriggerEnter(const Collision& collision)
{
	if (m_actionEnd) return;

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
	if (m_actionEnd) return;

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

	int index = 0;
	for (auto& player : players) {
		Player* p = player->GetComponent<Player>();
		p->StagingStart();

		Mathf::Vector3 playerPos = player->m_transform.GetWorldPosition();
		Mathf::Vector3 direction = stagingPositions[0] - playerPos;
		Vector3 right = Vector3::Up.Cross(direction);
		if (right.LengthSquared() < 0.0001f)
			right = -Vector3::Right; // fallback for colinear
		right.Normalize();
		Vector3 up = direction.Cross(right);

		Matrix rotMatrix = Matrix(
			right.x, right.y, right.z, 0,
			up.x, up.y, up.z, 0,
			direction.x, direction.y, direction.z, 0,
			0, 0, 0, 1
		);

		Quaternion rot = Quaternion::CreateFromRotationMatrix(rotMatrix);

		auto tween = std::make_shared<Tweener<float>>(
			[=]() { return 0.f; },
			[=](float val) {
				Quaternion currentRotation = player->m_transform.GetWorldQuaternion();
				Quaternion newRot = Quaternion::Slerp(currentRotation, rot, 0.5f);

				player->m_transform.SetRotation(newRot);
			},
			1.f,
			1.f,
			[&](float t) {
				return Easing::Linear(t);
			}
		);

		float distance = Mathf::Vector3::Distance(playerPos, stagingPositions[index]);

		auto posTween = std::make_shared<Tweener<float>>(
			[=]() { return 0.f; },
			[=](float val) {
				player->m_transform.SetPosition(Mathf::Vector3::Lerp(playerPos, stagingPositions[index], val));
			},
			1.f,
			distance / 5.f,
			[&](float t) {
				return Easing::Linear(t);
			}
		);

		posTween->SetOnComplete([=]() {
			p->Staging();
			if(!m_letterboxController)
			{
				auto diaConductorObj = GameObject::Find("MovieModeController");
				if (diaConductorObj)
				{
					m_letterboxController = diaConductorObj->GetComponent<LetterboxController>();
				}
			}

			switch (stagingID)
			{
			case 0:
				m_letterboxController->Stap1();
				break;
			case 1:
				m_letterboxController->Stap2();
				break;
			};

			m_actionEnd = true;
		});

		tw->AddTween(tween);
		tw->AddTween(posTween);
		index++;
	}
}
