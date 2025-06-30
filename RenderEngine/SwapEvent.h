#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "Core.Minimal.h"

class ImplSwapEvent : public Singleton<ImplSwapEvent>
{
private:
	friend class Singleton;

private:
	ImplSwapEvent() = default;
	~ImplSwapEvent() = default;

public:
	Core::Delegate<void> m_swapEvent{};
};

static inline auto& SwapEvent = ImplSwapEvent::GetInstance()->m_swapEvent;
#endif // !DYNAMICCPP_EXPORTS