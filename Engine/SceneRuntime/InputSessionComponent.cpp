#include "InputSessionComponent.h"
#include "InputSessionSystem.h"
#include "InputManager.h"
#include "DataSystem.h"
#include "Entity.h"
#include "ClrHost.h"
#include "InputBindingOverrideArchive.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <utility>

void InputSessionComponent::OnAddedToScene() { InputSessionSystem::Get().Register(this); }
void InputSessionComponent::OnRemovingFromScene() { InputSessionSystem::Get().Unregister(this); }
void InputSessionComponent::SetGraphGuid(const FileGuid& guid)
{
    std::lock_guard lock(m_diagnosticMutex);
    auto configuration = m_pendingConfiguration.value_or(m_diagnosticConfiguration);
    configuration.graph = guid;
    m_pendingConfiguration = configuration;
}
InputSessionConfiguration InputSessionComponent::GetConfiguration() const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_pendingConfiguration.value_or(m_diagnosticConfiguration);
}
void InputSessionComponent::RequestConfiguration(InputSessionConfiguration configuration)
{
    if (configuration.user == 0 || configuration.controllerIndex < -1 || configuration.controllerIndex >= 4)
    {
        return;
    }
    std::lock_guard lock(m_diagnosticMutex);
    m_pendingConfiguration = configuration;
}
void InputSessionComponent::ApplyConfiguration()
{
    std::lock_guard lock(m_diagnosticMutex);
    if (m_pendingConfiguration)
    {
        const auto& value = *m_pendingConfiguration;
        m_graphGuid = value.graph;
        m_userId = value.user;
        m_controllerIndex = value.controllerIndex;
        m_shareKeyboard = value.shareKeyboard;
        m_evaluateUI = value.evaluateUI;
        m_pendingConfiguration.reset();
    }
    m_diagnosticConfiguration = {m_graphGuid, m_userId, m_controllerIndex, m_shareKeyboard, m_evaluateUI};
}
void InputSessionComponent::AddRebindDiagnostic(Input::InputDiagnostic diagnostic)
{
    std::lock_guard lock(m_diagnosticMutex);
    m_rebindDiagnostics.push_back(std::move(diagnostic));
}
own::shared_owner<const Input::InputFrame> InputSessionComponent::GetDiagnosticFrame(Input::Domain domain) const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_diagnosticFrames[static_cast<size_t>(domain)];
}
own::shared_owner<const Input::InputGraphProgram> InputSessionComponent::GetDiagnosticProgram() const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_diagnosticProgram;
}
std::vector<Input::InputDiagnostic> InputSessionComponent::GetRebindDiagnostics() const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_rebindDiagnostics;
}
void InputSessionComponent::RequestRebind(std::vector<Input::InputBindingOverride> overrides)
{
    std::lock_guard lock(m_diagnosticMutex);
    m_pendingRebind = std::move(overrides);
}
void InputSessionComponent::RequestResetOverrides()
{
    std::lock_guard lock(m_diagnosticMutex);
    m_resetOverridesRequested = true;
}
void InputSessionComponent::BeginRebindCapture()
{
    std::lock_guard lock(m_diagnosticMutex);
    m_captureRequested = true;
    m_captureStart = InputManagement->Now();
    m_captureUser = m_diagnosticConfiguration.user;
    InputManagement->SetRebindCapture(m_captureUser, true);
    m_rebindCandidate.reset();
}
void InputSessionComponent::CancelRebindCapture()
{
    std::lock_guard lock(m_diagnosticMutex);
    m_captureRequested = false;
    m_captureActive = false;
    InputManagement->SetRebindCapture(m_captureUser, false);
    m_rebindCandidate.reset();
}
bool InputSessionComponent::IsRebindCaptureActive() const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_captureRequested || m_captureActive;
}
std::optional<Input::RoutedInputRecord> InputSessionComponent::GetRebindCandidate() const
{
    std::lock_guard lock(m_diagnosticMutex);
    return m_rebindCandidate;
}
void InputSessionComponent::RefreshProgram()
{
    const auto catalog = DataSystems->GetCookedCatalog();
    const auto revision = catalog ? catalog->ResolverRevision() : 0;
    if (m_requestedGuid != m_graphGuid || revision != m_resolverRevision)
    {
        m_request.Cancel();
        m_request = {};
        m_requestPending = false;
        m_requestedGuid = m_graphGuid;
        m_resolverRevision = revision;
        if (m_graphGuid != FileGuid{})
        {
            const AssetDepot::AssetLink<Input::InputGraphProgram> link{{experiment::AssetId{m_graphGuid.m_guid}, {}}};
            m_request = DataSystems->RequestAsync<Input::InputGraphProgram>(link);
            m_requestPending = true;
        }
        else if (m_session.IsInitialized())
        {
            if (!InputSessionSystem::Get().RetireSession(this, Input::CancelReason::DefinitionChanged))
            {
                return;
            }
            m_activeGuid = {};
            m_baseProgram.reset();
            m_overrides.clear();
            std::lock_guard lock(m_diagnosticMutex);
            m_diagnosticProgram.reset();
        }
    }
    if (m_requestPending)
    {
        const auto result = m_request.Snapshot();
        if (result.status != AssetDepot::AssetRequestStatus::Pending)
        {
            m_requestPending = false;
            if (result.status == AssetDepot::AssetRequestStatus::Ready && result.asset)
            {
                const bool unchanged = m_session.IsInitialized() && m_baseProgram &&
                    m_activeGuid == m_requestedGuid &&
                    m_baseProgram->GetDefinition().id == result.asset->GetDefinition().id &&
                    m_baseProgram->GetSemanticHash() == result.asset->GetSemanticHash();
                if (unchanged)
                {
                    // Display/layout-only asset updates do not cancel live FSMs or
                    // replace a user's already prepared binding overrides.
                    m_baseProgram = result.asset;
                }
                else
                {
                    if (m_session.IsInitialized() &&
                        m_session.GetProgram()->GetDefinition().id != result.asset->GetDefinition().id)
                    {
                        if (!InputSessionSystem::Get().RetireSession(this, Input::CancelReason::DefinitionChanged))
                        {
                            return;
                        }
                    }
                    const bool wasInitialized = m_session.IsInitialized();
                    const bool accepted = wasInitialized ? m_session.QueueProgram(result.asset) :
                        m_session.Initialize(m_handle, m_userId, result.asset);
                    if (!accepted)
                    {
                        AddRebindDiagnostic({{}, {}, "Input graph generation could not be installed."});
                        return;
                    }
                    if (!wasInitialized)
                    {
                        m_installedAt = InputManagement->Now();
                    }
                    m_baseProgram = result.asset;
                    m_activeGuid = m_requestedGuid;
                    {
                        std::lock_guard lock(m_diagnosticMutex);
                        m_diagnosticProgram = result.asset;
                    }
                    RestoreOverrides();
                    InputManagement->SetGraphClaims(m_handle, m_userId, *m_session.GetProgram(), IsEnabled() && m_evaluateUI);
                }
            }
            // Failure deliberately keeps the last good immutable program lease.
        }
    }
    std::optional<std::vector<Input::InputBindingOverride>> pending;
    bool resetOverrides = false;
    {
        std::lock_guard lock(m_diagnosticMutex);
        if (m_session.IsInitialized())
        {
            pending = std::move(m_pendingRebind);
            m_pendingRebind.reset();
            resetOverrides = std::exchange(m_resetOverridesRequested, false);
        }
    }
    if (resetOverrides && m_baseProgram)
    {
        if (m_session.QueueProgram(m_baseProgram, Input::CancelReason::Rebound))
        {
            m_overrides.clear();
            m_persistPending = true;
            {
                std::lock_guard lock(m_diagnosticMutex);
                m_rebindDiagnostics.clear();
            }
            InputManagement->SetGraphClaims(m_handle, m_userId, *m_baseProgram, IsEnabled() && m_evaluateUI);
            CancelRebindCapture();
        }
    }
    if (pending)
    {
        std::vector<Input::InputDiagnostic> diagnostics;
        ApplyRebind(*pending, diagnostics);
        {
            std::lock_guard lock(m_diagnosticMutex);
            m_rebindDiagnostics = std::move(diagnostics);
        }
        CancelRebindCapture();
    }
}
void InputSessionComponent::Publish(const own::shared_owner<const Input::InputFrame>& frame)
{
    auto keepAlive = root_from_this();
    if (!frame)
    {
        return;
    }
    {
        std::lock_guard lock(m_diagnosticMutex);
        m_diagnosticFrames[static_cast<size_t>(frame->GetDomain())] = frame;
        m_diagnosticProgram = m_session.GetProgram();
    }
    if (frame->GetDomain() == Input::Domain::UI)
    {
        for (const auto& event : frame->GetEvents())
        {
            if (event.phase == Input::EventPhase::Performed)
            {
                InputManagement->SetPerformedClaim(m_handle, m_userId, event.signal, true);
            }
            else if (event.phase == Input::EventPhase::Completed || event.phase == Input::EventPhase::Canceled)
            {
                InputManagement->SetPerformedClaim(m_handle, m_userId, event.signal, false);
            }
        }
    }
    PersistOverrides(*frame);
    const auto handle = m_handle;
    const auto subscriptions = m_nativeSubscriptions;
    if (subscriptions)
    {
        subscriptions->Dispatch(*frame);
    }
    if (m_handle == handle && !IsDestroyMark() && InputSessionSystem::Get().ResolveSession(handle) == &m_session)
    {
        ClrHost::Get().PublishInputFrame(handle, *frame);
    }
}
void InputSessionComponent::ObserveCapture(std::span<const Input::RoutedInputRecord> records, Input::Timestamp now)
{
    std::lock_guard lock(m_diagnosticMutex);
    if (m_captureRequested && !m_captureActive)
    {
        m_captureRequested = false;
        m_captureActive = true;
        m_captureDeadline = now + 10'000'000'000;
        m_captureBlocked.clear();
        for (const auto& state : m_captureState)
        {
            if (std::abs(state.value.x) > 0.25f || std::abs(state.value.y) > 0.25f)
            {
                m_captureBlocked.push_back(state.control);
            }
        }
        if (m_session.IsInitialized())
        {
            m_session.QueueCancel(Input::CancelReason::Rebound);
        }
    }
    if (now >= m_captureDeadline)
    {
        m_captureActive = false;
    }
    for (const auto& record : records)
    {
        if (record.user != 0 && record.user != m_userId)
        {
            continue;
        }
        if (record.kind == Input::RecordKind::DeviceDisconnected)
        {
            m_captureActive = false;
        }
        if (record.kind != Input::RecordKind::Control && record.kind != Input::RecordKind::Resync)
        {
            continue;
        }
        const bool neutral = std::abs(record.value.x) <= 0.25f && std::abs(record.value.y) <= 0.25f;
        if (neutral)
        {
            std::erase(m_captureBlocked, record.control);
        }
        else if (record.receivedTime <= m_captureStart || record.kind == Input::RecordKind::Resync)
        {
            if (std::ranges::find(m_captureBlocked, record.control) == m_captureBlocked.end())
            {
                m_captureBlocked.push_back(record.control);
            }
        }
        const auto state = std::ranges::find(m_captureState, record.control, &Input::RoutedInputRecord::control);
        if (state == m_captureState.end())
        {
            m_captureState.push_back(record);
        }
        else
        {
            *state = record;
        }
        if (m_captureActive && record.receivedTime > m_captureStart &&
            record.kind == Input::RecordKind::Control &&
            (std::abs(record.value.x) >= 0.5f || std::abs(record.value.y) >= 0.5f) &&
            record.control.kind != Input::SourceKind::PointerPosition &&
            record.control.kind != Input::SourceKind::MouseDelta &&
            std::ranges::find(m_captureBlocked, record.control) == m_captureBlocked.end())
        {
            m_rebindCandidate = record;
            m_captureActive = false;
        }
    }
    if (!m_captureActive)
    {
        InputManagement->SetRebindCapture(m_captureUser, false);
    }
}
void InputSessionComponent::SetControllerVibration(float seconds, float left, float right, float low, float high)
{
    InputManagement->SetUserVibration(m_userId, m_controllerIndex, seconds, {low, high, left, right});
}
void InputSessionComponent::SetControllerVibration(float seconds, float power)
{
    SetControllerVibration(seconds, power, power, power, power);
}

