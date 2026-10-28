#include "../../Containers/ContainerAPI.h"
#include "DiagnosticDocument.h"
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace Diagnostics
{
    namespace
    {
        enum class Kind { Null, Map, Sequence, String, Literal };

        struct Value
        {
            Kind kind = Kind::Null;
            ce::utf8_string text;
            ce::dynamic_array<ce::pair<ce::utf8_string, std::size_t>> children;
        };

        void Quote(ce::utf8_string& output, ce::utf8_string_view text)
        {
            constexpr char hex[] = "0123456789abcdef";
            output.push_back('"');
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                const auto c = static_cast<unsigned char>(text[index]);
                switch (c)
                {
                case '"': output.append("\\\""); break;
                case '\\': output.append("\\\\"); break;
                case '\b': output.append("\\b"); break;
                case '\f': output.append("\\f"); break;
                case '\n': output.append("\\n"); break;
                case '\r': output.append("\\r"); break;
                case '\t': output.append("\\t"); break;
                default:
                    if (c < 0x20)
                    {
                        output.append("\\u00");
                        output.push_back(hex[c >> 4]);
                        output.push_back(hex[c & 15]);
                    }
                    else output.push_back(static_cast<char>(c));
                }
            }
            output.push_back('"');
        }

        template<class T> ce::utf8_string Numeric(T value)
        {
            char buffer[128];
            const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (error != std::errc{}) throw std::runtime_error("Diagnostic number conversion failed");
            return ce::utf8_string(ce::utf8_string_view(buffer, static_cast<std::size_t>(end - buffer)));
        }
    }

    // Writer-only owned tree. Node IDs survive growth and Document moves.
    // Diagnostics never initializes the authoring parser or its error policy.
    struct DocumentState
    {
        ce::dynamic_array<Value> values{1};

        Value& At(std::size_t id)
        {
            if (id >= values.size()) throw std::logic_error("Invalid diagnostic document node");
            return values[id];
        }

        void Emit(std::size_t id, ce::utf8_string& output, std::size_t depth = 0) const
        {
            if (depth > 512) throw std::length_error("Diagnostic document depth exceeded");
            const auto& value = values.at(id);
            if (value.kind == Kind::Null) { output.append("null"); return; }
            if (value.kind == Kind::String) { Quote(output, value.text); return; }
            if (value.kind == Kind::Literal) { output.append(value.text); return; }
            const bool map = value.kind == Kind::Map;
            output.push_back(map ? '{' : '[');
            bool first = true;
            for (const auto& [key, child] : value.children)
            {
                if (!first) output.push_back(',');
                first = false;
                if (map) { Quote(output, key); output.push_back(':'); }
                Emit(child, output, depth + 1);
            }
            output.push_back(map ? '}' : ']');
        }
    };

    namespace
    {
        Value& Ref(DocumentState* state, std::size_t id)
        {
            if (!state) throw std::logic_error("Invalid diagnostic document node");
            return state->At(id);
        }

        void Container(Value& value, Kind kind)
        {
            if (value.kind == Kind::Null) value.kind = kind;
            if (value.kind != kind) throw std::logic_error("Diagnostic node kind mismatch");
        }

        void Scalar(Value& value, Kind kind, ce::utf8_string text)
        {
            if (value.kind == Kind::Map || value.kind == Kind::Sequence)
                throw std::logic_error("Cannot replace diagnostic container with scalar");
            value.kind = kind;
            value.text = std::move(text);
        }
    }

    Document::Document() : state_(std::make_unique<DocumentState>()) {}
    Document::~Document() = default;
    Document::Document(Document&&) noexcept = default;
    Document& Document::operator=(Document&&) noexcept = default;

    Node Document::Root()
    {
        if (!state_) throw std::logic_error("Moved diagnostic document");
        return Node(state_.get(), 0);
    }

    ce::utf8_string Document::ToJson() const
    {
        if (!state_) throw std::logic_error("Moved diagnostic document");
        ce::utf8_string result;
        state_->Emit(0, result);
        return result;
    }

    Node Node::operator[](ce::utf8_string_view key) const
    {
        auto& parent = Ref(state_, id_);
        Container(parent, Kind::Map);
        for (const auto& [name, child] : parent.children)
            if (name == key) return Node(state_, child);
        const auto child = state_->values.size();
        parent.children.emplace_back(key, child);
        state_->values.emplace_back();
        return Node(state_, child);
    }

    Node Node::operator[](std::size_t index) const
    {
        const auto& parent = Ref(state_, id_);
        if (parent.kind != Kind::Sequence || index >= parent.children.size())
            throw std::out_of_range("Diagnostic sequence index");
        return Node(state_, parent.children[index].second);
    }

    Node Node::Append() const
    {
        auto& parent = Ref(state_, id_);
        Container(parent, Kind::Sequence);
        const auto child = state_->values.size();
        parent.children.emplace_back(ce::utf8_string{}, child);
        state_->values.emplace_back();
        return Node(state_, child);
    }

    bool Node::Has(ce::utf8_string_view key) const
    {
        const auto& parent = Ref(state_, id_);
        for (const auto& child : parent.children)
            if (child.first == key && parent.kind == Kind::Map) return true;
        return false;
    }

    void Node::SetMap() const { Container(Ref(state_, id_), Kind::Map); }
    void Node::SetSequence() const { Container(Ref(state_, id_), Kind::Sequence); }
    void Node::operator<<(ce::utf8_string_view value) const { Scalar(Ref(state_, id_), Kind::String, ce::utf8_string(value)); }
    void Node::operator<<(bool value) const { Scalar(Ref(state_, id_), Kind::Literal, ce::utf8_string(value ? "true" : "false")); }
    void Node::SetSigned(std::int64_t value) const { Scalar(Ref(state_, id_), Kind::Literal, Numeric(value)); }
    void Node::SetUnsigned(std::uint64_t value) const { Scalar(Ref(state_, id_), Kind::Literal, Numeric(value)); }
    void Node::SetFloat(double value) const
    {
        if (!std::isfinite(value)) throw std::invalid_argument("Nonfinite diagnostic number");
        Scalar(Ref(state_, id_), Kind::Literal, Numeric(value));
    }
}
