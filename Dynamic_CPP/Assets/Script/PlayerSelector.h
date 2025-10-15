#pragma once
#include "Core.Minimal.h"
#include "PlayerSelectorTypes.h"
#include "ModuleBehavior.h"
#include "ICustomEditor.h"

class PlayerSelector : public ModuleBehavior, public ICustomEditor
{
public:
	MODULE_BEHAVIOR_BODY(PlayerSelector)
	virtual void Start() override;
	virtual void Update(float tick) override;

	void MoveStep(int playerIndex, int step);
	void SetAxis(int playerIndex, int axis);

	static inline bool IsSide(SelectorSlot s) {
		return s == SelectorSlot::Left || s == SelectorSlot::Right;
	}

	bool IsOccupiedByOther(SelectorSlot target, int playerIndex) const;
	void RegisterObserver(IPlayerSelectorObserver* observer);
	void UnregisterObserver(IPlayerSelectorObserver* observer);
	SelectorSlot GetSlot(int playerIndex) const;

	// ICustomEditor을(를) 통해 상속됨
	void OnInspectorGUI() override;

private:
	void Notify(const SelectorState& s);

private:
	std::array<SelectorSlot, 2>				m_slot{ SelectorSlot::Neutral, SelectorSlot::Neutral };
	std::array<int, 2>						m_axis{ 0, 0 }; // -1/0/+1
	std::vector<IPlayerSelectorObserver*>	m_observers;

};
