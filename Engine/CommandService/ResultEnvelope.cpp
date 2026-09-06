#include "ResultEnvelope.h"

namespace CommandService
{
    JsonValue BuildResultEnvelope(const std::string& command,
                                  const std::string& status,
                                  const std::string& code,
                                  const std::string& message,
                                  const std::string& dataJson,
                                  double             queuedMs,
                                  uint32_t           waitedFrames,
                                  double             executedMs,
                                  const std::string& correlationId)
    {
        JsonValue root = JsonValue::Object();
        root.Set("schemaVersion", JsonValue::Int(1));
        if (!correlationId.empty()) root.Set("correlationId", JsonValue::String(correlationId));
        root.Set("command", JsonValue::String(command));
        root.Set("status",  JsonValue::String(status));
        root.Set("code",    JsonValue::String(code));
        root.Set("message", JsonValue::String(message));

        // ★ `data` 는 항상 객체다(§5.2). 값이 없으면 `null` 이 아니라 `{}` 다 —
        //   소비자가 `data.frames` 를 읽기 전에 형을 확인하지 않아도 되게 한다.
        //
        //   파싱이 실패하면 **지어내지 않고 빈 객체**를 넣는다. 어댑터가 잘못
        //   직렬화한 것을 여기서 고쳐 주면 그 결함이 영영 안 보인다.
        const JsonParseResult data = ParseJson(dataJson.empty() ? "{}" : dataJson);
        root.Set("data", data.ok ? data.value : JsonValue::Object());

        // timing 은 장식이 아니라 지연 계약의 증거다(§5.2). 배치에도 같이 실린다 —
        // "명령 N 개가 N 프레임을 기다리지 않는다"(§18)를 배치 쪽에서 확인하려면
        // 배치 결과에도 `waitedFrames` 가 있어야 한다.
        JsonValue timing = JsonValue::Object();
        timing.Set("queuedMs",     JsonValue::Double(queuedMs));
        timing.Set("waitedFrames", JsonValue::Int(static_cast<int64_t>(waitedFrames)));
        timing.Set("executedMs",   JsonValue::Double(executedMs));
        root.Set("timing", std::move(timing));

        return root;
    }
}
