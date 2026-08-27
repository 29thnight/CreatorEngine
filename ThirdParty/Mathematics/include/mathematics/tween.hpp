// mathematics/tween.hpp -- stateless interpolation and manager-owned tween state.
//
// tween<T> owns values, timing and policies. It deliberately owns no target
// pointer, callback, clock or manager reference, so a game can keep it in a
// vector, slot map or ECS component without introducing hidden lifetime edges.
#ifndef MATHEMATICS_TWEEN_HPP
#define MATHEMATICS_TWEEN_HPP

#include <mathematics/color.hpp>
#include <mathematics/easing.hpp>
#include <mathematics/quaternion.hpp>
#include <mathematics/rect.hpp>
#include <mathematics/vector.hpp>

#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>

namespace math::interpolation {

struct linear_fn {
    template <typename value_type>
        requires requires(const value_type& from, const value_type& to,
                          float progress) {
            { lerp(from, to, progress) } -> std::same_as<value_type>;
        }
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
    operator()(const value_type& from, const value_type& to,
               float progress) const noexcept(noexcept(lerp(from, to, progress))) {
        return lerp(from, to, progress);
    }
};

struct normalized_linear_fn {
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
    operator()(const quaternion& from, const quaternion& to,
               float progress) const noexcept {
        return nlerp(from, to, progress);
    }
};

struct spherical_fn {
    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr quaternion
    operator()(const quaternion& from, const quaternion& to,
               float progress) const noexcept {
        return slerp(from, to, progress);
    }
};

inline constexpr linear_fn linear{};
inline constexpr normalized_linear_fn normalized_linear{};
inline constexpr spherical_fn spherical{};

} // namespace math::interpolation

namespace math {

namespace detail {

template <typename easing_type>
concept easing_object = std::constructible_from<easing_function, easing_type>;

template <typename interpolator_type, typename value_type>
concept interpolator_object =
    std::is_empty_v<std::remove_cvref_t<interpolator_type>> &&
    std::default_initializable<std::remove_cvref_t<interpolator_type>> &&
    std::is_nothrow_invocable_r_v<
        value_type, const std::remove_cvref_t<interpolator_type>&,
        const value_type&, const value_type&, float>;

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr bool
finite_non_negative(float value) noexcept {
    return value >= 0.0f && value - value == 0.0f;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr float
canonical_time(float value) noexcept {
    if (finite_non_negative(value)) return value;
    assert(false && "tween time must be finite and non-negative");
    return 0.0f;
}

MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr std::uint32_t
saturating_whole(float value) noexcept {
    if (!(value > 0.0f)) return 0u;
    constexpr float limit =
        static_cast<float>(std::numeric_limits<std::uint32_t>::max());
    if (value >= limit) return std::numeric_limits<std::uint32_t>::max();
    return static_cast<std::uint32_t>(value);
}

} // namespace detail

template <typename value_type>
class interpolation_function {
public:
    using function_type = value_type (*)(
        const value_type&, const value_type&, float) noexcept;

    interpolation_function() = delete;

    template <typename interpolator_type>
        requires detail::interpolator_object<interpolator_type, value_type>
    constexpr interpolation_function(interpolator_type) noexcept
        : function_(&invoke<std::remove_cvref_t<interpolator_type>>) {}

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
    operator()(const value_type& from, const value_type& to,
               float progress) const noexcept {
        return function_(from, to, progress);
    }

private:
    template <typename interpolator_type>
    MATHEMATICS_NODISCARD static constexpr value_type
    invoke(const value_type& from, const value_type& to,
           float progress) noexcept {
        return interpolator_type{}(from, to, progress);
    }

