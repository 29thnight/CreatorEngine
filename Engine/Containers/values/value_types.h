#pragma once
#include "../include/ce/all.h"
#include <type_traits>
#include <utility>
#include <tuple>
#include <variant>
#include <array>
#include <functional>
#include <compare>
namespace ce
{
    template<class First, class Second>
    struct pair;
    template<class... Types>
    class tuple;

    namespace value_detail
    {
        template<class T>
        void accept_implicit(T);
        template<class T>
        concept implicit_default = requires { accept_implicit<T>({}); };

        template<std::size_t Index, class Head, class... Tail>
        struct nth : nth<Index - 1, Tail...>
        {
        };

        template<class Head, class... Tail>
        struct nth<0, Head, Tail...>
        {
            using type = Head;
        };

        template<class T>
        struct is_reference_wrapper : std::false_type
        {
        };

        template<class T>
        struct is_reference_wrapper<std::reference_wrapper<T>> : std::true_type
        {
        };

        // A reference product borrows; it never extends a conversion temporary's life.
        template<class Target, class Source>
        inline constexpr bool safe_construct =
            std::is_constructible_v<Target, Source> &&
            (!std::is_lvalue_reference_v<Target> || std::is_lvalue_reference_v<Source> ||
             is_reference_wrapper<std::remove_cvref_t<Source>>::value) &&
            !std::reference_constructs_from_temporary_v<Target, Source>;

        template<class T, class = void>
        struct extent
        {
            static constexpr std::size_t value = std::size_t(-1);
        };

        template<class T>
        struct extent<T, std::void_t<decltype(std::tuple_size<T>::value)>> : std::tuple_size<T>
        {
        };

        template<class A, class B>
        struct extent<pair<A, B>, void> : std::integral_constant<std::size_t, 2>
        {
        };

        template<class... T>
        struct extent<tuple<T...>, void> : std::integral_constant<std::size_t, sizeof...(T)>
        {
        };
        template<class T>
        inline constexpr std::size_t extent_v = extent<std::remove_cvref_t<T>>::value;

        template<std::size_t I, class T>
        struct element : std::tuple_element<I, T>
        {
        };

        template<std::size_t I, class A, class B>
        struct element<I, pair<A, B>> : nth<I, A, B>
        {
        };

        template<std::size_t I, class... T>
        struct element<I, tuple<T...>> : nth<I, T...>
        {
        };
        template<std::size_t I, class T>
        using element_t = typename element<I, std::remove_cvref_t<T>>::type;

        using std::get;

        template<std::size_t I, class T>
        constexpr decltype(auto) adl_get(T&& value) noexcept(noexcept(get<I>(static_cast<T&&>(value))))
        {
            return get<I>(static_cast<T&&>(value));
        }

        template<std::size_t I, class T>
        constexpr decltype(auto) access(T&& value) noexcept(
            []
            {
                if constexpr (requires(T&& v) { static_cast<T&&>(v).template get<I>(); })
                    return noexcept(std::declval<T>().template get<I>());
                else
                    return noexcept(adl_get<I>(std::declval<T>()));
            }())
        {
            if constexpr (requires { static_cast<T&&>(value).template get<I>(); })
                return static_cast<T&&>(value).template get<I>();
            else
                return adl_get<I>(static_cast<T&&>(value));
        }

        template<class Target, std::size_t I, class Product>
        inline constexpr bool safe_element = safe_construct<Target, decltype(access<I>(std::declval<Product>()))> &&
                                             !(std::is_reference_v<Target> && !std::is_lvalue_reference_v<Product> &&
                                               !std::is_reference_v<element_t<I, Product>>);

        template<class T, class... Types>
        inline constexpr std::size_t count_type = (std::size_t(0) + ... + std::size_t(std::is_same_v<T, Types>));

