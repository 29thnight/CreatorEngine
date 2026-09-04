#include "OperationTable.h"

#include "ServiceEndpointFile.h"   // UtcTimestamp

#include <algorithm>

namespace CommandService
{
    std::string_view ToString(OperationState state) noexcept
    {
        switch (state)
        {
        case OperationState::Queued:    return "queued";
        case OperationState::Running:   return "running";
        case OperationState::Completed: return "completed";
        }
        return "unknown";
    }

    std::string OperationTable::Create(const std::string& command)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        OperationRecord record;
        record.id         = "op-" + std::to_string(m_nextId++);
        record.command    = command;
        record.state      = OperationState::Queued;
        record.createdUtc = UtcTimestamp();
        record.events.push_back("queued");

        const std::string id = record.id;
        m_records.push_back(std::move(record));
        TrimLocked();
        return id;
    }

    void OperationTable::MarkRunning(const std::string& id)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (OperationRecord& record : m_records)
        {
            if (record.id != id) continue;
            if (OperationState::Completed == record.state) return;
            record.state = OperationState::Running;
            record.events.push_back("running");
            return;
        }
    }

    void OperationTable::Complete(const std::string& id, const std::string& status,
                                  const std::string& code, const std::string& message,
                                  const std::string& dataJson,
                                  double queuedMs, double executedMs, uint32_t waitedFrames)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (OperationRecord& record : m_records)
        {
            if (record.id != id) continue;
            record.state        = OperationState::Completed;
            record.status       = status;
            record.code         = code;
            record.message      = message;
            record.dataJson     = dataJson.empty() ? "{}" : dataJson;
            record.queuedMs     = queuedMs;
            record.executedMs   = executedMs;
            record.waitedFrames = waitedFrames;
            record.completedUtc = UtcTimestamp();
            record.events.push_back("completed");
            return;
        }
    }

    bool OperationTable::Get(const std::string& id, OperationRecord& out) const
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (const OperationRecord& record : m_records)
        {
            if (record.id == id) { out = record; return true; }
        }
        return false;
    }

    bool OperationTable::RequestCancel(const std::string& id)
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        for (OperationRecord& record : m_records)
        {
            if (record.id != id) continue;
            record.cancelRequested = true;
            return true;
        }
        return false;
    }

    void OperationTable::TrimLocked()
    {
        // 완료된 것부터 버린다. 도는 중인 것을 버리면 그 결과가 갈 곳을 잃는다.
        while (m_records.size() > kMaxRecords)
        {
            const auto oldest = std::find_if(m_records.begin(), m_records.end(),
                [](const OperationRecord& record)
                { return OperationState::Completed == record.state; });
            if (oldest == m_records.end()) break;   // 전부 진행 중이면 더 못 버린다
            m_records.erase(oldest);
        }
    }

    JsonValue OperationTable::ToJson(const OperationRecord& record)
    {
        JsonValue root = JsonValue::Object();
        root.Set("schemaVersion", JsonValue::Int(1));
        root.Set("operationId",   JsonValue::String(record.id));
        root.Set("command",       JsonValue::String(record.command));
        root.Set("state",         JsonValue::String(std::string(ToString(record.state))));
        root.Set("createdUtc",    JsonValue::String(record.createdUtc));

        if (OperationState::Completed == record.state)
        {
            root.Set("status",  JsonValue::String(record.status));
            root.Set("code",    JsonValue::String(record.code));
            root.Set("message", JsonValue::String(record.message));

            const JsonParseResult data = ParseJson(record.dataJson);
            root.Set("data", data.ok ? data.value : JsonValue::Object());

            JsonValue timing = JsonValue::Object();
            timing.Set("queuedMs",     JsonValue::Double(record.queuedMs));
            timing.Set("waitedFrames", JsonValue::Int(static_cast<int64_t>(record.waitedFrames)));
            timing.Set("executedMs",   JsonValue::Double(record.executedMs));
            root.Set("timing", std::move(timing));

            root.Set("completedUtc", JsonValue::String(record.completedUtc));
        }

        root.Set("cancelRequested", JsonValue::Bool(record.cancelRequested));
        return root;
    }
}
