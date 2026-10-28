#pragma once
#include "../../Containers/ContainerAPI.h"
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
        Node operator[](ce::utf8_string_view key) const;
        Node operator[](std::string_view key) const { return (*this)[ce::utf8_string_view(key.data(), key.size())]; }
        template<class T> requires std::convertible_to<T, const char*>
        Node operator[](T&& key) const
        {
            const char* text = key;
            return (*this)[ce::utf8_string_view(text ? text : "")];
        }
        Node operator[](std::size_t index) const;
        Node Append() const;
        bool Has(ce::utf8_string_view key) const;
        bool Has(std::string_view key) const { return Has(ce::utf8_string_view(key.data(), key.size())); }
        bool Has(const char* key) const { return Has(ce::utf8_string_view(key ? key : "")); }
        void SetMap() const;
        void SetSequence() const;
        void operator<<(ce::utf8_string_view value) const;
        void operator<<(std::string_view value) const { *this << ce::utf8_string_view(value.data(), value.size()); }
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
        ce::utf8_string ToJson() const;
    private:
        std::unique_ptr<DocumentState> state_;
    };
}
