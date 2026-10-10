#include "InputSessionSystem.h"
#include "InputSessionComponent.h"
#include "InputManager.h"
#include "Entity.h"
#include "SceneManager.h"
#include "ClrHost.h"
#include "LogSystem.h"
#include <algorithm>
#include <cmath>

InputSessionSystem& InputSessionSystem::Get()
{
    static InputSessionSystem instance;
    return instance;
}

void InputSessionSystem::Register(InputSessionComponent* component)
{
    if (!component || std::ranges::any_of(m_entries, [component](const Entry& entry) {
        return entry.component == component;
    }))
    {
        return;
    }
    size_t index = 0;
    for (; index < m_entries.size(); ++index)
    {
        if (!m_entries[index].component)
        {
            break;
        }
    }
    if (index == m_entries.size())
    {
        m_entries.push_back({});
    }
    auto& entry = m_entries[index];
    entry.component = component;
    component->m_nativeSubscriptions = own::make_shared<Input::InputSubscriptionRegistry>();
    component->ApplyConfiguration();
    component->m_handle = {index + 1, entry.generation};
    InputManagement->SetUserDevices(component->m_userId, component->m_controllerIndex, component->m_shareKeyboard);
}

void InputSessionSystem::Unregister(InputSessionComponent* component)
{
    if (!component)
    {
        return;
    }
    for (auto& entry : m_entries)
    {
        if (entry.component != component)
        {
            continue;
        }
        RetireSession(component, Input::CancelReason::SessionShutdown);
        entry.component = nullptr;
        ++entry.generation;
        component->m_request.Cancel();
        component->CancelRebindCapture();
        component->m_handle = {};
        const auto user = component->m_userId;
        const bool userStillPresent = std::ranges::any_of(m_entries, [user](const Entry& other) {
            return other.component && other.component->m_userId == user;
        });
        if (!userStillPresent)
        {
            InputManagement->RemoveUserDevices(user);
        }
        // All raw component and registry references have been released before
        // cancellation subscribers can destroy objects or register new sessions.
        // Self-removal inside a subscriber is an immediate unsubscribe: the
        // registry rejects nested dispatch and invalidation stops the remaining
        // old-generation callbacks. No terminal callback is owed in that path.
        // External retirement still publishes its final cancellation frames.
        DrainRetirements(true);
        return;
    }
}

Input::InputSession* InputSessionSystem::ResolveSession(Input::InputSessionHandle handle)
{
    if (!handle.id || handle.id > m_entries.size())
    {
        return nullptr;
    }
    const auto& entry = m_entries[static_cast<size_t>(handle.id - 1)];
    return entry.component && entry.generation == handle.generation ? entry.component->GetSession() : nullptr;
}

bool InputSessionSystem::RequestDeviceAssignment(Input::InputSessionHandle handle, Input::DeviceID device,
    std::uint64_t deviceEpoch, std::uint64_t assignmentEpoch, bool assigned)
{
    auto* session = ResolveSession(handle);
    return session && session->IsOwnerThread() &&
        InputManagement->AssignDevice(session->GetUser(), device, deviceEpoch, assignmentEpoch, assigned);
}

bool InputSessionSystem::RequestHaptic(Input::InputSessionHandle handle, float seconds, float left, float right)
{
    auto* session = ResolveSession(handle);
    if (!session || !session->IsOwnerThread())
    {
        return false;
    }
    const auto* component = m_entries[static_cast<size_t>(handle.id - 1)].component;
    return InputManagement->SetUserVibration(session->GetUser(), component->m_controllerIndex,
        seconds, {left, right, 0, 0});
}

