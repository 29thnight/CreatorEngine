#include "ModuleBehavior.h"
#include "SceneManager.h"

void ModuleBehavior::AwakeInvoke()
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	if (m_isCallAwake == false)
	{
		Awake();
		m_isCallAwake = true;
	}
}

void ModuleBehavior::OnEnableInvoke()
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	if (false == m_isCallOnEnable)
	{
		OnEnable();
		m_isCallOnEnable = true;
	}
}

void ModuleBehavior::StartInvoke()
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	if (m_isCallStart == false)
	{
		Start();
		m_isCallStart = true;
	}
}

void ModuleBehavior::FixedUpdateInvoke(float fixedTick)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	FixedUpdate(fixedTick);
}

void ModuleBehavior::OnTriggerEnterInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnTriggerEnter(collider);
}

void ModuleBehavior::OnTriggerStayInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnTriggerStay(collider);
}

void ModuleBehavior::OnTriggerExitInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnTriggerExit(collider);
}

void ModuleBehavior::OnCollisionEnterInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnCollisionEnter(collider);
}

void ModuleBehavior::OnCollisionStayInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnCollisionStay(collider);
}

void ModuleBehavior::OnCollisionExitInvoke(const Collision& collider)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	OnCollisionExit(collider);
}

void ModuleBehavior::UpdateInvoke(float tick)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	Update(tick);
}

void ModuleBehavior::LateUpdateInvoke(float tick)
{
	if (true == m_destroyMark || false == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	LateUpdate(tick);
}

void ModuleBehavior::OnDisableInvoke()
{
	if (true == m_isEnabled) return;
	if (!SceneManagers->m_isGameStart) return;

	if (true == m_isCallOnEnable)
	{
		OnDisable();
		m_isCallOnEnable = false;
	}
}

void ModuleBehavior::OnDestroyInvoke()
{
	if (true != m_destroyMark) return;
	if (!SceneManagers->m_isGameStart) return;

	if (true == m_isCallAwake)
	{
		OnDestroy();
		m_isCallAwake = false;
	}
}