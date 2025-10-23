#pragma once

#include <charconv>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include <ryml.hpp>
#include <ryml_std.hpp>

namespace MetaYml
{

enum class EmitterStyle
{
    Default,
    Flow,
};

namespace detail
{
    template<typename T>
    struct is_vector : std::false_type
    {
    };

    template<typename T, typename Alloc>
    struct is_vector<std::vector<T, Alloc>> : std::true_type
    {
    };

    template<typename T>
    inline constexpr bool is_vector_v = is_vector<T>::value;

    inline std::vector<class Node>& empty_sequence()
    {
        static std::vector<class Node> seq;
        return seq;
    }

    inline const std::vector<class Node>& empty_sequence_const()
    {
        return empty_sequence();
    }

    inline const std::vector<std::pair<std::string, class Node>>& empty_map()
    {
        static const std::vector<std::pair<std::string, class Node>> map;
        return map;
    }
}

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

    static Node Invalid();

    [[nodiscard]] bool IsDefined() const { return m_defined; }
    [[nodiscard]] bool IsNull() const;
    [[nodiscard]] bool IsScalar() const;
    [[nodiscard]] bool IsMap() const;
    [[nodiscard]] bool IsSequence() const;

    explicit operator bool() const { return m_defined; }

    Node operator[](std::string_view key);
    Node operator[](std::string_view key) const;

    Node operator[](std::size_t index);
    Node operator[](std::size_t index) const;

    template<typename T>
    Node& operator=(const T& value)
    {
        assign_scalar(value);
        return *this;
    }

    Node& operator=(std::string_view value)
    {
        assign_scalar(value);
        return *this;
    }

    void push_back(const Node& value);

    template<typename T>
    void push_back(const T& value)
    {
        Node child;
        child = value;
        push_back(child);
    }

    [[nodiscard]] std::size_t size() const;

    void clear();

    using sequence_iterator = std::vector<Node>::iterator;
    using const_sequence_iterator = std::vector<Node>::const_iterator;

    sequence_iterator begin();
    sequence_iterator end();
    const_sequence_iterator begin() const;
    const_sequence_iterator end() const;
    const_sequence_iterator cbegin() const;
    const_sequence_iterator cend() const;

    void SetStyle(EmitterStyle style);
    [[nodiscard]] EmitterStyle GetStyle() const;

    [[nodiscard]] std::string Scalar() const;

    template<typename T>
    T as() const
    {
        if constexpr (detail::is_vector_v<T>)
        {
            using Elem = typename T::value_type;
            T result;
            if (!IsSequence())
                return result;
            for (const auto& child : SequenceItems())
            {
                result.push_back(child.template as<Elem>());
            }
            return result;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            if (!m_defined || !m_impl || m_impl->type != Type::Scalar)
                return std::string{};
            return m_impl->scalar;
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            auto text = as<std::string>();
            if (text == "true" || text == "True" || text == "TRUE")
                return true;
            if (text == "false" || text == "False" || text == "FALSE")
                return false;
            int value{};
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec == std::errc{})
                return value != 0;
            return !text.empty();
        }
        else if constexpr (std::is_integral_v<T>)
        {
            auto text = as<std::string>();
            T value{};
            auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (ec != std::errc{})
            {
                long long fallback = 0;
                try
                {
                    fallback = std::stoll(text);
                }
                catch (...)
                {
                    fallback = 0;
                }
                value = static_cast<T>(fallback);
            }
            return value;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            auto text = as<std::string>();
            double value = 0.0;
            try
            {
                value = std::stod(text);
            }
            catch (...)
            {
                value = 0.0;
            }
            return static_cast<T>(value);
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for MetaYml::Node::as()");
        }
    }

    template<typename T>
    T as(const T& defaultValue) const
    {
        if (!m_defined)
            return defaultValue;
        if constexpr (detail::is_vector_v<T>)
        {
            if (!IsSequence())
                return defaultValue;
        }
        else
        {
            if (!IsScalar())
                return defaultValue;
        }
        try
        {
            return as<T>();
        }
        catch (...)
        {
            return defaultValue;
        }
    }

    std::string as(const char* defaultValue) const
    {
        return as<std::string>(defaultValue ? std::string(defaultValue) : std::string{});
    }

    std::string as(std::string_view defaultValue) const
    {
        return as<std::string>(std::string(defaultValue));
    }

    const std::vector<std::pair<std::string, Node>>& MapItems() const;
    std::vector<std::pair<std::string, Node>>& MapItems();

    const std::vector<Node>& SequenceItems() const;
    std::vector<Node>& SequenceItems();

    [[nodiscard]] Type GetType() const;