        template<class T, class... Types>
        constexpr std::size_t type_index() noexcept
        {
            constexpr bool matches[] = {std::is_same_v<T, Types>...};
            for (std::size_t i = 0; i < sizeof...(Types); ++i)
                if (matches[i])
                    return i;
            return sizeof...(Types);
        }

        template<class A, class B>
        constexpr auto compare(const A& a, const B& b)
        {
            if constexpr (requires { a <=> b; })
                return a <=> b;
            else
            {
                if (a < b)
                    return std::weak_ordering::less;
                if (b < a)
                    return std::weak_ordering::greater;
                return std::weak_ordering::equivalent;
            }
        }

        template<class T>
        constexpr void swap_value(T& a, T& b) noexcept(std::is_nothrow_swappable_v<T>)
        {
            using std::swap;
            swap(a, b);
        }

        template<class Member>
        struct member_class;

        template<class M, class C>
        struct member_class<M C::*>
        {
            using type = C;
        };

        template<class Class, class T>
        constexpr decltype(auto) object(T&& value) noexcept(
            []
            {
                if constexpr (std::is_base_of_v<Class, std::remove_cvref_t<T>> ||
                              is_reference_wrapper<std::remove_cvref_t<T>>::value)
                    return true;
                else
                    return noexcept(*std::declval<T>());
            }())
        {
            if constexpr (std::is_base_of_v<Class, std::remove_cvref_t<T>>)
                return static_cast<T&&>(value);
            else if constexpr (is_reference_wrapper<std::remove_cvref_t<T>>::value)
                return value.get();
            else
                return *static_cast<T&&>(value);
        }

        template<class F, class... A>
            requires(!std::is_member_pointer_v<std::remove_cvref_t<F>>)
        constexpr decltype(auto) invoke(F&& function, A&&... args) noexcept(
            noexcept(static_cast<F&&>(function)(static_cast<A&&>(args)...)))
        {
            return static_cast<F&&>(function)(static_cast<A&&>(args)...);
        }

        template<class F, class T, class... A>
            requires std::is_member_function_pointer_v<std::remove_cvref_t<F>>
        constexpr decltype(auto) invoke(F&& function, T&& target, A&&... args) noexcept(
            noexcept((object<typename member_class<std::remove_cvref_t<F>>::type>(static_cast<T&&>(target)).*
                      function)(static_cast<A&&>(args)...)))
        {
            return (object<typename member_class<std::remove_cvref_t<F>>::type>(static_cast<T&&>(target)).*
                    function)(static_cast<A&&>(args)...);
        }

        template<class F, class T>
            requires std::is_member_object_pointer_v<std::remove_cvref_t<F>>
        constexpr decltype(auto) invoke(F&& member, T&& target) noexcept(
            noexcept(object<typename member_class<std::remove_cvref_t<F>>::type>(static_cast<T&&>(target)).*member))
        {
            return object<typename member_class<std::remove_cvref_t<F>>::type>(static_cast<T&&>(target)).*member;
        }

        template<std::size_t I, class T>
        struct leaf
        {
#if defined(_MSC_VER) && !defined(__clang__)
            [[msvc::no_unique_address]]
#else
            [[no_unique_address]]
#endif
            T value;

            constexpr leaf() noexcept(std::is_nothrow_default_constructible_v<T>)
                requires std::is_default_constructible_v<T>
                : value()
            {
            }

            template<class U>
                requires std::is_constructible_v<T, U&&>
            constexpr explicit leaf(U&& source) noexcept(std::is_nothrow_constructible_v<T, U&&>)
                : value(static_cast<U&&>(source))
            {
            }
        };

        struct direct_tag
        {
        };

        struct product_tag
        {
        };
        template<class Sequence, class... T>
        struct storage;

        template<std::size_t I, class Head, class... Tail>
        constexpr decltype(auto) select(Head&& head, Tail&&... tail) noexcept
        {
            if constexpr (I == 0)
                return static_cast<Head&&>(head);
            else
                return select<I - 1>(static_cast<Tail&&>(tail)...);
        }

