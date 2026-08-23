#include <functional>
#include <concepts>
#include <compare>
#include <iostream>

template<typename T>
struct Property
{
    std::function<T()> get;
    std::function<void(const T&)> set;
    T value;

    // implicit conversion to T
    operator T() const {
        return get ? get() : value;
    }

    // explicit bool conversion
    explicit operator bool() const requires std::convertible_to<T, bool> {
        return static_cast<bool>(T(*this));
    }

    // assignment
    Property& operator=(const T& val) {
        if (set) set(val);
        else value = val;
        return *this;
    }

    // arithmetic compound assignment
    template<typename U> requires requires(T a, U b) { a += b; }
    Property& operator+=(const U& rhs) {
        *this = T(*this) + rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a -= b; }
    Property& operator-=(const U& rhs) {
        *this = T(*this) - rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a *= b; }
    Property& operator*=(const U& rhs) {
        *this = T(*this) * rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a /= b; }
    Property& operator/=(const U& rhs) {
        *this = T(*this) / rhs;
        return *this;
    }

    // bitwise compound assignment
    template<typename U> requires requires(T a, U b) { a |= b; }
    Property& operator|=(const U& rhs) {
        *this = T(*this) | rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a &= b; }
    Property& operator&=(const U& rhs) {
        *this = T(*this) & rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a ^= b; }
    Property& operator^=(const U& rhs) {
        *this = T(*this) ^ rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a <<= b; }
    Property& operator<<=(const U& rhs) {
        *this = T(*this) << rhs;
        return *this;
    }

    template<typename U> requires requires(T a, U b) { a >>= b; }
    Property& operator>>=(const U& rhs) {
        *this = T(*this) >> rhs;
        return *this;
    }

    // arithmetic binary
    template<typename U> requires requires(T a, U b) { a + b; }
    auto operator+(const U& rhs) const { return T(*this) + rhs; }

    template<typename U> requires requires(T a, U b) { a - b; }
    auto operator-(const U& rhs) const { return T(*this) - rhs; }

    template<typename U> requires requires(T a, U b) { a* b; }
    auto operator*(const U& rhs) const { return T(*this) * rhs; }

    template<typename U> requires requires(T a, U b) { a / b; }
    auto operator/(const U& rhs) const { return T(*this) / rhs; }

    // bitwise binary
    template<typename U> requires requires(T a, U b) { a | b; }
    auto operator|(const U& rhs) const { return T(*this) | rhs; }

    template<typename U> requires requires(T a, U b) { a& b; }
    auto operator&(const U& rhs) const { return T(*this) & rhs; }

    template<typename U> requires requires(T a, U b) { a^ b; }
    auto operator^(const U& rhs) const { return T(*this) ^ rhs; }

    template<typename U> requires requires(T a, U b) { a << b; }
    auto operator<<(const U& rhs) const { return T(*this) << rhs; }

    template<typename U> requires requires(T a, U b) { a >> b; }
    auto operator>>(const U& rhs) const { return T(*this) >> rhs; }

    // unary operators
    auto operator~() const requires requires(T a) { ~a; } {
        return ~T(*this);
    }

    auto operator!() const requires requires(T a) { !a; } {
        return !T(*this);
    }

    // increment/decrement
    Property& operator++() requires requires(T a) { ++a; } {
        *this = ++T(*this);
        return *this;
    }

    Property operator++(int) requires requires(T a) { a++; } {
        Property copy = *this;
        ++(*this);
        return copy;
    }

    Property& operator--() requires requires(T a) { --a; } {
        *this = --T(*this);
        return *this;
    }

    Property operator--(int) requires requires(T a) { a--; } {
        Property copy = *this;
        --(*this);
        return copy;
    }

    // comparison
    template<typename U> requires requires(T a, U b) { a == b; }
    bool operator==(const U& rhs) const {
        return T(*this) == rhs;
    }

    template<typename U> requires requires(T a, U b) { a <=> b; }
    auto operator<=>(const U& rhs) const {
        return T(*this) <=> rhs;
    }
};

// ostream 출력 지원
template<typename T>
std::ostream& operator<<(std::ostream& os, const Property<T>& prop)
{
    os << T(prop); // implicit cast
    return os;
}

// std::hash 지원
namespace std {
    template<typename T>
    struct hash<Property<T>> {
        size_t operator()(const Property<T>& p) const requires requires(T t) { std::hash<T>{}(t); } {
            return std::hash<T>{}(T(p));
        }
    };
}