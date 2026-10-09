#pragma once

// Standalone source probes bypass Scene's lifecycle dispatcher. Keep the GC
// root alive outside this guard, and discharge engine cleanup even if a probe
// assertion throws. This guard is a borrow, not an additional storage owner.
namespace gcce_probe
{
    template<class T>
    class cleanup final
    {
    public:
        explicit cleanup(T& value) noexcept : value_(value) {}
        cleanup(const cleanup&) = delete;
        cleanup& operator=(const cleanup&) = delete;
        ~cleanup() { value_.FinalizeManagedDestroy(); }

    private:
        T& value_;
    };
}