namespace
{
    std::filesystem::path InputProfilePath(const FileGuid& graph, Input::UserID user)
    {
        // Player RuntimeData is per-process temporary storage. Profiles belong to
        // the OS user and stable graph identity, independently of content extraction.
        wchar_t local[32768]{};
        const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(std::size(local)));
        if (!length || length >= std::size(local))
        {
            return {};
        }
        return std::filesystem::path(local) / L"CreatorEngine" / L"InputProfiles" /
            graph.ToString() / (std::to_string(user) + ".inputoverride");
    }
}

bool InputSessionComponent::ApplyRebind(std::span<const Input::InputBindingOverride> overrides,
    std::vector<Input::InputDiagnostic>& diagnostics)
{
    auto merged = m_overrides;
    for (const auto& value : overrides)
    {
        const auto found = std::ranges::find(merged, value.binding, &Input::InputBindingOverride::binding);
        if (found == merged.end())
        {
            merged.push_back(value);
        }
        else
        {
            *found = value;
        }
    }
    if (!m_session.QueueRebind(merged, diagnostics))
    {
        return false;
    }
    m_overrides = std::move(merged);
    m_persistPending = true;
    InputManagement->SetGraphClaims(m_handle, m_userId, *m_session.GetProgram(), IsEnabled() && m_evaluateUI);
    return true;
}