    function_type function_;
};

template <typename value_type>
inline constexpr bool linearly_interpolable =
    detail::interpolator_object<interpolation::linear_fn, value_type>;

template <typename value_type, typename easing_type,
          typename interpolator_type>
    requires detail::easing_object<easing_type> &&
             std::is_nothrow_invocable_r_v<
                 value_type, const interpolator_type&,
                 const value_type&, const value_type&, float>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
tween_value(const value_type& from, const value_type& to, float progress,
            const easing_type& easing_value,
            const interpolator_type& interpolator) noexcept {
    return std::invoke(
        interpolator, from, to,
        static_cast<float>(std::invoke(easing_value, progress)));
}

template <typename value_type, typename easing_type = easing::linear_fn>
    requires linearly_interpolable<value_type> &&
             detail::easing_object<easing_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
tween_value(const value_type& from, const value_type& to, float progress,
            const easing_type& easing_value = {}) noexcept {
    return tween_value(from, to, progress, easing_value,
                       interpolation::linear);
}

template <typename value_type, typename easing_type,
          typename interpolator_type>
    requires detail::easing_object<easing_type> &&
             std::is_nothrow_invocable_r_v<
                 value_type, const interpolator_type&,
                 const value_type&, const value_type&, float>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
tween_value_clamped(const value_type& from, const value_type& to,
                    float progress, const easing_type& easing_value,
                    const interpolator_type& interpolator) noexcept {
    return std::invoke(interpolator, from, to,
                       ease_clamped(progress, easing_value));
}

template <typename value_type, typename easing_type = easing::linear_fn>
    requires linearly_interpolable<value_type> &&
             detail::easing_object<easing_type>
MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
tween_value_clamped(const value_type& from, const value_type& to,
                    float progress,
                    const easing_type& easing_value = {}) noexcept {
    return tween_value_clamped(from, to, progress, easing_value,
                               interpolation::linear);
}

enum class tween_state : std::uint8_t {
    playing,
    paused,
    completed
};

enum class tween_playback : std::uint8_t {
    once,
    loop,
    ping_pong
};

struct infinite_cycles_t {
    explicit constexpr infinite_cycles_t() = default;
};

inline constexpr infinite_cycles_t infinite_cycles{};

template <typename value_type>
struct tween_step {
    value_type value;
    tween_state state;
    std::uint32_t completed_cycles;

    MATHEMATICS_NODISCARD constexpr bool completed() const noexcept {
        return state == tween_state::completed;
    }
};

template <typename value_type_>
class tween {
private:
    struct timeline_position {
        float progress;
        bool completed;
    };

public:
    using value_type = value_type_;

    constexpr tween(value_type from, value_type to, float duration_seconds,
                    easing_function easing_value,
                    interpolation_function<value_type> interpolator) noexcept
        : from_(std::move(from)),
          to_(std::move(to)),
          duration_seconds_(detail::canonical_time(duration_seconds)),
          easing_(easing_value),
          interpolator_(interpolator),
          state_(duration_seconds_ == 0.0f
                     ? tween_state::completed
                     : tween_state::playing) {}

    template <typename easing_type>
        requires detail::easing_object<easing_type>
    constexpr tween& ease(easing_type easing_value) & noexcept {
        easing_ = easing_function{easing_value};
        return *this;
    }

    template <typename easing_type>
        requires detail::easing_object<easing_type>
    constexpr tween&& ease(easing_type easing_value) && noexcept {
        this->ease(std::move(easing_value));
        return std::move(*this);
    }

    template <typename interpolator_type>
        requires detail::interpolator_object<interpolator_type, value_type>
    constexpr tween& interpolate(interpolator_type interpolator) & noexcept {
        interpolator_ = interpolation_function<value_type>{interpolator};
        return *this;
    }

    template <typename interpolator_type>
        requires detail::interpolator_object<interpolator_type, value_type>
    constexpr tween&& interpolate(interpolator_type interpolator) && noexcept {
        this->interpolate(std::move(interpolator));
        return std::move(*this);
    }

    constexpr tween& delay(float seconds) & noexcept {
        initial_delay_seconds_ = detail::canonical_time(seconds);
        restart();
        return *this;
    }

    constexpr tween&& delay(float seconds) && noexcept {
        this->delay(seconds);
        return std::move(*this);
    }

    constexpr tween& playback(tween_playback value) & noexcept {
        playback_ = value;
        restart();
        return *this;
    }

    constexpr tween&& playback(tween_playback value) && noexcept {
        this->playback(value);
        return std::move(*this);
    }

    constexpr tween& cycles(std::uint32_t count) & noexcept {
        assert(count >= 1u && "finite tween cycle count must be at least one");
        cycle_count_ = count == 0u ? 1u : count;
        infinite_ = false;
        restart();
        return *this;
    }

