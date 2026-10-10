#pragma once
#include "InputSession.h"
#include "InputSubscriptions.h"
#include <deque>
#include <vector>

class InputSessionComponent;

// GT owns sessions and both consumer frontiers. The asynchronous platform producer
// owns only InputManager's finite ingress. Neither UI nor game steals the other's records.
class InputSessionSystem final
{
public:
    static InputSessionSystem& Get();
    void Register(InputSessionComponent* component);
    void Unregister(InputSessionComponent* component);
    Input::InputSession* ResolveSession(Input::InputSessionHandle handle);
    bool RetireSession(InputSessionComponent* component, Input::CancelReason reason);
    bool RequestDeviceAssignment(Input::InputSessionHandle handle, Input::DeviceID device,
        std::uint64_t deviceEpoch, std::uint64_t assignmentEpoch, bool assigned);
    bool RequestLayerChange(Input::InputSessionHandle handle, Input::LayerID layer, bool enabled);
    bool RequestRebind(Input::InputSessionHandle handle, std::span<const Input::InputBindingOverride> overrides,
        std::vector<Input::InputDiagnostic>& diagnostics);
    bool RequestHaptic(Input::InputSessionHandle handle, float seconds, float left, float right);
    void ApplyAuthoringChanges();
    void PumpUI(bool paused, double timeScale);
    void BeforeFixedStep(double fixedSeconds, double droppedSeconds);
    void Stop();
    Input::InputVector2 GetUIPointerAt(std::uint64_t sequence) const;
    const own::shared_owner<const Input::InputFrame>& GetUIFrame() const noexcept { return m_uiFrame; }

    static constexpr Input::SignalID kPointerSignal{0x43524541544f5255, 1};
    static constexpr Input::SignalID kClickSignal{0x43524541544f5255, 2};
    static constexpr Input::SignalID kNavigateSignal{0x43524541544f5255, 3};
    static constexpr Input::SignalID kSubmitSignal{0x43524541544f5255, 4};

private:
    struct Entry
    {
        InputSessionComponent* component{};
        std::uint64_t generation{1};
    };
    InputSessionSystem() = default;
    void Collect();
    void Evaluate(Input::Domain domain, Input::InputBoundary boundary);
    void InitializeUI();
    void DrainRetirements(bool publish);
    struct Retirement
    {
        Input::InputSessionHandle handle{};
        std::array<own::shared_owner<const Input::InputFrame>, 2> frames;
        own::shared_owner<Input::InputSubscriptionRegistry> subscriptions;
    };
    std::vector<Entry> m_entries;
    std::vector<Retirement> m_retirements;
    std::deque<Input::RoutedInputRecord> m_history;
    std::vector<Input::RoutedInputRecord> m_ingress;
    std::vector<Input::RoutedInputRecord> m_batch;
    std::uint64_t m_gameCursor{};
    std::uint64_t m_uiCursor{};
    std::uint64_t m_gameSequence{};
    std::uint64_t m_uiSequence{};
    Input::Timestamp m_gameFrontier{};
    Input::Timestamp m_uiFrontier{};
    bool m_started{};
    bool m_evaluating{};
    bool m_paused{};
    bool m_gameGap{};
    bool m_uiGap{};
    struct PointerSample
    {
        std::uint64_t sequence{};
        Input::InputVector2 position{};
    };
    std::vector<PointerSample> m_uiPointers;
    Input::InputVector2 m_uiPointerStart{};
    Input::InputVector2 m_uiPointerLast{};
    Input::InputSession m_uiSession;
    own::shared_owner<const Input::InputFrame> m_uiFrame;
};