        template<std::size_t... I, class... T>
        struct storage<std::index_sequence<I...>, T...>
            : leaf<sizeof...(T) - 1 - I, typename nth<sizeof...(T) - 1 - I, T...>::type>...
        {
            constexpr storage() noexcept((std::is_nothrow_default_constructible_v<T> && ...))
                requires(std::is_default_constructible_v<T> && ...)
                : leaf<sizeof...(T) - 1 - I, typename nth<sizeof...(T) - 1 - I, T...>::type>()...
            {
            }

            template<class... U>
            constexpr storage(direct_tag, U&&... values) noexcept((std::is_nothrow_constructible_v<T, U&&> && ...))
                : leaf<sizeof...(T) - 1 - I, typename nth<sizeof...(T) - 1 - I, T...>::type>(
                      select<sizeof...(T) - 1 - I>(static_cast<U&&>(values)...))...
            {
            }

            template<class P>
            constexpr storage(product_tag, P&& values) noexcept(
                (std::is_nothrow_constructible_v<T, decltype(access<I>(std::declval<P>()))> && ...))
                : leaf<sizeof...(T) - 1 - I, typename nth<sizeof...(T) - 1 - I, T...>::type>(
                      access<sizeof...(T) - 1 - I>(static_cast<P&&>(values)))...
            {
            }
        };
    } // namespace value_detail

    struct piecewise_construct_t
    {
        explicit constexpr piecewise_construct_t() = default;
    };

    inline constexpr piecewise_construct_t piecewise_construct{};

    template<class First, class Second>
    struct pair
    {
        using first_type = First;
        using second_type = Second;
        First first;
        Second second;

        constexpr explicit(!value_detail::implicit_default<First> || !value_detail::implicit_default<Second>)
            pair() noexcept(std::is_nothrow_default_constructible_v<First> &&
                            std::is_nothrow_default_constructible_v<Second>)
            requires(std::is_default_constructible_v<First> && std::is_default_constructible_v<Second>)
            : first(), second()
        {
        }

        constexpr pair(const pair&) = default;
        constexpr pair(pair&&) = default;

        template<class A = std::remove_reference_t<First>, class B = std::remove_reference_t<Second>>
            requires(value_detail::safe_construct<First, A &&> && value_detail::safe_construct<Second, B &&>)
        constexpr explicit(!std::is_convertible_v<A&&, First> || !std::is_convertible_v<B&&, Second>)
            pair(A&& a, B&& b) noexcept(std::is_nothrow_constructible_v<First, A&&> &&
                                        std::is_nothrow_constructible_v<Second, B&&>)
            : first(static_cast<A&&>(a)), second(static_cast<B&&>(b))
        {
        }

        template<class P>
            requires(!std::is_same_v<std::remove_cvref_t<P>, pair> &&
                     requires(P&& p) {
                         p.first;
                         p.second;
                     } && value_detail::safe_construct<First, decltype((std::declval<P>().first))> &&
                     value_detail::safe_construct<Second, decltype((std::declval<P>().second))> &&
                     !(std::is_reference_v<First> && !std::is_lvalue_reference_v<P> &&
                       !std::is_reference_v<decltype(std::declval<P>().first)>) &&
                     !(std::is_reference_v<Second> && !std::is_lvalue_reference_v<P> &&
                       !std::is_reference_v<decltype(std::declval<P>().second)>))
        constexpr explicit(!std::is_convertible_v<decltype((std::declval<P>().first)), First> ||
                           !std::is_convertible_v<decltype((std::declval<P>().second)), Second>)
            pair(P&& other) noexcept(std::is_nothrow_constructible_v<First, decltype((std::declval<P>().first))> &&
                                     std::is_nothrow_constructible_v<Second, decltype((std::declval<P>().second))>)
            : first(static_cast<P&&>(other).first), second(static_cast<P&&>(other).second)
        {
        }