    constexpr tween&& cycles(std::uint32_t count) && noexcept {
        this->cycles(count);
        return std::move(*this);
    }

    constexpr tween& cycles(infinite_cycles_t) & noexcept {
        infinite_ = true;
        restart();
        return *this;
    }

    constexpr tween&& cycles(infinite_cycles_t value) && noexcept {
        this->cycles(value);
        return std::move(*this);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
    sample() const noexcept {
        return value_at(elapsed_seconds_);
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
    sample_at(float elapsed_seconds) const noexcept {
        return value_at(detail::canonical_time(elapsed_seconds));
    }

    MATHEMATICS_NODISCARD constexpr tween_step<value_type>
    advance(float delta_seconds) noexcept {
        if (state_ != tween_state::playing) {
            return tween_step<value_type>{sample(), state_, 0u};
        }

        const float delta = detail::canonical_time(delta_seconds);
        const std::uint32_t before = completed_cycle_count(elapsed_seconds_);

        float next = elapsed_seconds_ + delta;
        if (next - next != 0.0f) {
            next = std::numeric_limits<float>::max();
        }
        if (!infinite_ || playback_ == tween_playback::once) {
            const float end = end_time();
            if (next > end) next = end;
        }
        elapsed_seconds_ = next;

        const timeline_position position = locate(elapsed_seconds_);
        if (position.completed) state_ = tween_state::completed;

        const std::uint32_t after = completed_cycle_count(elapsed_seconds_);
        const std::uint32_t crossed =
            after >= before ? after - before
                            : std::numeric_limits<std::uint32_t>::max();
        return tween_step<value_type>{
            interpolator_(from_, to_, easing_(position.progress)),
            state_, crossed};
    }

    constexpr void pause() noexcept {
        if (state_ == tween_state::playing) state_ = tween_state::paused;
    }

    constexpr void resume() noexcept {
        if (state_ == tween_state::paused) state_ = tween_state::playing;
    }

    constexpr void restart() noexcept {
        elapsed_seconds_ = 0.0f;
        state_ = duration_seconds_ == 0.0f
                     ? tween_state::completed
                     : tween_state::playing;
    }

    constexpr void seek(float elapsed_seconds) noexcept {
        const bool was_paused = state_ == tween_state::paused;
        elapsed_seconds_ = detail::canonical_time(elapsed_seconds);
        if (!infinite_ || playback_ == tween_playback::once) {
            const float end = end_time();
            if (elapsed_seconds_ > end) elapsed_seconds_ = end;
        }
        const timeline_position position = locate(elapsed_seconds_);
        state_ = position.completed
                     ? tween_state::completed
                     : (was_paused ? tween_state::paused
                                   : tween_state::playing);
    }

    MATHEMATICS_NODISCARD constexpr tween_state state() const noexcept {
        return state_;
    }

    MATHEMATICS_NODISCARD constexpr bool finished() const noexcept {
        return state_ == tween_state::completed;
    }

    MATHEMATICS_NODISCARD constexpr float elapsed_seconds() const noexcept {
        return elapsed_seconds_;
    }

    MATHEMATICS_NODISCARD constexpr float duration_seconds() const noexcept {
        return duration_seconds_;
    }

    MATHEMATICS_NODISCARD constexpr float initial_delay_seconds() const noexcept {
        return initial_delay_seconds_;
    }

    MATHEMATICS_NODISCARD constexpr const value_type& from() const noexcept {
        return from_;
    }

    MATHEMATICS_NODISCARD constexpr const value_type& to() const noexcept {
        return to_;
    }

private:
    MATHEMATICS_NODISCARD constexpr float cycle_span() const noexcept {
        return playback_ == tween_playback::ping_pong
                   ? duration_seconds_ * 2.0f
                   : duration_seconds_;
    }

    MATHEMATICS_NODISCARD constexpr float end_time() const noexcept {
        if (playback_ != tween_playback::once && infinite_)
            return std::numeric_limits<float>::max();
        const std::uint32_t effective_cycles =
            playback_ == tween_playback::once ? 1u : cycle_count_;
        const float total = initial_delay_seconds_ +
                            cycle_span() * static_cast<float>(effective_cycles);
        return total - total == 0.0f
                   ? total
                   : std::numeric_limits<float>::max();
    }

    MATHEMATICS_NODISCARD constexpr timeline_position
    locate(float elapsed_seconds) const noexcept {
        if (duration_seconds_ == 0.0f) return timeline_position{1.0f, true};
        if (elapsed_seconds <= initial_delay_seconds_)
            return timeline_position{0.0f, false};

        const float active = elapsed_seconds - initial_delay_seconds_;
        if (playback_ == tween_playback::once) {
            if (active >= duration_seconds_)
                return timeline_position{1.0f, true};
            return timeline_position{active / duration_seconds_, false};
        }

        const float span = cycle_span();
        const float total = span * static_cast<float>(cycle_count_);
        if (!infinite_ && active >= total) {
            return timeline_position{
                playback_ == tween_playback::ping_pong ? 0.0f : 1.0f,
                true};
        }

        if (playback_ == tween_playback::loop) {
            const std::uint32_t cycle =
                detail::saturating_whole(active / duration_seconds_);
            const float local =
                active - static_cast<float>(cycle) * duration_seconds_;
            return timeline_position{local / duration_seconds_, false};
        }

        const std::uint32_t leg =
            detail::saturating_whole(active / duration_seconds_);
        const float local =
            active - static_cast<float>(leg) * duration_seconds_;
        const float forward = local / duration_seconds_;
        return timeline_position{
            (leg & 1u) == 0u ? forward : 1.0f - forward, false};
    }

    MATHEMATICS_NODISCARD constexpr std::uint32_t
    completed_cycle_count(float elapsed_seconds) const noexcept {
        if (duration_seconds_ == 0.0f ||
            elapsed_seconds <= initial_delay_seconds_)
            return 0u;
        const float active = elapsed_seconds - initial_delay_seconds_;
        if (playback_ == tween_playback::once)
            return active >= duration_seconds_ ? 1u : 0u;
        const std::uint32_t completed =
            detail::saturating_whole(active / cycle_span());
        return !infinite_ && completed > cycle_count_
                   ? cycle_count_
                   : completed;
    }

    MATHEMATICS_NODISCARD MATHEMATICS_INLINE constexpr value_type
    value_at(float elapsed_seconds) const noexcept {
        const timeline_position position = locate(elapsed_seconds);
        return interpolator_(from_, to_, easing_(position.progress));
    }

    value_type from_;
    value_type to_;
    float elapsed_seconds_ = 0.0f;
    float duration_seconds_ = 0.0f;
    float initial_delay_seconds_ = 0.0f;
    easing_function easing_;
    interpolation_function<value_type> interpolator_;
    std::uint32_t cycle_count_ = 1u;
    tween_playback playback_ = tween_playback::once;
    tween_state state_ = tween_state::playing;
    bool infinite_ = false;
};

template <typename value_type, typename easing_type = easing::linear_fn>
    requires linearly_interpolable<value_type> &&
             detail::easing_object<easing_type>
MATHEMATICS_NODISCARD constexpr tween<value_type>
make_tween(value_type from, value_type to, float duration_seconds,
           easing_type easing_value = {}) noexcept {
    return tween<value_type>{
        std::move(from), std::move(to), duration_seconds,
        easing_function{easing_value},
        interpolation_function<value_type>{interpolation::linear}};
}

template <typename value_type, typename interpolator_type,
          typename easing_type = easing::linear_fn>
    requires detail::interpolator_object<interpolator_type, value_type> &&
             detail::easing_object<easing_type>
MATHEMATICS_NODISCARD constexpr tween<value_type>
make_tween(value_type from, value_type to, float duration_seconds,
           interpolator_type interpolator,
           easing_type easing_value = {}) noexcept {
    return tween<value_type>{
        std::move(from), std::move(to), duration_seconds,
        easing_function{easing_value},
        interpolation_function<value_type>{interpolator}};
}

static_assert(std::is_trivially_copyable_v<interpolation_function<float>>);

} // namespace math

#endif // MATHEMATICS_TWEEN_HPP