private:
    struct Impl
    {
        Type type{ Type::Null };
        std::string scalar{};
        std::vector<std::pair<std::string, Node>> map{};
        std::vector<Node> sequence{};
        EmitterStyle style{ EmitterStyle::Default };
    };

    explicit Node(std::shared_ptr<Impl> impl, bool defined);

    void ensure_impl();
    void ensure_type(Type type);
    void ensure_map();
    void ensure_sequence();
    void ensure_scalar();

    template<typename T>
    void assign_scalar(const T& value)
    {
        ensure_scalar();
        if constexpr (std::is_same_v<T, std::string>)
        {
            m_impl->scalar = value;
        }
        else if constexpr (std::is_same_v<T, const char*>)
        {
            m_impl->scalar = value ? value : "";
        }
        else if constexpr (std::is_same_v<T, std::string_view>)
        {
            m_impl->scalar.assign(value.begin(), value.end());
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            m_impl->scalar = value ? "true" : "false";
        }
        else if constexpr (std::is_integral_v<T>)
        {
            char buffer[64]{};
            auto [ptr, ec] = std::to_chars(std::begin(buffer), std::end(buffer), value);
            if (ec == std::errc{})
            {
                m_impl->scalar.assign(buffer, ptr);
            }
            else
            {
                m_impl->scalar = std::to_string(static_cast<long long>(value));
            }
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            m_impl->scalar = std::to_string(value);
        }
        else
        {
            static_assert(!sizeof(T), "Unsupported type for MetaYml::Node assignment");
        }
    }

    std::shared_ptr<Impl> m_impl;
    bool m_defined{ true };
};

inline Node::Node()
    : m_impl(std::make_shared<Impl>()), m_defined(true)
{
}

inline Node::Node(std::shared_ptr<Impl> impl, bool defined)
    : m_impl(std::move(impl)), m_defined(defined)
{
}

inline Node Node::Invalid()
{
    return Node(nullptr, false);
}

inline bool Node::IsNull() const
{
    return !m_defined || !m_impl || m_impl->type == Type::Null;
}

inline bool Node::IsScalar() const
{
    return m_defined && m_impl && m_impl->type == Type::Scalar;
}

inline bool Node::IsMap() const
{
    return m_defined && m_impl && m_impl->type == Type::Map;
}

inline bool Node::IsSequence() const
{
    return m_defined && m_impl && m_impl->type == Type::Sequence;
}

inline void Node::ensure_impl()
{
    if (!m_impl)
    {
        m_impl = std::make_shared<Impl>();
    }
}

inline void Node::ensure_type(Type type)
{
    ensure_impl();
    if (m_impl->type != type)
    {
        m_impl->type = type;
        switch (type)
        {
        case Type::Scalar:
            m_impl->map.clear();
            m_impl->sequence.clear();
            break;
        case Type::Map:
            m_impl->scalar.clear();
            m_impl->sequence.clear();
            break;
        case Type::Sequence:
            m_impl->scalar.clear();
            m_impl->map.clear();
            break;
        case Type::Null:
            m_impl->scalar.clear();
            m_impl->map.clear();
            m_impl->sequence.clear();
            break;
        }
    }
    m_defined = true;
}

inline void Node::ensure_map()
{
    ensure_type(Type::Map);
}

inline void Node::ensure_sequence()
{
    ensure_type(Type::Sequence);
}

inline void Node::ensure_scalar()
{
    ensure_type(Type::Scalar);
}

inline Node Node::operator[](std::string_view key)
{
    ensure_map();
    for (auto& [existingKey, child] : m_impl->map)
    {
        if (existingKey == key)
        {
            return child;
        }
    }

    Node child;
    m_impl->map.emplace_back(std::string(key), child);
    return m_impl->map.back().second;
}

inline Node Node::operator[](std::string_view key) const
{
    if (!IsMap())
        return Node::Invalid();

    for (const auto& [existingKey, child] : m_impl->map)
    {
        if (existingKey == key)
        {
            return child;
        }
    }

    return Node::Invalid();
}

inline Node Node::operator[](std::size_t index)
{
    ensure_sequence();
    if (index >= m_impl->sequence.size())
    {
        m_impl->sequence.resize(index + 1);
    }
    return m_impl->sequence[index];
}

inline Node Node::operator[](std::size_t index) const
{
    if (!IsSequence() || index >= m_impl->sequence.size())
        return Node::Invalid();
    return m_impl->sequence[index];
}

inline void Node::push_back(const Node& value)
{
    ensure_sequence();
    m_impl->sequence.push_back(value);
}

inline std::size_t Node::size() const
{
    if (!m_defined || !m_impl)
        return 0;
    switch (m_impl->type)
    {
    case Type::Map:
        return m_impl->map.size();
    case Type::Sequence:
        return m_impl->sequence.size();
    default:
        return 0;
    }
}