      private:
        template<class A, class B, std::size_t... I, std::size_t... J>
        constexpr pair(
            piecewise_construct_t, A&& a, B&& b, std::index_sequence<I...>,
            std::index_sequence<J...>) noexcept(std::is_nothrow_constructible_v<First,
                                                                                decltype(value_detail::access<I>(
                                                                                    std::declval<A>()))...> &&
                                                std::is_nothrow_constructible_v<
                                                    Second, decltype(value_detail::access<J>(std::declval<B>()))...>)
            : first(value_detail::access<I>(static_cast<A&&>(a))...),
              second(value_detail::access<J>(static_cast<B&&>(b))...)
        {
        }

      public:
        template<class A, class B>
            requires(!std::is_reference_v<First> && !std::is_reference_v<Second> &&
                     value_detail::extent_v<A> != std::size_t(-1) && value_detail::extent_v<B> != std::size_t(-1))
        constexpr pair(piecewise_construct_t tag, A&& a,
                       B&& b) noexcept(noexcept(pair(tag, static_cast<A&&>(a), static_cast<B&&>(b),
                                                     std::make_index_sequence<value_detail::extent_v<A>>{},
                                                     std::make_index_sequence<value_detail::extent_v<B>>{})))
            : pair(tag, static_cast<A&&>(a), static_cast<B&&>(b), std::make_index_sequence<value_detail::extent_v<A>>{},
                   std::make_index_sequence<value_detail::extent_v<B>>{})
        {
        }

        constexpr pair& operator=(const pair&)
            requires(!std::is_reference_v<First> && !std::is_reference_v<Second>)
        = default;
        constexpr pair& operator=(pair&&)
            requires(!std::is_reference_v<First> && !std::is_reference_v<Second>)
        = default;

        template<class P>
            requires requires(pair& self, P&& p) {
                self.first = static_cast<P&&>(p).first;
                self.second = static_cast<P&&>(p).second;
            }
        constexpr pair& operator=(P&& other) noexcept(noexcept(first = static_cast<P&&>(other).first) &&
                                                      noexcept(second = static_cast<P&&>(other).second))
        {
            first = static_cast<P&&>(other).first;
            second = static_cast<P&&>(other).second;
            return *this;
        }

        template<class P>
            requires requires(const pair& self, P&& p) {
                self.first = static_cast<P&&>(p).first;
                self.second = static_cast<P&&>(p).second;
            }
        constexpr const pair& operator=(P&& other) const noexcept(noexcept(first = static_cast<P&&>(other).first) &&
                                                                  noexcept(second = static_cast<P&&>(other).second))
        {
            first = static_cast<P&&>(other).first;
            second = static_cast<P&&>(other).second;
            return *this;
        }

        template<std::size_t I>
            requires(I < 2)
        constexpr decltype(auto) get() & noexcept
        {
            if constexpr (I == 0)
                return (first);
            else
                return (second);
        }

        template<std::size_t I>
            requires(I < 2)
        constexpr decltype(auto) get() const& noexcept
        {
            if constexpr (I == 0)
                return (first);
            else
                return (second);
        }

        template<std::size_t I>
            requires(I < 2)
        constexpr decltype(auto) get() && noexcept
        {
            if constexpr (I == 0)
                return static_cast<First&&>(first);
            else
                return static_cast<Second&&>(second);
        }

        template<std::size_t I>
            requires(I < 2)
        constexpr decltype(auto) get() const&& noexcept
        {
            if constexpr (I == 0)
                return static_cast<const First&&>(first);
            else
                return static_cast<const Second&&>(second);
        }

        template<class T>
            requires(value_detail::count_type<T, First, Second> == 1)
        constexpr decltype(auto) get() & noexcept
        {
            return this->template get<value_detail::type_index<T, First, Second>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, First, Second> == 1)
        constexpr decltype(auto) get() const& noexcept
        {
            return this->template get<value_detail::type_index<T, First, Second>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, First, Second> == 1)
        constexpr decltype(auto) get() && noexcept
        {
            return static_cast<pair&&>(*this).template get<value_detail::type_index<T, First, Second>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, First, Second> == 1)
        constexpr decltype(auto) get() const&& noexcept
        {
            return static_cast<const pair&&>(*this).template get<value_detail::type_index<T, First, Second>()>();
        }

