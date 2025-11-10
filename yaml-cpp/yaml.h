#pragma once

#ifndef YAML_CPP_API
#define YAML_CPP_API
#endif

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

namespace YAML
{

namespace detail
{

template<typename T>
struct is_vector : std::false_type {};

template<typename T, typename Alloc>
struct is_vector<std::vector<T, Alloc>> : std::true_type {};

template<typename T>
inline constexpr bool is_vector_v = is_vector<T>::value;

template<typename T>
inline constexpr bool always_false_v = false;

} // namespace detail

enum class EmitterStyle
{
        Flow,
        Block,
};

class Node
{
public:
        enum class Type
        {
                Null,
                Scalar,
                Map,
                Sequence,
        };

        Node();
        Node(const Node&) = default;
        Node(Node&&) noexcept = default;
        Node& operator=(const Node&) = default;
        Node& operator=(Node&&) noexcept = default;
        ~Node() = default;

        bool IsNull() const { return m_data->type == Type::Null; }
        bool IsScalar() const { return m_data->type == Type::Scalar; }
        bool IsMap() const { return m_data->type == Type::Map; }
        bool IsSequence() const { return m_data->type == Type::Sequence; }
        bool IsDefined() const { return !IsNull(); }

        explicit operator bool() const { return !IsNull(); }

        void SetStyle(EmitterStyle style) { m_data->style = style; }
        EmitterStyle GetStyle() const { return m_data->style; }

        std::size_t size() const;

        Node& operator[](std::string_view key);
        Node operator[](std::string_view key) const;

        Node& operator[](std::size_t index);
        Node operator[](std::size_t index) const;

        Node& operator=(const char* value);
        Node& operator=(const std::string& value);
        Node& operator=(std::string_view value);

        template<typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        Node& operator=(T value)
        {
                set_scalar(to_string(value));
                return *this;
        }

        void push_back(const Node& node);

        template<typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        void push_back(T value)
        {
                Node temp;
                temp = value;
                push_back(temp);
        }

        void push_back(const std::string& value)
        {
                Node temp;
                temp = value;
                push_back(temp);
        }

        void push_back(std::string_view value)
        {
                Node temp;
                temp = value;
                push_back(temp);
        }

        template<typename T>
        T as() const
        {
                if constexpr (std::is_same_v<T, std::string>)
                {
                        if (!IsScalar())
                                return std::string{};
                        return m_data->scalar;
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                        if (!IsScalar())
                                return false;

                        if (m_data->scalar == "true" || m_data->scalar == "True" || m_data->scalar == "TRUE")
                                return true;
                        if (m_data->scalar == "false" || m_data->scalar == "False" || m_data->scalar == "FALSE")
                                return false;

                        auto trimmed = trim(m_data->scalar);
                        if (trimmed.empty())
                                return false;

                        return trimmed != "0";
                }
                else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
                {
                        if (!IsScalar())
                                return T{};
                        long long value = 0;
                        parse_integer(m_data->scalar, value);
                        return static_cast<T>(value);
                }
                else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>)
                {
                        if (!IsScalar())
                                return T{};
                        unsigned long long value = 0;
                        parse_unsigned(m_data->scalar, value);
                        return static_cast<T>(value);
                }
                else if constexpr (std::is_floating_point_v<T>)
                {
                        if (!IsScalar())
                                return T{};
                        long double value = 0.0;
                        parse_floating(m_data->scalar, value);
                        return static_cast<T>(value);
                }
                else if constexpr (detail::is_vector_v<T>)
                {
                        using Elem = typename T::value_type;
                        T result;
                        if (!IsSequence())
                                return result;
                        result.reserve(m_data->sequence.size());
                        for (const auto& child : m_data->sequence)
                        {
                                result.push_back(child.template as<Elem>());
                        }
                        return result;
                }
                else
                {
                        static_assert(detail::always_false_v<T>, "Unsupported YAML::Node conversion");
                }
        }

        std::string Scalar() const { return m_data->scalar; }

        class const_iterator
        {
        public:
                struct Key
                {
                        std::string value;
                        std::string Scalar() const { return value; }
                };

                struct value_type
                {
                        Key first;
                        Node second;

                        bool IsNull() const { return second.IsNull(); }
                        bool IsScalar() const { return second.IsScalar(); }
                        bool IsMap() const { return second.IsMap(); }
                        bool IsSequence() const { return second.IsSequence(); }
                        std::size_t size() const { return second.size(); }

                        template<typename T>
                        T as() const
                        {
                                return second.template as<T>();
                        }

                        Node operator[](std::string_view key) const { return second[key]; }
                        Node operator[](std::size_t index) const { return second[index]; }
                        std::string Scalar() const { return second.Scalar(); }

                        operator Node() const { return second; }
                };

                using difference_type = std::ptrdiff_t;
                using iterator_category = std::forward_iterator_tag;

                const_iterator() = default;

                static const_iterator MakeMapIterator(const Impl* owner,
                        std::map<std::string, Node>::const_iterator it,
                        std::map<std::string, Node>::const_iterator end)
                {
                        const_iterator iter;
                        iter.m_owner = owner;
                        iter.m_isSequence = false;
                        iter.m_mapIt = it;
                        iter.m_mapEnd = end;
                        return iter;
                }

