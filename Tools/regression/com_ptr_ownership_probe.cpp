import std;
import "Creator.WinAPI.h";
import winrt_base;
import Creator.Platform.Windows;

namespace
{
    struct References
    {
        ULONG count{1};
        unsigned destroyed{};
        HRESULT queryFailure{S_OK};
    };

    class CountedUnknown final : public IUnknown
    {
    public:
        explicit CountedUnknown(References& references) : references_(references) {}

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** output) override
        {
            *output = nullptr;
            if (FAILED(references_.queryFailure))
            {
                return references_.queryFailure;
            }
            if (id == __uuidof(IUnknown))
            {
                *output = static_cast<IUnknown*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++references_.count;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const auto remaining = --references_.count;
            if (remaining == 0)
            {
                ++references_.destroyed;
                delete this;
            }
            return remaining;
        }

    private:
        References& references_;
    };
}

bool VerifyComPointerOwnership()
{
    References references;
    {
        winrt::com_ptr<IUnknown> owner;
        owner.attach(new CountedUnknown(references));
        winrt::com_ptr<IUnknown> retained;
        retained.copy_from(owner.get());
        auto copied = retained;
        auto moved = std::move(copied);
        if (references.count != 3 || copied || !moved)
        {
            return false;
        }
        retained.copy_from(retained.get());
        auto* transferred = moved.detach();
        if (moved || references.count != 3)
        {
            return false;
        }
        winrt::com_ptr<IUnknown> attached;
        attached.attach(transferred);
        retained = nullptr;
        if (references.count != 2)
        {
            return false;
        }

        References replaced;
        retained.attach(new CountedUnknown(replaced));
        if (FAILED(ce::platform::windows::QueryComInterface(owner, retained)) ||
            retained.get() != owner.get() || replaced.destroyed != 1 || references.count != 3)
        {
            return false;
        }

        winrt::com_ptr<IStream> unsupported;
        // Query failure must leave an empty output and return the original code.
        if (ce::platform::windows::QueryComInterface(owner, unsupported) != E_NOINTERFACE ||
            unsupported || references.count != 3)
        {
            return false;
        }

        references.queryFailure = E_OUTOFMEMORY;
        if (ce::platform::windows::QueryComInterface(owner, retained) != E_OUTOFMEMORY ||
            retained || references.count != 2)
        {
            return false;
        }
        references.queryFailure = S_OK;

        References overwritten;
        winrt::com_ptr<IUnknown> output;
        output.attach(new CountedUnknown(overwritten));
        if (FAILED(owner->QueryInterface(IID_PPV_ARGS(output.put()))) ||
            overwritten.destroyed != 1 || references.count != 3)
        {
            return false;
        }
    }
    return references.count == 0 && references.destroyed == 1;
}