        constexpr void swap(pair& other) noexcept(std::is_nothrow_swappable_v<First> &&
                                                  std::is_nothrow_swappable_v<Second>)
            requires(std::is_swappable_v<First> && std::is_swappable_v<Second>)
        {
            value_detail::swap_value(first, other.first);
            value_detail::swap_value(second, other.second);
        }

        template<class P>
            requires requires(const pair& a, const P& b) {
                a.first == b.first;
                a.second == b.second;
            }
        constexpr bool operator==(const P& other) const
        {
            return first == other.first && second == other.second;
        }

        template<class P>
            requires requires(const pair& a, const P& b) {
                a.first < b.first;
                a.second < b.second;
            }
        constexpr auto operator<=>(const P& other) const
        {
            using order = std::common_comparison_category_t<decltype(value_detail::compare(first, other.first)),
                                                            decltype(value_detail::compare(second, other.second))>;
            const order leading = value_detail::compare(first, other.first);
            if (leading != 0)
                return leading;
            return order(value_detail::compare(second, other.second));
        }
    };

    template<class A, class B>
    pair(A, B) -> pair<A, B>;

    template<class A, class B>
    constexpr void swap(pair<A, B>& a, pair<A, B>& b) noexcept(noexcept(a.swap(b)))
    {
        a.swap(b);
    }

    template<class... Types>
    class tuple : public value_detail::storage<std::index_sequence_for<Types...>, Types...>
    {
        using base = value_detail::storage<std::index_sequence_for<Types...>, Types...>;
        static constexpr bool references = (std::is_reference_v<Types> || ...);

        template<class P, std::size_t... I>
        static constexpr bool safe_product(std::index_sequence<I...>)
        {
            return (value_detail::safe_element<Types, I, P> && ...);
        }

        template<class P>
        static constexpr bool single_direct()
        {
            if constexpr (sizeof...(Types) == 1)
                return std::is_constructible_v<typename value_detail::nth<0, Types...>::type, P>;
            else
                return false;
        }

        template<class P, std::size_t... I>
        static constexpr bool product_convertible(std::index_sequence<I...>)
        {
            return (std::is_convertible_v<decltype(value_detail::access<I>(std::declval<P>())), Types> && ...);
        }

        template<class Self, class P, std::size_t... I>
        static constexpr bool assignable(std::index_sequence<I...>)
        {
            return (std::is_assignable_v<decltype(std::declval<Self>().template get<I>()),
                                         decltype(value_detail::access<I>(std::declval<P>()))> &&
                    ...);
        }

        template<class P, std::size_t... I>
        constexpr void assign(P&& other, std::index_sequence<I...>) noexcept(
            (std::is_nothrow_assignable_v<decltype(this->template get<I>()),
                                          decltype(value_detail::access<I>(std::declval<P>()))> &&
             ...))
        {
            ((this->template get<I>() = value_detail::access<I>(static_cast<P&&>(other))), ...);
        }

        template<class P, std::size_t... I>
        constexpr void assign(P&& other, std::index_sequence<I...>) const
            noexcept((std::is_nothrow_assignable_v<decltype(this->template get<I>()),
                                                   decltype(value_detail::access<I>(std::declval<P>()))> &&
                      ...))
        {
            ((this->template get<I>() = value_detail::access<I>(static_cast<P&&>(other))), ...);
        }

        template<class P, std::size_t... I>
        constexpr bool equal(const P& other, std::index_sequence<I...>) const
        {
            return ((this->template get<I>() == value_detail::access<I>(other)) && ...);
        }

