#pragma once

// Shared definition text for the named module and the explicit parser/probe
// adapter. Windows SDK declarations are supplied by those entry points.
namespace ce::platform::windows::detail
{
    enum class apartment_model
    {
        mta, // Engine-owned main, presentation and render threads.
        sta, // Message-pumping UI hosts, currently ProfilerViewer.
        current_or_mta // Borrowed decoding calls: respect existing STA/MTA.
    };

    // Pool, submission, socket and file-watcher threads have no implicit WinRT
    // capability. A COM/WinRT job must acquire a scope before using such APIs.
    // No apartment-affine result may escape to another thread without marshaling.
    // Construct before apartment-bound objects; destroy after their release and
    // worker shutdown, on the same thread. Never copy or transfer this scope.
    // A successful RoInitialize (including S_FALSE) owns exactly one uninitialize.
    // HRESULT failure remains a failure; callers choose their existing error path.
    class apartment_scope
    {
    public:
        explicit apartment_scope(apartment_model model) noexcept
            : result(initialize(model)), initialized_(SUCCEEDED(result))
        {
        }

        ~apartment_scope() noexcept
        {
            reset();
        }

        apartment_scope(const apartment_scope&) = delete;
        apartment_scope& operator=(const apartment_scope&) = delete;
        apartment_scope(apartment_scope&&) = delete;
        apartment_scope& operator=(apartment_scope&&) = delete;

        // Only for an explicit host teardown boundary after all users release.
        void reset() noexcept
        {
            if (initialized_)
            {
                initialized_ = false;
                RoUninitialize();
            }
        }

        const HRESULT result;

    private:
        static HRESULT initialize(apartment_model model) noexcept
        {
            if (model == apartment_model::current_or_mta)
            {
                APTTYPE type{};
                APTTYPEQUALIFIER qualifier{};
                const HRESULT queried = CoGetApartmentType(&type, &qualifier);
                if (queried == CO_E_NOTINITIALIZED)
                {
                    model = apartment_model::mta;
                }
                else if (FAILED(queried))
                {
                    return queried;
                }
                else if (type == APTTYPE_STA || type == APTTYPE_MAINSTA)
                {
                    model = apartment_model::sta;
                }
                else if (type == APTTYPE_MTA)
                {
                    model = apartment_model::mta;
                }
                else
                {
                    // Do not silently reinterpret a neutral apartment as MTA.
                    return RPC_E_CHANGED_MODE;
                }
            }
            return RoInitialize(model == apartment_model::sta
                ? RO_INIT_SINGLETHREADED : RO_INIT_MULTITHREADED);
        }

        bool initialized_;
    };
}
