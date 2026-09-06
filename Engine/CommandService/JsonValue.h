#pragma once
// LC4 (PHASE 14.5) — 자체 JSON codec.
//
// ── 왜 자체 구현인가 (§4) ───────────────────────────────────────────────
//
// 스키마가 작고 고정이다. ryml 재사용은 저작 계측 경로
// (`AuthoringParsedDocument`)와 얽혀 Player 의 `runtime.text-parser calls=0`
// 게이트의 의미를 흐린다(§17). 두 role 이 같은 codec 을 쓰는 편이 계약이 하나다.
//
// ── 범위를 좁게 유지한다 ────────────────────────────────────────────────
//
// 쓰기는 서비스 응답이 쓰는 형태 전부, 읽기는 **요청 본문 하나**
// (`{"command":..., "args":[...], "correlationId":..., "timeoutMs":..., "mode":...}`)
// 만 다룬다. 일반 JSON 파서를 목표로 하지 않는다 — 목표가 되는 순간 이 파일이
// 검증되지 않은 파서가 되고, 그것이 §17 이 라이브러리를 기각하며 감수한 위험이다.
//
// 객체는 순서 있는 벡터다. 출력이 실행마다 같은 순서여야 소비자가 diff 로
// 비교할 수 있다(LC3 이 discovery 에서 같은 이유로 정렬을 못박았다).

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace CommandService
{
    class JsonValue
    {
    public:
        enum class Kind : uint8_t { Null, Bool, Int, Double, String, Array, Object };

        JsonValue() = default;

        static JsonValue Bool(bool value);
        static JsonValue Int(int64_t value);
        static JsonValue Double(double value);
        static JsonValue String(std::string value);
        static JsonValue Array();
        static JsonValue Object();

        Kind GetKind() const noexcept { return m_kind; }
        bool IsNull()   const noexcept { return Kind::Null == m_kind; }

        bool               AsBool()   const noexcept { return m_bool; }
        int64_t            AsInt()    const noexcept { return m_int; }
        double             AsDouble() const noexcept { return m_double; }
        const std::string& AsString() const noexcept { return m_string; }

        const std::vector<JsonValue>& Items() const noexcept { return m_array; }
        const std::vector<std::pair<std::string, JsonValue>>& Fields() const noexcept { return m_object; }

        void Append(JsonValue value);
        void Set(std::string key, JsonValue value);
        const JsonValue* Find(std::string_view key) const noexcept;

        /// 한 줄 직렬화. 사람이 읽는 들여쓰기는 하지 않는다 — 응답 본문이고,
        /// 사람이 읽어야 할 때는 `curl | jq` 가 있다.
        std::string Serialize() const;

    private:
        void SerializeInto(std::string& out) const;

        Kind        m_kind{ Kind::Null };
        bool        m_bool{};
        int64_t     m_int{};
        double      m_double{};
        std::string m_string;

        std::vector<JsonValue>                         m_array;
        std::vector<std::pair<std::string, JsonValue>> m_object;
    };

    struct JsonParseResult
    {
        JsonValue   value;
        bool        ok{ false };
        std::string error;
    };

    /// 요청 본문을 읽는다. 실패는 예외가 아니라 값으로 돌려준다 —
    /// 이 함수는 신뢰할 수 없는 입력을 받고, 던지면 수신 스레드가 죽는다.
    ///
    /// 중첩 깊이에 상한이 있다. 깊이 폭탄(`[[[[...`)은 재귀 파서를 스택으로
    /// 죽이는 가장 싼 공격이고, 본문 크기 제한만으로는 막히지 않는다.
    JsonParseResult ParseJson(std::string_view text, int maxDepth = 32);
}