                static const_iterator MakeSequenceIterator(const Impl* owner,
                        std::vector<Node>::const_iterator it,
                        std::vector<Node>::const_iterator end)
                {
                        const_iterator iter;
                        iter.m_owner = owner;
                        iter.m_isSequence = true;
                        iter.m_seqIt = it;
                        iter.m_seqEnd = end;
                        return iter;
                }

                value_type operator*() const
                {
                        if (!m_owner)
                                return {};

                        if (m_isSequence)
                        {
                                value_type value;
                                value.second = (m_seqIt != m_seqEnd) ? *m_seqIt : Node();
                                return value;
                        }

                        return value_type{ Key{ m_mapIt->first }, m_mapIt->second };
                }

                const_iterator& operator++()
                {
                        if (!m_owner)
                                return *this;

                        if (m_isSequence)
                        {
                                if (m_seqIt != m_seqEnd)
                                        ++m_seqIt;
                        }
                        else
                        {
                                if (m_mapIt != m_mapEnd)
                                        ++m_mapIt;
                        }
                        return *this;
                }

                bool operator==(const const_iterator& other) const
                {
                        if (m_owner != other.m_owner)
                                return false;
                        if (m_isSequence != other.m_isSequence)
                                return false;

                        if (!m_owner)
                                return true;

                        if (m_isSequence)
                                return m_seqIt == other.m_seqIt;

                        return m_mapIt == other.m_mapIt;
                }

                bool operator!=(const const_iterator& other) const
                {
                        return !(*this == other);
                }

        private:
                const Impl* m_owner{ nullptr };
                bool m_isSequence{ false };
                std::map<std::string, Node>::const_iterator m_mapIt{};
                std::map<std::string, Node>::const_iterator m_mapEnd{};
                std::vector<Node>::const_iterator m_seqIt{};
                std::vector<Node>::const_iterator m_seqEnd{};
        };

        const_iterator begin() const;
        const_iterator end() const;

        Type GetType() const { return m_data->type; }

private:
        struct Impl
        {
                Type type{ Type::Null };
                std::string scalar{};
                std::map<std::string, Node> map{};
                std::vector<Node> sequence{};
                EmitterStyle style{ EmitterStyle::Block };
        };

        std::shared_ptr<Impl> m_data;

        void ensure_map();
        void ensure_sequence();
        void ensure_scalar();
        void set_scalar(std::string value);

        static std::string to_string(long long value)
        {
                return std::to_string(value);
        }

        static std::string to_string(unsigned long long value)
        {
                return std::to_string(value);
        }

        static std::string to_string(long double value)
        {
                std::ostringstream oss;
                oss << value;
                return oss.str();
        }

        template<typename T>
        static std::string to_string(T value)
        {
                if constexpr (std::is_same_v<T, bool>)
                {
                        return value ? "true" : "false";
                }
                else if constexpr (std::is_floating_point_v<T>)
                {
                        std::ostringstream oss;
                        oss << value;
                        return oss.str();
                }
                else if constexpr (std::is_integral_v<T>)
                {
                        if constexpr (std::is_signed_v<T>)
                                return std::to_string(static_cast<long long>(value));
                        else
                                return std::to_string(static_cast<unsigned long long>(value));
                }
                else
                {
                        std::ostringstream oss;
                        oss << value;
                        return oss.str();
                }
        }

        static std::string trim(std::string_view value)
        {
                        auto begin = value.find_first_not_of(" \t\r\n");
                        if (begin == std::string_view::npos)
                                return {};
                        auto end = value.find_last_not_of(" \t\r\n");
                        return std::string(value.substr(begin, end - begin + 1));
        }

        static void parse_integer(const std::string& input, long long& out)
        {
                std::istringstream iss(input);
                iss >> out;
                if (!iss)
                        throw std::runtime_error("Invalid integer scalar: " + input);
        }

        static void parse_unsigned(const std::string& input, unsigned long long& out)
        {
                std::istringstream iss(input);
                iss >> out;
                if (!iss)
                        throw std::runtime_error("Invalid unsigned scalar: " + input);
        }

        static void parse_floating(const std::string& input, long double& out)
        {
                std::istringstream iss(input);
                iss >> out;
                if (!iss)
                        throw std::runtime_error("Invalid floating scalar: " + input);
        }

        friend Node detail::from_ryml(const ryml::ConstNodeRef& ref);
        friend void detail::to_ryml(const Node& node, ryml::NodeRef ref);
};

