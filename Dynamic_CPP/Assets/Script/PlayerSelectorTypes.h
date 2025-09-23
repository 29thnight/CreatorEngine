#pragma once

enum class SelectorSlot { Left = -1, Neutral = 0, Right = 1 };

struct SelectorState 
{
    int          playerIndex{ 0 };   // 0 or 1
    SelectorSlot slot{ SelectorSlot::Neutral };
    int          dirAxis{ 0 };       // -1/0/+1 (최근 입력 방향)
};

struct IPlayerSelectorObserver 
{
    virtual ~IPlayerSelectorObserver() = default;
    virtual void OnSelectorChanged(const SelectorState& s) = 0;
};