        template<std::size_t I, class Order, class P>
        constexpr Order compare_element(const P& other) const
        {
            if constexpr (I == sizeof...(Types))
                return Order::equivalent;
            else
            {
                const Order result = value_detail::compare(this->template get<I>(), value_detail::access<I>(other));
                if (result != 0)
                    return result;
                return compare_element<I + 1, Order>(other);
            }
        }

        template<std::size_t I, class P>
        constexpr bool less_element(const P& other) const
        {
            if constexpr (I == sizeof...(Types))
                return false;
            else
            {
                const auto result = value_detail::compare(this->template get<I>(), value_detail::access<I>(other));
                return result < 0 || (result == 0 && less_element<I + 1>(other));
            }
        }

        template<class P, std::size_t... I>
        constexpr auto compare(const P& other, std::index_sequence<I...>) const
        {
            using order = std::common_comparison_category_t<decltype(value_detail::compare(
                this->template get<I>(), value_detail::access<I>(other)))...>;
            return compare_element<0, order>(other);
        }

        template<std::size_t... I>
        constexpr void swap_elements(tuple& other,
                                     std::index_sequence<I...>) noexcept((std::is_nothrow_swappable_v<Types> && ...))
        {
            (value_detail::swap_value(this->template get<I>(), other.template get<I>()), ...);
        }

      public:
        constexpr explicit(!(value_detail::implicit_default<Types> && ...))
            tuple() noexcept((std::is_nothrow_default_constructible_v<Types> && ...))
            requires(std::is_default_constructible_v<Types> && ...)
        = default;
        constexpr tuple(const tuple&) = default;
        constexpr tuple(tuple&&) = default;

        // Typed list arguments have no deducible type. This overload owns copies;
        // reference products continue through the guarded forwarding constructors.
        constexpr tuple(const Types&... values) noexcept((std::is_nothrow_copy_constructible_v<Types> && ...))
            requires(sizeof...(Types) != 0 && (!std::is_reference_v<Types> && ...) &&
                     (std::is_copy_constructible_v<Types> && ...))
            : base(value_detail::direct_tag{}, values...)
        {
        }

