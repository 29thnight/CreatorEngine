#include "JsonValue.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace CommandService
{
    // ── 생성 ────────────────────────────────────────────────────────────

    JsonValue JsonValue::Bool(bool value)
    {
        JsonValue v; v.m_kind = Kind::Bool; v.m_bool = value; return v;
    }
    JsonValue JsonValue::Int(int64_t value)
    {
        JsonValue v; v.m_kind = Kind::Int; v.m_int = value; return v;
    }
    JsonValue JsonValue::Double(double value)
    {
        JsonValue v; v.m_kind = Kind::Double; v.m_double = value; return v;
    }
    JsonValue JsonValue::String(std::string value)
    {
        JsonValue v; v.m_kind = Kind::String; v.m_string = std::move(value); return v;
    }
    JsonValue JsonValue::Array()  { JsonValue v; v.m_kind = Kind::Array;  return v; }
    JsonValue JsonValue::Object() { JsonValue v; v.m_kind = Kind::Object; return v; }

    void JsonValue::Append(JsonValue value)
    {
        if (Kind::Array != m_kind) { m_kind = Kind::Array; m_array.clear(); }
        m_array.push_back(std::move(value));
    }

    void JsonValue::Set(std::string key, JsonValue value)
    {
        if (Kind::Object != m_kind) { m_kind = Kind::Object; m_object.clear(); }

        const auto found = std::find_if(m_object.begin(), m_object.end(),
            [&key](const std::pair<std::string, JsonValue>& field) { return field.first == key; });
        if (found != m_object.end()) { found->second = std::move(value); return; }
        m_object.emplace_back(std::move(key), std::move(value));
    }

    const JsonValue* JsonValue::Find(std::string_view key) const noexcept
    {
        for (const auto& field : m_object)
        {
            if (field.first == key) return &field.second;
        }
        return nullptr;
    }

    // ── 직렬화 ──────────────────────────────────────────────────────────

    namespace
    {
        void EscapeInto(std::string_view text, std::string& out)
        {
            out.push_back('"');
            for (const char c : text)
            {
                switch (c)
                {
                case '"':  out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                case '\b': out += "\\b";  break;
                case '\f': out += "\\f";  break;
                default:
                    // 제어 문자는 \u00XX 로. 그대로 두면 만들어진 문서가 JSON 이
                    // 아니고, 소비자의 파서가 그 지점에서 죽는다.
                    if (static_cast<unsigned char>(c) < 0x20)
                    {
                        char buffer[8] = {};
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                                      static_cast<unsigned>(static_cast<unsigned char>(c)));
                        out += buffer;
                    }
                    else
                    {
                        // UTF-8 바이트는 그대로 통과시킨다. JSON 은 UTF-8 이 기본이고,
                        // \u 로 접으면 한글이 읽을 수 없는 문서가 된다.
                        out.push_back(c);
                    }
                    break;
                }
            }
            out.push_back('"');
        }
    }

    void JsonValue::SerializeInto(std::string& out) const
    {
        switch (m_kind)
        {
        case Kind::Null:   out += "null"; break;
        case Kind::Bool:   out += m_bool ? "true" : "false"; break;
        case Kind::Int:    out += std::to_string(m_int); break;
        case Kind::Double:
        {
            // NaN/Inf 는 JSON 에 없다. null 로 낸다 — 문서를 깨뜨리는 것보다
            // "값이 없다"가 소비자에게 덜 나쁘다.
            if (!std::isfinite(m_double)) { out += "null"; break; }
            char buffer[32] = {};
            std::snprintf(buffer, sizeof(buffer), "%.6g", m_double);
            out += buffer;
            break;
        }
        case Kind::String: EscapeInto(m_string, out); break;
        case Kind::Array:
            out.push_back('[');
            for (std::size_t i = 0; i < m_array.size(); ++i)
            {
                if (0 != i) out.push_back(',');
                m_array[i].SerializeInto(out);
            }
            out.push_back(']');
            break;
        case Kind::Object:
            out.push_back('{');
            for (std::size_t i = 0; i < m_object.size(); ++i)
            {
                if (0 != i) out.push_back(',');
                EscapeInto(m_object[i].first, out);
                out.push_back(':');
                m_object[i].second.SerializeInto(out);
            }
            out.push_back('}');
            break;
        }
    }

    std::string JsonValue::Serialize() const
    {
        std::string out;
        SerializeInto(out);
        return out;
    }

    // ── 파싱 ────────────────────────────────────────────────────────────

    namespace
    {
        struct Parser
        {
            std::string_view text;
            std::size_t      at{ 0 };
            int              maxDepth{ 32 };
            std::string      error;

            void SkipSpace()
            {
                while (at < text.size())
                {
                    const char c = text[at];
                    if (' ' == c || '\t' == c || '\n' == c || '\r' == c) { ++at; continue; }
                    break;
                }
            }

            bool Fail(const char* why) { if (error.empty()) error = why; return false; }

            bool ParseValue(JsonValue& out, int depth)
            {
                if (depth > maxDepth) return Fail("중첩이 너무 깊다");
                SkipSpace();
                if (at >= text.size()) return Fail("본문이 끝났다");

                switch (text[at])
                {
                case '{': return ParseObject(out, depth);
                case '[': return ParseArray(out, depth);
                case '"':
                {
                    std::string s;
                    if (!ParseString(s)) return false;
                    out = JsonValue::String(std::move(s));
                    return true;
                }
                case 't':
                    if (text.substr(at, 4) != "true") return Fail("true 가 아니다");
                    at += 4; out = JsonValue::Bool(true); return true;
                case 'f':
                    if (text.substr(at, 5) != "false") return Fail("false 가 아니다");
                    at += 5; out = JsonValue::Bool(false); return true;
                case 'n':
                    if (text.substr(at, 4) != "null") return Fail("null 이 아니다");
                    at += 4; out = JsonValue(); return true;
                default:  return ParseNumber(out);
                }
            }

            bool ParseObject(JsonValue& out, int depth)
            {
                ++at;   // '{'
                out = JsonValue::Object();
                SkipSpace();
                if (at < text.size() && '}' == text[at]) { ++at; return true; }

                while (true)
                {
                    SkipSpace();
                    std::string key;
                    if (!ParseString(key)) return false;
                    SkipSpace();
                    if (at >= text.size() || ':' != text[at]) return Fail("':' 이 없다");
                    ++at;

                    JsonValue value;
                    if (!ParseValue(value, depth + 1)) return false;
                    out.Set(std::move(key), std::move(value));

                    SkipSpace();
                    if (at >= text.size()) return Fail("객체가 닫히지 않았다");
                    if (',' == text[at]) { ++at; continue; }
                    if ('}' == text[at]) { ++at; return true; }
                    return Fail("객체에 ',' 나 '}' 가 아닌 것이 왔다");
                }
            }

            bool ParseArray(JsonValue& out, int depth)
            {
                ++at;   // '['
                out = JsonValue::Array();
                SkipSpace();
                if (at < text.size() && ']' == text[at]) { ++at; return true; }

                while (true)
                {
                    JsonValue value;
                    if (!ParseValue(value, depth + 1)) return false;
                    out.Append(std::move(value));

                    SkipSpace();
                    if (at >= text.size()) return Fail("배열이 닫히지 않았다");
                    if (',' == text[at]) { ++at; continue; }
                    if (']' == text[at]) { ++at; return true; }
                    return Fail("배열에 ',' 나 ']' 가 아닌 것이 왔다");
                }
            }

            bool ParseString(std::string& out)
            {
                SkipSpace();
                if (at >= text.size() || '"' != text[at]) return Fail("문자열이 아니다");
                ++at;

                out.clear();
                while (at < text.size())
                {
                    const char c = text[at++];
                    if ('"' == c) return true;
                    if ('\\' != c) { out.push_back(c); continue; }

                    if (at >= text.size()) return Fail("escape 가 끊겼다");
                    const char e = text[at++];
                    switch (e)
                    {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'u':
                    {
                        // \uXXXX 를 UTF-8 로 편다. surrogate pair 는 다루지 않는다 —
                        // 이 codec 이 받는 것은 명령 이름과 인자이고, 거기에
                        // BMP 밖 문자가 오면 그냥 그대로(UTF-8 로) 보내면 된다.
                        if (at + 4 > text.size()) return Fail("\\u 가 끊겼다");
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i)
                        {
                            const char h = text[at + i];
                            code <<= 4;
                            if (h >= '0' && h <= '9')      code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else return Fail("\\u 뒤가 16진수가 아니다");
                        }
                        at += 4;
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        else if (code < 0x800)
                        {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        else
                        {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: return Fail("알 수 없는 escape");
                    }
                }
                return Fail("문자열이 닫히지 않았다");
            }

            bool ParseNumber(JsonValue& out)
            {
                const std::size_t start = at;
                if (at < text.size() && ('-' == text[at] || '+' == text[at])) ++at;

                bool isDouble = false;
                while (at < text.size())
                {
                    const char c = text[at];
                    if (c >= '0' && c <= '9') { ++at; continue; }
                    if ('.' == c || 'e' == c || 'E' == c || '-' == c || '+' == c)
                    {
                        isDouble = true; ++at; continue;
                    }
                    break;
                }
                if (at == start) return Fail("숫자가 아니다");

                const std::string token(text.substr(start, at - start));
                try
                {
                    if (isDouble) out = JsonValue::Double(std::stod(token));
                    else          out = JsonValue::Int(std::stoll(token));
                }
                catch (const std::exception&)
                {
                    // 범위를 넘는 값을 조용히 0 으로 만들지 않는다(§14.2).
                    return Fail("숫자가 범위를 벗어났다");
                }
                return true;
            }
        };
    }

    JsonParseResult ParseJson(std::string_view text, int maxDepth)
    {
        JsonParseResult result;

        Parser parser;
        parser.text     = text;
        parser.maxDepth = maxDepth;

        if (!parser.ParseValue(result.value, 0))
        {
            result.error = parser.error.empty() ? "JSON 파싱 실패" : parser.error;
            return result;
        }

        parser.SkipSpace();
        if (parser.at != text.size())
        {
            result.error = "본문 뒤에 남는 문자가 있다";
            return result;
        }

        result.ok = true;
        return result;
    }
}
