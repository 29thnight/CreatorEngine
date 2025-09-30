#include "TimerTrigger.h"

void TimerTrigger::Update(float dt)
{
	m_elapsed += dt;
	EventSignal sig{ EventSignalType::TickTimer };
	sig.f = dt;
	Raise(sig);
	if (m_elapsed >= m_duration) m_finished = true; // optional one-shot
}
