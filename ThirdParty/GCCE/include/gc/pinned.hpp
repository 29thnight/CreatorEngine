#pragma once

#include "refs.hpp"

#include <type_traits>

namespace gc
{
// Keeps an object alive while raw pointers to it are used where references
// cannot go, typically by worker threads:
//
//     gc::pinned<mesh> pin(m);              // owner thread, before the job
//     jobs.run([p = pin.get()] { use(*p); });
//     jobs.wait();
//     // pin goes out of scope on the owner thread, after the job finished
//
// A pin is a labelled root ("gc.pinned"), movable but not copyable, so each
// borrow has one owner that decides when it ends. Create, move and destroy
// it on the owner thread. It protects memory only: the borrowers still need
// their own synchronization with anything that changes the object.
template <class T>
class pinned
{
public:
    pinned() noexcept = default;

    template <strong_ref R>
        requires std::is_convertible_v<decltype(std::declval<const R&>().get()), T*>
    explicit pinned(const R& ref) : root_(ref)
    {
        root_.set_label("gc.pinned");
    }

    pinned(pinned&& other) : root_(std::move(other.root_)) { root_.set_label("gc.pinned"); }
    pinned& operator=(pinned&& other)
    {
        root_ = std::move(other.root_);
        root_.set_label("gc.pinned");
        return *this;
    }
    pinned(const pinned&) = delete;
    pinned& operator=(const pinned&) = delete;

    // Ends the borrow early.
    void release() noexcept { root_.reset(); }

    [[nodiscard]] T* get() const noexcept { return root_.get(); }
    T& operator*() const noexcept { return *get(); }
    T* operator->() const noexcept { return get(); }
    explicit operator bool() const noexcept { return static_cast<bool>(root_); }

private:
    root_ref<T> root_;
};
} // namespace gc