void InputSessionComponent::RestoreOverrides()
{
    m_overrides.clear();
    m_persistPending = false;
    const auto path = InputProfilePath(m_activeGuid, m_userId);
    std::error_code error;
    if (path.empty() || !std::filesystem::exists(path, error))
    {
        return;
    }
    const auto length = std::filesystem::file_size(path, error);
    if (error || length > Input::kInputOverrideMaxBytes)
    {
        AddRebindDiagnostic({{}, {}, "Input override profile is unreadable or exceeds its size limit."});
        return;
    }
    std::ifstream stream(path, std::ios::binary);
    std::vector<std::byte> bytes(static_cast<size_t>(length));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    {
        AddRebindDiagnostic({{}, {}, "Input override profile could not be read."});
        return;
    }
    std::string failure;
    std::vector<Input::InputBindingOverride> overrides;
    if (!Input::ReadInputBindingOverrides(bytes, m_userId, *m_session.GetProgram(), overrides, failure))
    {
        AddRebindDiagnostic({{}, {}, std::move(failure)});
        return;
    }
    std::vector<Input::InputDiagnostic> diagnostics;
    if (m_session.QueueRebind(overrides, diagnostics))
    {
        m_overrides = std::move(overrides);
    }
    for (auto& diagnostic : diagnostics)
    {
        AddRebindDiagnostic(std::move(diagnostic));
    }
}

void InputSessionComponent::PersistOverrides(const Input::InputFrame& frame)
{
    if (!m_persistPending || !m_session.IsInitialized() ||
        frame.GetSemanticHash() != m_session.GetProgram()->GetSemanticHash())
    {
        return;
    }
    m_persistPending = false;
    const auto path = InputProfilePath(m_activeGuid, m_userId);
    std::vector<std::byte> bytes;
    std::string failure;
    if (path.empty() || !Input::WriteInputBindingOverrides(m_userId, *m_session.GetProgram(), m_overrides, bytes, failure))
    {
        AddRebindDiagnostic({{}, {}, failure.empty() ? "Input profile directory is unavailable." : failure});
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        AddRebindDiagnostic({{}, {}, "Input profile directory could not be created."});
        return;
    }
    auto temporary = path;
    temporary += "." + std::to_string(GetCurrentProcessId()) + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.flush();
        if (!stream)
        {
            AddRebindDiagnostic({{}, {}, "Input profile could not be saved; the live rebind remains active."});
            return;
        }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        std::filesystem::remove(temporary, error);
        AddRebindDiagnostic({{}, {}, "Input profile atomic replacement failed; previous profile is intact."});
    }
}
