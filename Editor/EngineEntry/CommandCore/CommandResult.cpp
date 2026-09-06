#include "CommandResult.h"

#include <algorithm>

namespace CommandCore
{
    std::string_view ToString(CommandStatus status) noexcept
    {
        switch (status)
        {
        case CommandStatus::Succeeded:           return "succeeded";
        case CommandStatus::InvalidArguments:    return "invalid_arguments";
        case CommandStatus::PreconditionsFailed: return "preconditions_failed";
        case CommandStatus::Failed:              return "failed";
        case CommandStatus::Cancelled:           return "cancelled";
        case CommandStatus::TimedOut:            return "timed_out";
        case CommandStatus::InternalError:       return "internal_error";
        }
        return "unknown";
    }

    uint8_t SeverityRank(CommandStatus status) noexcept
    {
        switch (status)
        {
        case CommandStatus::Succeeded:           return 0;

        case CommandStatus::InvalidArguments:    return 2;
        case CommandStatus::PreconditionsFailed: return 3;

        // 셋을 같은 순위에 둔다. 명령이 돌았고 원하는 결과가 안 나왔다는 점이
        // 같고, 자동화가 갈라 다뤄야 할 이유가 아직 없다. 갈릴 이유가 생기면
        // 그때 순위를 벌린다 — 미리 벌려 두면 근거 없는 순서가 계약이 된다.
        case CommandStatus::Failed:              return 4;
        case CommandStatus::Cancelled:           return 4;
        case CommandStatus::TimedOut:            return 4;

        case CommandStatus::InternalError:       return 5;
        }
        return 5;
    }

    // ── CommandData ─────────────────────────────────────────────────────

    CommandData CommandData::Bool(bool value)
    {
        CommandData data;
        data.m_kind = Kind::Bool;
        data.m_bool = value;
        return data;
    }

    CommandData CommandData::Int(int64_t value)
    {
        CommandData data;
        data.m_kind = Kind::Int;
        data.m_int  = value;
        return data;
    }

    CommandData CommandData::Double(double value)
    {
        CommandData data;
        data.m_kind   = Kind::Double;
        data.m_double = value;
        return data;
    }

    CommandData CommandData::String(std::string value)
    {
        CommandData data;
        data.m_kind   = Kind::String;
        data.m_string = std::move(value);
        return data;
    }

    CommandData CommandData::Array()
    {
        CommandData data;
        data.m_kind = Kind::Array;
        return data;
    }

    CommandData CommandData::Object()
    {
        CommandData data;
        data.m_kind = Kind::Object;
        return data;
    }

    void CommandData::Append(CommandData value)
    {
        if (Kind::Array != m_kind)
        {
            // 이미 값을 들고 있었다면 그것을 첫 원소로 남긴다.
            //
            // 처음에는 그냥 배열로 바꾸고 기존 값을 버렸다. 호출자가 눈치채지
            // 못하는 데이터 손실이라 바꿨다 — Append 를 부른 쪽은 "더한다"를
            // 기대하지 스칼라가 사라지기를 기대하지 않는다. Null 은 담은 값이
            // 없으므로 그냥 빈 배열이 된다.
            CommandData previous = std::move(*this);
            m_kind = Kind::Array;
            m_bool = false; m_int = 0; m_double = 0.0;
            m_string.clear();
            m_array.clear();
            m_object.clear();
            if (Kind::Null != previous.m_kind) m_array.push_back(std::move(previous));
        }
        m_array.push_back(std::move(value));
    }

    void CommandData::Set(std::string key, CommandData value)
    {
        if (Kind::Object != m_kind)
        {
            m_kind = Kind::Object;
            m_object.clear();
        }

        const auto found = std::find_if(m_object.begin(), m_object.end(),
            [&key](const std::pair<std::string, CommandData>& field) { return field.first == key; });

        if (found != m_object.end()) { found->second = std::move(value); return; }
        m_object.emplace_back(std::move(key), std::move(value));
    }

    const CommandData* CommandData::Find(std::string_view key) const noexcept
    {
        for (const auto& field : m_object)
        {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }

    // ── 생성 헬퍼 ───────────────────────────────────────────────────────

    CommandResult Ok(std::string message, CommandData data)
    {
        CommandResult result;
        result.status  = CommandStatus::Succeeded;
        result.code    = "ok";
        result.message = std::move(message);
        result.data    = std::move(data);
        return result;
    }

    CommandResult Fail(std::string code, std::string message, CommandData data)
    {
        CommandResult result;
        result.status  = CommandStatus::Failed;
        result.code    = std::move(code);
        result.message = std::move(message);
        result.data    = std::move(data);
        return result;
    }

    CommandResult InvalidArguments(std::string message, std::string code)
    {
        CommandResult result;
        result.status  = CommandStatus::InvalidArguments;
        result.code    = std::move(code);
        result.message = std::move(message);
        return result;
    }

    CommandResult PreconditionFailed(std::string code, std::string message)
    {
        CommandResult result;
        result.status  = CommandStatus::PreconditionsFailed;
        result.code    = std::move(code);
        result.message = std::move(message);
        return result;
    }

    CommandResult InternalError(std::string code, std::string message)
    {
        CommandResult result;
        result.status  = CommandStatus::InternalError;
        result.code    = std::move(code);
        result.message = std::move(message);
        return result;
    }

}
