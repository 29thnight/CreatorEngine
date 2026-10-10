#pragma once
#include "Core.Minimal.h"
#include "Component.h"
#include "InputSession.h"
#include "InputSubscriptions.h"
#include "AssetDepot/AssetRequest.h"
#include <mutex>
#include <optional>

class InputSessionSystem;

struct InputSessionConfiguration
{
    FileGuid graph{};
    std::uint64_t user{1};
    int controllerIndex{0};
    bool shareKeyboard{};
    bool evaluateUI{true};
};

class [[reflgen::reflect]] InputSessionComponent : public meta::identity<InputSessionComponent, Component>
{
public:
    [[reflgen::ignore]]
    void gc_trace(gc::tracer& tracer) const override { Component::gc_trace(tracer); }
    void OnAddedToScene() override;
    void OnRemovingFromScene() override;
    void SetGraphGuid(const FileGuid& guid);
    InputSessionConfiguration GetConfiguration() const;
    void RequestConfiguration(InputSessionConfiguration configuration);
    Input::InputSession* GetSession() noexcept { return m_session.IsInitialized() ? &m_session : nullptr; }
    const Input::InputSession* GetSession() const noexcept { return m_session.IsInitialized() ? &m_session : nullptr; }
    Input::InputSessionHandle GetSessionHandle() const noexcept { return m_handle; }
    Input::InputSubscriptionRegistry& GetSubscriptions() { return *m_nativeSubscriptions; }
    own::shared_owner<const Input::InputFrame> GetDiagnosticFrame(Input::Domain domain) const;
    own::shared_owner<const Input::InputGraphProgram> GetDiagnosticProgram() const;
    std::vector<Input::InputDiagnostic> GetRebindDiagnostics() const;
    void RequestRebind(std::vector<Input::InputBindingOverride> overrides);
    void RequestResetOverrides();
    void BeginRebindCapture();
    void CancelRebindCapture();
    bool IsRebindCaptureActive() const;
    std::optional<Input::RoutedInputRecord> GetRebindCandidate() const;
    void SetControllerVibration(float seconds, float left, float right, float low, float high);
    void SetControllerVibration(float seconds, float power);

    FileGuid m_graphGuid{};
    std::uint64_t m_userId{1};
    int m_controllerIndex{0};
    bool m_shareKeyboard{};
    bool m_evaluateUI{true};

private:
    friend class InputSessionSystem;
    friend struct reflgen::access;
    void ApplyConfiguration();
    void RefreshProgram();
    void AddRebindDiagnostic(Input::InputDiagnostic diagnostic);
    bool ApplyRebind(std::span<const Input::InputBindingOverride> overrides,
        std::vector<Input::InputDiagnostic>& diagnostics);
    void RestoreOverrides();
    void PersistOverrides(const Input::InputFrame& frame);
    void Publish(const own::shared_owner<const Input::InputFrame>& frame);
    void ObserveCapture(std::span<const Input::RoutedInputRecord> records, Input::Timestamp now);
    [[reflgen::ignore]]
    Input::InputSession m_session;
    [[reflgen::ignore]]
    own::shared_owner<Input::InputSubscriptionRegistry> m_nativeSubscriptions = own::make_shared<Input::InputSubscriptionRegistry>();
    [[reflgen::ignore]]
    Input::InputSessionHandle m_handle{};
    [[reflgen::ignore]]
    Input::Timestamp m_installedAt{};
    [[reflgen::ignore]]
    FileGuid m_requestedGuid{};
    [[reflgen::ignore]]
    FileGuid m_activeGuid{};
    [[reflgen::ignore]]
    AssetDepot::AssetRequest<Input::InputGraphProgram> m_request;
    [[reflgen::ignore]]
    bool m_requestPending{};
    [[reflgen::ignore]]
    std::uint64_t m_resolverRevision{};
    [[reflgen::ignore]]
    mutable std::mutex m_diagnosticMutex;
    [[reflgen::ignore]]
    InputSessionConfiguration m_diagnosticConfiguration;
    [[reflgen::ignore]]
    std::optional<InputSessionConfiguration> m_pendingConfiguration;
    [[reflgen::ignore]]
    bool m_wasEnabled{true};
    [[reflgen::ignore]]
    bool m_wasUIEnabled{true};
    [[reflgen::ignore]]
    own::shared_owner<const Input::InputGraphProgram> m_diagnosticProgram;
    [[reflgen::ignore]]
    own::shared_owner<const Input::InputGraphProgram> m_baseProgram;
    [[reflgen::ignore]]
    std::array<own::shared_owner<const Input::InputFrame>, 2> m_diagnosticFrames;
    [[reflgen::ignore]]
    std::optional<std::vector<Input::InputBindingOverride>> m_pendingRebind;
    [[reflgen::ignore]]
    std::vector<Input::InputBindingOverride> m_overrides;
    [[reflgen::ignore]]
    bool m_persistPending{};
    [[reflgen::ignore]]
    bool m_resetOverridesRequested{};
    [[reflgen::ignore]]
    std::vector<Input::InputDiagnostic> m_rebindDiagnostics;
    [[reflgen::ignore]]
    bool m_captureRequested{};
    [[reflgen::ignore]]
    bool m_captureActive{};
    [[reflgen::ignore]]
    Input::Timestamp m_captureDeadline{};
    [[reflgen::ignore]]
    Input::Timestamp m_captureStart{};
    [[reflgen::ignore]]
    Input::UserID m_captureUser{};
    [[reflgen::ignore]]
    std::optional<Input::RoutedInputRecord> m_rebindCandidate;
    [[reflgen::ignore]]
    std::vector<Input::ControlID> m_captureBlocked;
    [[reflgen::ignore]]
    std::vector<Input::RoutedInputRecord> m_captureState;
};