void InputSessionSystem::InitializeUI()
{
    if (m_uiSession.IsInitialized())
    {
        return;
    }
    Input::InputGraph graph;
    graph.id = {0x43524541544f5255, 0x494e505554554931};
    const Input::LayerID layer{graph.id.high, 1};
    graph.layers.push_back({layer, "UI", 100, Input::ClaimPolicy::PassThrough, true});
    const auto add = [&](Input::SignalID id, const char* name, Input::ValueType type,
        Input::SourceKind kind, std::uint32_t code, Input::CoordinateSpace space) {
        Input::InputSignalDefinition signal;
        signal.id = id;
        signal.layer = layer;
        signal.name = name;
        signal.type = type;
        signal.domain = Input::Domain::UI;
        signal.resumePersistentValue = kind == Input::SourceKind::PointerPosition;
        graph.signals.push_back(signal);
        Input::InputBinding binding;
        binding.id = {id.high, id.low};
        binding.signal = id;
        binding.sources.push_back({{kind, code}, 1.0f, type == Input::ValueType::Vector2 ? 1.0f : 0.0f, space});
        graph.bindings.push_back(std::move(binding));
    };
    add(kPointerSignal, "Pointer", Input::ValueType::Vector2, Input::SourceKind::PointerPosition, 0,
        Input::CoordinateSpace::ClientPixels);
    add(kClickSignal, "Click", Input::ValueType::Button, Input::SourceKind::MouseButton, 0, Input::CoordinateSpace::None);
    add(kNavigateSignal, "Navigate", Input::ValueType::Vector2, Input::SourceKind::GamepadAxis, 0,
        Input::CoordinateSpace::Normalized);
    add(kSubmitSignal, "Submit", Input::ValueType::Button, Input::SourceKind::GamepadButton, 0, Input::CoordinateSpace::None);
    std::vector<Input::InputDiagnostic> diagnostics;
    auto program = Input::CompileInputGraph(graph, diagnostics);
    if (program)
    {
        m_uiSession.Initialize({UINT64_MAX, 1}, 1, std::move(program));
    }
    else
    {
        for (const auto& diagnostic : diagnostics)
        {
            Debug::PrintLog(spdlog::level::err, diagnostic.message);
        }
    }
}