        template<class... U>
            requires(sizeof...(U) == sizeof...(Types) && sizeof...(U) != 0 &&
                     (value_detail::safe_construct<Types, U &&> && ...))
        constexpr explicit(!(std::is_convertible_v<U&&, Types> && ...))
            tuple(U&&... values) noexcept((std::is_nothrow_constructible_v<Types, U&&> && ...))
            : base(value_detail::direct_tag{}, static_cast<U&&>(values)...)
        {
        }

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types) && !std::is_same_v<std::remove_cvref_t<P>, tuple> &&
                     !single_direct<P &&>() && safe_product<P &&>(std::index_sequence_for<Types...>{}))
        constexpr explicit(!product_convertible<P&&>(std::index_sequence_for<Types...>{}))
            tuple(P&& other) noexcept(noexcept(base(value_detail::product_tag{}, static_cast<P&&>(other))))
            : base(value_detail::product_tag{}, static_cast<P&&>(other))
        {
        }

        template<std::size_t I>
            requires(I < sizeof...(Types))
        constexpr decltype(auto) get() & noexcept
        {
            using T = typename value_detail::nth<I, Types...>::type;
            return (static_cast<value_detail::leaf<I, T>&>(*this).value);
        }

        template<std::size_t I>
            requires(I < sizeof...(Types))
        constexpr decltype(auto) get() const& noexcept
        {
            using T = typename value_detail::nth<I, Types...>::type;
            return (static_cast<const value_detail::leaf<I, T>&>(*this).value);
        }

        template<std::size_t I>
            requires(I < sizeof...(Types))
        constexpr decltype(auto) get() && noexcept
        {
            using T = typename value_detail::nth<I, Types...>::type;
            return static_cast<T&&>(static_cast<value_detail::leaf<I, T>&>(*this).value);
        }

        template<std::size_t I>
            requires(I < sizeof...(Types))
        constexpr decltype(auto) get() const&& noexcept
        {
            using T = typename value_detail::nth<I, Types...>::type;
            return static_cast<const T&&>(static_cast<const value_detail::leaf<I, T>&>(*this).value);
        }

        template<class T>
            requires(value_detail::count_type<T, Types...> == 1)
        constexpr decltype(auto) get() & noexcept
        {
            return this->template get<value_detail::type_index<T, Types...>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, Types...> == 1)
        constexpr decltype(auto) get() const& noexcept
        {
            return this->template get<value_detail::type_index<T, Types...>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, Types...> == 1)
        constexpr decltype(auto) get() && noexcept
        {
            return static_cast<tuple&&>(*this).template get<value_detail::type_index<T, Types...>()>();
        }

        template<class T>
            requires(value_detail::count_type<T, Types...> == 1)
        constexpr decltype(auto) get() const&& noexcept
        {
            return static_cast<const tuple&&>(*this).template get<value_detail::type_index<T, Types...>()>();
        }

        constexpr tuple& operator=(const tuple&)
            requires(!references)
        = default;
        constexpr tuple& operator=(tuple&&)
            requires(!references)
        = default;

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types) &&
                     assignable<tuple&, P &&>(std::index_sequence_for<Types...>{}))
        constexpr tuple& operator=(P && other) noexcept(noexcept(assign(static_cast<P&&>(other),
                                                                        std::index_sequence_for<Types...>{})))
        {
            assign(static_cast<P&&>(other), std::index_sequence_for<Types...>{});
            return *this;
        }

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types) &&
                     assignable<const tuple&, P &&>(std::index_sequence_for<Types...>{}))
        constexpr const tuple& operator=(P && other) const
            noexcept(noexcept(assign(static_cast<P&&>(other), std::index_sequence_for<Types...>{})))
        {
            assign(static_cast<P&&>(other), std::index_sequence_for<Types...>{});
            return *this;
        }

        constexpr void swap(tuple& other) noexcept((std::is_nothrow_swappable_v<Types> && ...))
            requires(std::is_swappable_v<Types> && ...)
        {
            swap_elements(other, std::index_sequence_for<Types...>{});
        }

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types))
        constexpr bool operator==(const P& other) const
        {
            return equal(other, std::index_sequence_for<Types...>{});
        }

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types))
        constexpr bool operator<(const P& other) const
        {
            return less_element<0>(other);
        }

        template<class P>
            requires(value_detail::extent_v<P> == sizeof...(Types))
        constexpr auto operator<=>(const P& other) const
        {
            return compare(other, std::index_sequence_for<Types...>{});
        }
    };

    template<class... T>
    tuple(T...) -> tuple<T...>;

    template<class... T>
    constexpr void swap(tuple<T...>& a, tuple<T...>& b) noexcept(noexcept(a.swap(b)))
    {
        a.swap(b);
    }

    template<std::size_t I, class P>
        requires requires(P&& p) { static_cast<P&&>(p).template get<I>(); }
    constexpr decltype(auto) get(P&& product) noexcept(noexcept(static_cast<P&&>(product).template get<I>()))
    {
        return static_cast<P&&>(product).template get<I>();
    }

    template<std::size_t I, class P>
        requires(
            !requires(P&& p) { static_cast<P&&>(p).template get<I>(); } &&
            requires(P&& p) { std::get<I>(static_cast<P&&>(p)); })
    constexpr decltype(auto) get(P&& product) noexcept(noexcept(std::get<I>(static_cast<P&&>(product))))
    {
        return std::get<I>(static_cast<P&&>(product));
    }

    template<class T, class P>
        requires requires(P&& p) { static_cast<P&&>(p).template get<T>(); }
    constexpr decltype(auto) get(P&& product) noexcept(noexcept(static_cast<P&&>(product).template get<T>()))
    {
        return static_cast<P&&>(product).template get<T>();
    }

    template<class T, class P>
        requires(
            !requires(P&& p) { static_cast<P&&>(p).template get<T>(); } &&
            requires(P&& p) { std::get<T>(static_cast<P&&>(p)); })
    constexpr decltype(auto) get(P&& product) noexcept(noexcept(std::get<T>(static_cast<P&&>(product))))
    {
        return std::get<T>(static_cast<P&&>(product));
    }

    template<class T>
    struct tuple_size : value_detail::extent<std::remove_cv_t<T>>
    {
    };
    template<class T>
    inline constexpr std::size_t tuple_size_v = tuple_size<T>::value;

    template<std::size_t I, class T>
    struct tuple_element : value_detail::element<I, T>
    {
    };

    template<std::size_t I, class T>
    struct tuple_element<I, const T>
    {
        using type = std::add_const_t<typename tuple_element<I, T>::type>;
    };

    template<std::size_t I, class T>
    struct tuple_element<I, volatile T>
    {
        using type = std::add_volatile_t<typename tuple_element<I, T>::type>;
    };

    template<std::size_t I, class T>
    struct tuple_element<I, const volatile T>
    {
        using type = std::add_cv_t<typename tuple_element<I, T>::type>;
    };
    template<std::size_t I, class T>
    using tuple_element_t = typename tuple_element<I, T>::type;

    template<class A, class B>
    constexpr auto make_pair(A&& a, B&& b) noexcept(
        std::is_nothrow_constructible_v<pair<std::unwrap_ref_decay_t<A>, std::unwrap_ref_decay_t<B>>, A&&, B&&>)
    {
        return pair<std::unwrap_ref_decay_t<A>, std::unwrap_ref_decay_t<B>>(static_cast<A&&>(a), static_cast<B&&>(b));
    }

    template<class... T>
    constexpr auto make_tuple(T&&... values) noexcept(
        std::is_nothrow_constructible_v<tuple<std::unwrap_ref_decay_t<T>...>, T&&...>)
    {
        return tuple<std::unwrap_ref_decay_t<T>...>(static_cast<T&&>(values)...);
    }

    template<class... T>
    constexpr auto tie(T&... values) noexcept
    {
        return tuple<T&...>(values...);
    }

    template<class... T>
    constexpr auto forward_as_tuple(T&&... values) noexcept
    {
        return tuple<T&&...>(static_cast<T&&>(values)...);
    }

    namespace value_detail
    {
        template<class F, class P, std::size_t... I>
        constexpr decltype(auto) apply(F&& function, P&& product, std::index_sequence<I...>) noexcept(
            noexcept(value_detail::invoke(static_cast<F&&>(function), access<I>(static_cast<P&&>(product))...)))
        {
            return value_detail::invoke(static_cast<F&&>(function), access<I>(static_cast<P&&>(product))...);
        }
    } // namespace value_detail

    template<class F, class P>
        requires(value_detail::extent_v<P> != std::size_t(-1))
    constexpr decltype(auto) apply(F&& function, P&& product) noexcept(noexcept(value_detail::apply(
        static_cast<F&&>(function), static_cast<P&&>(product), std::make_index_sequence<value_detail::extent_v<P>>{})))
    {
        return value_detail::apply(static_cast<F&&>(function), static_cast<P&&>(product),
                                   std::make_index_sequence<value_detail::extent_v<P>>{});
    }
} // namespace ce

namespace std
{
    template<class A, class B>
    struct tuple_size<ce::pair<A, B>> : integral_constant<std::size_t, 2>
    {
    };

    template<std::size_t I, class A, class B>
    struct tuple_element<I, ce::pair<A, B>> : ce::value_detail::nth<I, A, B>
    {
    };

    template<class... T>
    struct tuple_size<ce::tuple<T...>> : integral_constant<std::size_t, sizeof...(T)>
    {
    };

    template<std::size_t I, class... T>
    struct tuple_element<I, ce::tuple<T...>> : ce::value_detail::nth<I, T...>
    {
    };
} // namespace std
