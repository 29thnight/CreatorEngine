#include "CommandResultJson.h"

#include "../../Engine/CommandService/ResultEnvelope.h"

namespace EditorCommandJson
{
    CommandService::JsonValue ToJson(const CommandCore::CommandData& data)
    {
        using CK = CommandCore::CommandData::Kind;
        using JV = CommandService::JsonValue;

        switch (data.GetKind())
        {
        case CK::Null:   return JV();
        case CK::Bool:   return JV::Bool(data.AsBool());
        case CK::Int:    return JV::Int(data.AsInt());
        case CK::Double: return JV::Double(data.AsDouble());
        case CK::String: return JV::String(data.AsString());
        case CK::Array:
        {
            JV array = JV::Array();
            for (const CommandCore::CommandData& item : data.Items()) array.Append(ToJson(item));
            return array;
        }
        case CK::Object:
        {
            JV object = JV::Object();
            for (const auto& field : data.Fields()) object.Set(field.first, ToJson(field.second));
            return object;
        }
        }
        return JV();
    }

    std::string DataJson(const CommandCore::CommandResult& result)
    {
        if (CommandCore::CommandData::Kind::Null == result.data.GetKind()) return "{}";
        return ToJson(result.data).Serialize();
    }

    std::string ResultLine(const std::string&                command,
                           const CommandCore::CommandResult& result,
                           double                            queuedMs,
                           uint32_t                          waitedFrames,
                           double                            executedMs)
    {
        // ★ `status` 문자열도 서비스와 같은 것을 쓴다(`CommandCore::ToString`).
        //   여기서 따로 사상하면 배치와 서비스가 같은 결과에 다른 이름을 붙이게
        //   되고, §18 의 "logical result 가 같다" 가 이름 단계에서 깨진다.
        return CommandService::BuildResultEnvelope(
            command,
            std::string(CommandCore::ToString(result.status)),
            result.code,
            result.message,
            DataJson(result),
            queuedMs, waitedFrames, executedMs).Serialize();
    }
}
