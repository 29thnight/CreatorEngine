#pragma once
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

// Borrowed diagnostic nodes never outlive their document. Backend state remains
// stable across Document moves; no parser/backend type crosses this interface.
namespace Diagnostics
{
    struct DocumentState;
    class Document;
    class Node
    {
    public:
        Node() = default;
        Node operator[](std::string_view key) const;
        Node operator[](std::size_t index) const;
        Node Append() const;
        bool Has(std::string_view key) const;
        void SetMap() const;
        void SetSequence() const;
        void operator<<(std::string_view value) const;
        void operator<<(const std::string& value) const { *this << std::string_view(value); }
        void operator<<(const char* value) const { *this << std::string_view(value ? value : ""); }
        void operator<<(bool value) const;
        template<std::integral T> void operator<<(T value) const
        {
            if constexpr (std::signed_integral<T>) SetSigned(static_cast<std::int64_t>(value));
            else SetUnsigned(static_cast<std::uint64_t>(value));
        }
        template<std::floating_point T> void operator<<(T value) const {SetFloat(static_cast<double>(value));}
    private:
        friend class Document;
        Node(DocumentState* state, std::size_t id) : state_(state), id_(id) {}
        void SetSigned(std::int64_t value) const;
        void SetUnsigned(std::uint64_t value) const;
        void SetFloat(double value) const;
        DocumentState* state_{};
        std::size_t id_{};
    };
    class Document
    {
    public:
        Document();
        ~Document();
        Document(Document&&) noexcept;
        Document& operator=(Document&&) noexcept;
        Document(const Document&)=delete;
        Document& operator=(const Document&)=delete;
        Node Root();
        std::string ToJson() const;
    private:
        std::unique_ptr<DocumentState> state_;
    };
}