inline void Node::clear()
{
    ensure_impl();
    m_impl->scalar.clear();
    m_impl->map.clear();
    m_impl->sequence.clear();
    m_impl->type = Type::Null;
}

inline Node::sequence_iterator Node::begin()
{
    if (!IsSequence())
        return detail::empty_sequence().begin();
    return m_impl->sequence.begin();
}

inline Node::sequence_iterator Node::end()
{
    if (!IsSequence())
        return detail::empty_sequence().end();
    return m_impl->sequence.end();
}

inline Node::const_sequence_iterator Node::begin() const
{
    if (!IsSequence())
        return detail::empty_sequence_const().cbegin();
    return m_impl->sequence.cbegin();
}

inline Node::const_sequence_iterator Node::end() const
{
    if (!IsSequence())
        return detail::empty_sequence_const().cend();
    return m_impl->sequence.cend();
}

inline Node::const_sequence_iterator Node::cbegin() const
{
    return begin();
}

inline Node::const_sequence_iterator Node::cend() const
{
    return end();
}

inline void Node::SetStyle(EmitterStyle style)
{
    ensure_impl();
    m_impl->style = style;
}

inline EmitterStyle Node::GetStyle() const
{
    if (!m_impl)
        return EmitterStyle::Default;
    return m_impl->style;
}

inline std::string Node::Scalar() const
{
    if (!IsScalar())
        return std::string{};
    return m_impl->scalar;
}

inline const std::vector<std::pair<std::string, Node>>& Node::MapItems() const
{
    if (!IsMap())
        return detail::empty_map();
    return m_impl->map;
}

inline std::vector<std::pair<std::string, Node>>& Node::MapItems()
{
    ensure_map();
    return m_impl->map;
}

inline const std::vector<Node>& Node::SequenceItems() const
{
    if (!IsSequence())
        return detail::empty_sequence_const();
    return m_impl->sequence;
}

inline std::vector<Node>& Node::SequenceItems()
{
    ensure_sequence();
    return m_impl->sequence;
}

inline Node::Type Node::GetType() const
{
    if (!m_impl)
        return Type::Null;
    return m_impl->type;
}

namespace detail
{
    inline Node FromRyml(const ryml::ConstNodeRef& ref)
    {
        Node node;

        if (ref.is_map())
        {
            for (std::size_t i = 0; i < ref.num_children(); ++i)
            {
                const auto child = ref.child(i);
                std::string key;
                if (child.has_key())
                {
                    auto k = child.key();
                    key.assign(k.str, k.len);
                }
                node[key] = FromRyml(child);
            }
        }
        else if (ref.is_seq())
        {
            for (std::size_t i = 0; i < ref.num_children(); ++i)
            {
                node.push_back(FromRyml(ref.child(i)));
            }
        }
        else if (ref.has_val())
        {
            auto val = ref.val();
            node = std::string(val.str, val.len);
        }
        else
        {
            node.clear();
        }

        return node;
    }

    inline void ToRyml(const Node& node, ryml::NodeRef ref)
    {
        if (!node || node.IsNull())
        {
            ref |= ryml::VAL;
            ref << "";
            return;
        }

        if (node.IsMap())
        {
            ref |= ryml::MAP;
            for (const auto& [key, value] : node.MapItems())
            {
                auto child = ref.append_child();
                child << ryml::key(key.c_str());
                ToRyml(value, child);
            }
        }
        else if (node.IsSequence())
        {
            ref |= ryml::SEQ;
            for (const auto& element : node.SequenceItems())
            {
                auto child = ref.append_child();
                ToRyml(element, child);
            }
        }
        else if (node.IsScalar())
        {
            ref |= ryml::VAL;
            const std::string value = node.Scalar();
            ref << value.c_str();
        }
        else
        {
            ref |= ryml::VAL;
            ref << "";
        }
    }
}

inline Node Load(std::string_view yaml)
{
    try
    {
        ryml::Tree tree = ryml::parse_in_arena(ryml::csubstr(yaml.data(), yaml.size()));
        return detail::FromRyml(tree.rootref());
    }
    catch (...)
    {
        return Node();
    }
}

inline Node LoadFile(std::string_view path)
{
    std::ifstream input(std::string(path));
    if (!input.is_open())
    {
        return Node();
    }

    std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    return Load(contents);
}

inline std::string Dump(const Node& node)
{
    ryml::Tree tree;
    auto root = tree.rootref();
    detail::ToRyml(node, root);
    return ryml::emitrs_yaml<std::string>(root);
}

inline std::ostream& operator<<(std::ostream& os, const Node& node)
{
    os << Dump(node);
    return os;
}

} // namespace MetaYml