namespace detail
{

inline Node from_ryml(const ryml::ConstNodeRef& ref)
{
        Node node;

        if (ref.is_map())
        {
                node.ensure_map();
                if (ref.style() == ryml::FLOW_STYLE)
                        node.SetStyle(EmitterStyle::Flow);
                for (auto child : ref.children())
                {
                        std::string key = child.key().str();
                        node[key] = from_ryml(child);
                }
        }
        else if (ref.is_seq())
        {
                node.ensure_sequence();
                if (ref.style() == ryml::FLOW_STYLE)
                        node.SetStyle(EmitterStyle::Flow);
                for (auto child : ref.children())
                {
                        node.push_back(from_ryml(child));
                }
        }
        else if (ref.has_val())
        {
                node.ensure_scalar();
                node.m_data->scalar = ref.val().str();
        }
        else
        {
                node.m_data->type = Node::Type::Null;
        }

        return node;
}

inline void to_ryml(const Node& node, ryml::NodeRef ref)
{
        switch (node.GetType())
        {
        case Node::Type::Null:
                ref |= ryml::VAL;
                ref << ryml::Null{};
                break;
        case Node::Type::Scalar:
                ref |= ryml::VAL;
                ref << node.m_data->scalar;
                break;
        case Node::Type::Map:
                ref |= ryml::MAP;
                if (node.GetStyle() == EmitterStyle::Flow)
                        ref.set_style(ryml::FLOW_STYLE);
                for (const auto& kv : node.m_data->map)
                {
                        ryml::NodeRef child = ref.append_child();
                        child.set_key(ryml::to_csubstr(kv.first));
                        to_ryml(kv.second, child);
                }
                break;
        case Node::Type::Sequence:
                ref |= ryml::SEQ;
                if (node.GetStyle() == EmitterStyle::Flow)
                        ref.set_style(ryml::FLOW_STYLE);
                for (const auto& childNode : node.m_data->sequence)
                {
                        ryml::NodeRef child = ref.append_child();
                        to_ryml(childNode, child);
                }
                break;
        }
}

} // namespace detail

inline Node::Node()
        : m_data(std::make_shared<Impl>())
{
}

inline std::size_t Node::size() const
{
        if (IsMap())
                return m_data->map.size();
        if (IsSequence())
                return m_data->sequence.size();
        return 0;
}

inline void Node::ensure_map()
{
        if (m_data->type != Type::Map)
        {
                m_data->type = Type::Map;
                m_data->map.clear();
                m_data->sequence.clear();
                m_data->scalar.clear();
        }
}

inline void Node::ensure_sequence()
{
        if (m_data->type != Type::Sequence)
        {
                m_data->type = Type::Sequence;
                m_data->sequence.clear();
                m_data->map.clear();
                m_data->scalar.clear();
        }
}

inline void Node::ensure_scalar()
{
        if (m_data->type != Type::Scalar)
        {
                m_data->type = Type::Scalar;
                m_data->scalar.clear();
                m_data->map.clear();
                m_data->sequence.clear();
        }
}

inline void Node::set_scalar(std::string value)
{
        ensure_scalar();
        m_data->scalar = std::move(value);
}

inline Node& Node::operator[](std::string_view key)
{
        ensure_map();
        auto& child = m_data->map[std::string(key)];
        return child;
}

inline Node Node::operator[](std::string_view key) const
{
        if (!IsMap())
                return Node();
        auto it = m_data->map.find(std::string(key));
        if (it == m_data->map.end())
                return Node();
        return it->second;
}

inline Node& Node::operator[](std::size_t index)
{
        ensure_sequence();
        if (index >= m_data->sequence.size())
                m_data->sequence.resize(index + 1);
        return m_data->sequence[index];
}

inline Node Node::operator[](std::size_t index) const
{
        if (!IsSequence() || index >= m_data->sequence.size())
                return Node();
        return m_data->sequence[index];
}

inline Node& Node::operator=(const char* value)
{
        set_scalar(value ? std::string(value) : std::string{});
        return *this;
}

inline Node& Node::operator=(const std::string& value)
{
        set_scalar(value);
        return *this;
}

inline Node& Node::operator=(std::string_view value)
{
        set_scalar(std::string(value));
        return *this;
}

inline void Node::push_back(const Node& node)
{
        ensure_sequence();
        m_data->sequence.push_back(node);
}

inline Node::const_iterator Node::begin() const
{
        if (IsMap())
                return const_iterator::MakeMapIterator(m_data.get(), m_data->map.begin(), m_data->map.end());
        if (IsSequence())
                return const_iterator::MakeSequenceIterator(m_data.get(), m_data->sequence.begin(), m_data->sequence.end());
        return const_iterator{};
}

inline Node::const_iterator Node::end() const
{
        if (IsMap())
                return const_iterator::MakeMapIterator(m_data.get(), m_data->map.end(), m_data->map.end());
        if (IsSequence())
                return const_iterator::MakeSequenceIterator(m_data.get(), m_data->sequence.end(), m_data->sequence.end());
        return const_iterator{};
}

inline Node Load(const std::string& contents)
{
        ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(contents));
        return detail::from_ryml(tree.crootref());
}

inline Node LoadFile(const std::string& path)
{
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
                throw std::runtime_error("Failed to open YAML file: " + path);
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        return Load(buffer.str());
}

inline std::string Dump(const Node& node)
{
        ryml::Tree tree;
        detail::to_ryml(node, tree.rootref());
        return ryml::emitrs_yaml<std::string>(tree);
}

} // namespace YAML