void InputSessionSystem::Collect()
{
    bool gap = false;
    InputManagement->DrainRecords(m_ingress, gap);
    m_gameGap |= gap;
    m_uiGap |= gap;
    const auto now = InputManagement->Now();
    for (auto& entry : m_entries)
    {
        if (entry.component)
        {
            entry.component->ObserveCapture(m_ingress, now);
        }
    }
    for (const auto& record : m_ingress)
    {
        m_history.push_back(record);
    }
    // Both consumer cursors have acknowledged these records. The remaining history
    // still has finite capacity/retention during pause or an overloaded GT.
    while (!m_history.empty() && m_history.front().sequence <= (std::min)(m_gameCursor, m_uiCursor))
    {
        m_history.pop_front();
    }
    while (!m_history.empty() && (m_history.size() > 65536 || now - m_history.front().realTime > 5'000'000'000))
    {
        const auto sequence = m_history.front().sequence;
        m_gameGap |= sequence > m_gameCursor;
        m_uiGap |= sequence > m_uiCursor;
        m_history.pop_front();
    }
}

void InputSessionSystem::Evaluate(Input::Domain domain, Input::InputBoundary boundary)
{
    struct EvaluationScope
    {
        bool& active;
        ~EvaluationScope() { active = false; }
    };
    m_evaluating = true;
    EvaluationScope scope{m_evaluating};
    auto& cursor = domain == Input::Domain::Game ? m_gameCursor : m_uiCursor;
    m_batch.clear();
    for (const auto& record : m_history)
    {
        if (record.sequence <= cursor)
        {
            continue;
        }
        if (record.GetTime(domain) > boundary.end)
        {
            break;
        }
        m_batch.push_back(record);
        cursor = record.sequence;
    }
    // User callbacks may add/remove components. Resolve generation again for each
    // entry instead of keeping an iterator or raw pointer across managed dispatch.
    const auto count = m_entries.size();
    for (size_t index = 0; index < count; ++index)
    {
        if (!m_started)
        {
            break;
        }
        auto* component = m_entries[index].component;
        if (!component)
        {
            continue;
        }
        Entity* owner = component->GetOwner();
        if (!owner || owner->IsDestroyMark())
        {
            continue;
        }
        component->ApplyConfiguration();
        if (auto* oldSession = component->GetSession(); oldSession && oldSession->GetUser() != component->m_userId)
        {
            const auto oldUser = oldSession->GetUser();
            auto program = component->m_baseProgram;
            component->CancelRebindCapture();
            RetireSession(component, Input::CancelReason::DeviceReassigned);
            if (!program || !oldSession->Initialize(component->m_handle, component->m_userId, std::move(program)))
            {
                component->m_resolverRevision = UINT64_MAX;
                continue;
            }
            component->m_installedAt = InputManagement->Now();
            component->RestoreOverrides();
            const bool userStillPresent = std::ranges::any_of(m_entries, [component, oldUser](const Entry& entry) {
                return entry.component && entry.component != component && entry.component->m_userId == oldUser;
            });
            if (!userStillPresent)
            {
                InputManagement->RemoveUserDevices(oldUser);
            }
            InputManagement->SetGraphClaims(component->m_handle, component->m_userId, *oldSession->GetProgram(),
                component->IsEnabled() && component->m_evaluateUI);
        }
        InputManagement->SetUserDevices(component->m_userId, component->m_controllerIndex, component->m_shareKeyboard);
        component->RefreshProgram();
        const auto generation = m_entries[index].generation;
        DrainRetirements(true);
        if (m_entries[index].component != component || m_entries[index].generation != generation)
        {
            continue;
        }
        auto* session = component->GetSession();
        if (!session)
        {
            continue;
        }
        const bool enabled = component->IsEnabled();
        const bool domainEnabled = enabled && (domain != Input::Domain::UI || component->m_evaluateUI);
        auto& wasEnabled = domain == Input::Domain::Game ? component->m_wasEnabled : component->m_wasUIEnabled;
        const bool changed = wasEnabled != domainEnabled;
        if (changed)
        {
            session->QueueCancel(domain, Input::CancelReason::LayerBlocked);
            if (domain == Input::Domain::UI)
            {
                InputManagement->SetSessionClaimsEnabled(component->m_handle, domainEnabled);
            }
            if (domainEnabled)
            {
                InputManagement->RequestResync();
            }
            wasEnabled = domainEnabled;
        }
        // Disabled domains still publish a canceled/neutral immutable frame. No
        // physical records or timers can restart them; re-enable drops this batch
        // and waits for the explicit current-state resync and neutral return.
        const auto first = std::find_if(m_batch.begin(), m_batch.end(), [component](const auto& record) {
            return record.receivedTime >= component->m_installedAt;
        });
        const std::span<const Input::RoutedInputRecord> records = domainEnabled && !changed ?
            std::span<const Input::RoutedInputRecord>(first, m_batch.end()) :
            std::span<const Input::RoutedInputRecord>{};
        auto frame = session->Evaluate(domain, boundary, records);
        const auto subscriptions = component->m_nativeSubscriptions;
        component->Publish(frame);
        if (changed && !domainEnabled && subscriptions)
        {
            subscriptions->InvalidateDomain(domain);
        }
    }
    if (m_started && domain == Input::Domain::UI && m_uiSession.IsInitialized())
    {
        // Built-in canvas navigation explicitly accepts all assigned local users.
        // Each physical device retains its identity, so held sources never alias.
        m_uiPointerStart = m_uiPointerLast;
        m_uiPointers.clear();
        for (const auto& record : m_batch)
        {
            if ((record.kind == Input::RecordKind::Control || record.kind == Input::RecordKind::Resync) &&
                record.control.kind == Input::SourceKind::PointerPosition && record.IsRecipient(Input::Domain::UI))
            {
                m_uiPointerLast = {record.value.x, record.value.y};
                m_uiPointers.push_back({record.sequence, m_uiPointerLast});
            }
        }
        auto uiRecords = m_batch;
        for (auto& record : uiRecords)
        {
            if (record.user != 0)
            {
                record.user = 1;
            }
        }
        m_uiFrame = m_uiSession.Evaluate(domain, boundary, uiRecords);
    }
}

void InputSessionSystem::PumpUI(bool paused, double timeScale)
{
    if (m_evaluating)
    {
        return;
    }
    if (!m_started)
    {
        m_started = true;
        m_gameFrontier = InputManagement->CurrentGameTime();
        m_uiFrontier = InputManagement->Now();
        InputManagement->ResetConsumerHistory();
    }
    InputManagement->CommitGameClock(paused, timeScale);
    InitializeUI();
    Collect();
    const auto now = InputManagement->Now();
    Evaluate(Input::Domain::UI, {++m_uiSequence, m_uiFrontier, now, m_uiGap});
    m_uiFrontier = now;
    m_uiGap = false;
    if (paused || m_paused != paused)
    {
        // No simulation tick is manufactured. A control-only game boundary seals
        // cancellation during pause, drains paused history, and keeps UI real-time.
        const auto gameNow = InputManagement->CurrentGameTime();
        Evaluate(Input::Domain::Game, {++m_gameSequence, m_gameFrontier, gameNow, m_gameGap});
        m_gameFrontier = gameNow;
        m_gameGap = false;
    }
    m_paused = paused;
}

void InputSessionSystem::BeforeFixedStep(double fixedSeconds, double droppedSeconds)
{
    if (!m_started || m_paused || m_evaluating)
    {
        return;
    }
    if (droppedSeconds > 0)
    {
        InputManagement->PublishClockGap();
        m_gameGap = true;
        m_gameFrontier += static_cast<Input::Timestamp>(droppedSeconds * 1'000'000'000.0);
    }
    Collect();
    const auto end = m_gameFrontier + static_cast<Input::Timestamp>(std::llround(fixedSeconds * 1'000'000'000.0));
    Evaluate(Input::Domain::Game, {++m_gameSequence, m_gameFrontier, end, m_gameGap});
    m_gameFrontier = end;
    m_gameGap = false;
}

void InputSessionSystem::Stop()
{
    InputManagement->CommitGameClock(true, 0);
    for (auto& entry : m_entries)
    {
        if (!entry.component)
        {
            continue;
        }
        auto* component = entry.component;
        RetireSession(component, Input::CancelReason::SessionShutdown);
        component->m_resolverRevision = UINT64_MAX;
        component->CancelRebindCapture();
        InputManagement->RemoveUserDevices(component->m_userId);
    }
    DrainRetirements(false);
    if (m_uiSession.IsInitialized())
    {
        m_uiSession.Shutdown(m_gameFrontier, m_uiFrontier);
    }
    m_uiFrame.reset();
    m_history.clear();
    m_gameGap = false;
    m_uiGap = false;
    m_started = false;
    m_paused = false;
}

bool InputSessionSystem::RequestLayerChange(Input::InputSessionHandle handle, Input::LayerID layer, bool enabled)
{
    auto* session = ResolveSession(handle);
    if (!session || !session->IsOwnerThread() || session->GetProgram()->FindLayer(layer) == Input::kInvalidSlot)
    {
        return false;
    }
    // The ordered record, rather than an immediately applied command, owns the
    // layer transition for both cursors and the central source reservation.
    InputManagement->SetLayerClaim(handle, session->GetUser(), layer, enabled);
    return true;
}

bool InputSessionSystem::RequestRebind(Input::InputSessionHandle handle,
    std::span<const Input::InputBindingOverride> overrides, std::vector<Input::InputDiagnostic>& diagnostics)
{
    auto* session = ResolveSession(handle);
    if (!session || !session->IsOwnerThread())
    {
        return false;
    }
    return m_entries[static_cast<size_t>(handle.id - 1)].component->ApplyRebind(overrides, diagnostics);
}

bool InputSessionSystem::RetireSession(InputSessionComponent* component, Input::CancelReason reason)
{
    for (auto& entry : m_entries)
    {
        if (entry.component == component)
        {
            Retirement retired;
            retired.handle = component->m_handle;
            InputManagement->RemoveGraphClaims(retired.handle);
            retired.subscriptions = component->m_nativeSubscriptions;
            component->m_nativeSubscriptions = own::make_shared<Input::InputSubscriptionRegistry>();
            if (component->m_session.IsInitialized())
            {
                component->m_session.Shutdown(m_gameFrontier, m_uiFrontier, reason);
                retired.frames[0] = component->m_session.GetLastFrame(Input::Domain::Game);
                retired.frames[1] = component->m_session.GetLastFrame(Input::Domain::UI);
            }
            m_retirements.push_back(std::move(retired));
            // Invalid for all native lookups now. Managed listeners are retired
            // after the immutable cancellation batch, outside structural mutation.
            component->m_handle.generation = ++entry.generation;
            return true;
        }
    }
    return false;
}

void InputSessionSystem::DrainRetirements(bool publish)
{
    std::vector<Retirement> retired;
    retired.swap(m_retirements);
    for (const auto& value : retired)
    {
        if (publish)
        {
            for (const auto& frame : value.frames)
            {
                if (frame)
                {
                    if (value.subscriptions)
                    {
                        value.subscriptions->Dispatch(*frame);
                    }
                    ClrHost::Get().PublishInputFrame(value.handle, *frame);
                }
            }
        }
        if (value.subscriptions)
        {
            value.subscriptions->InvalidateSession(value.handle);
        }
        ClrHost::Get().InvalidateInputSession(value.handle);
    }
}

Input::InputVector2 InputSessionSystem::GetUIPointerAt(std::uint64_t sequence) const
{
    auto position = m_uiPointerStart;
    for (const auto& sample : m_uiPointers)
    {
        if (sample.sequence > sequence)
        {
            break;
        }
        position = sample.position;
    }
    return position;
}

void InputSessionSystem::ApplyAuthoringChanges()
{
    for (const auto& entry : m_entries)
    {
        if (entry.component)
        {
            entry.component->ApplyConfiguration();
        }
    }
}
