import std;
import "Creator.WinAPI.h";
import "Creator.GraphicsSDK.h";
import "Creator.GameInputSDK.h";
import winrt_base;
import Creator.AppBackend.WinAPI;

bool VerifyExternalSdkHeaders();
bool VerifyComPointerOwnership();

namespace
{
    struct Receiver
    {
        int calls{};
        LRESULT Receive(HWND, WPARAM value, LPARAM)
        {
            ++calls;
            return static_cast<LRESULT>(value + 1);
        }
    };
}

int main()
{
    static_assert(WM_CLOSE == 0x0010);
    static_assert(GAMEINPUT_API_VERSION == 3);
    static_assert(std::is_base_of_v<IUnknown, GameInput::v3::IGameInput>);
    static_assert(std::is_base_of_v<Core::IWindowBackend, AppBackendWinAPI>);
    static_assert(!std::is_copy_constructible_v<AppBackendWinAPI>);
    ce::platform::windows::apartment_scope apartment{ ce::platform::windows::apartment_model::mta };
    if (apartment.result < 0)
    {
        return 1;
    }

    // A native COM object exercises WinRT's pointer without a GPU or WinRT projection type.
    winrt::com_ptr<IStream> stream;
    if (CreateStreamOnHGlobal(nullptr, TRUE, stream.put()) < 0)
    {
        return 2;
    }
    auto identity = stream.as<IUnknown>();
    winrt::com_ptr<IUnknown> queried;
    if (stream->QueryInterface(__uuidof(IUnknown), queried.put_void()) < 0 || identity.get() != queried.get())
    {
        return 3;
    }
    winrt::com_ptr<ID3D12Device> device;
    if (device || !VerifyComPointerOwnership())
    {
        return 4;
    }

    HWND destroyedWindow{};
    int created{};
    int initialized{};
    Receiver receiver;
    AppBackendWinAPI::RegisterCreateEventHandler([&created](HWND, WPARAM, LPARAM) -> LRESULT
    {
        if (const auto* backend = AppBackendWinAPI::GetForCurrentInstance())
        {
            if (backend->GetWidth() == 64)
            {
                ++created;
            }
        }
        return 0;
    });
    {
        WindowsWindowDesc desc;
        desc.title = L"Creator backend module probe";
        desc.clientWidth = 64;
        desc.clientHeight = 64;
        desc.centerOnDesktop = false;
        desc.showOnCreate = false;
        auto backend = std::make_unique<AppBackendWinAPI>(GetModuleHandleW(nullptr), desc);
        auto* observed = backend.get();
        destroyedWindow = observed->GetHandle();
        CoreWindow window(std::move(backend));
        if (!IsWindow(destroyedWindow) || IsWindowVisible(destroyedWindow) || created != 1)
        {
            return 5;
        }
        observed->InitializeTask([&initialized] { ++initialized; });
        observed->RegisterHandler(WM_APP + 17, &receiver, &Receiver::Receive);
        if (SendMessageW(destroyedWindow, WM_APP + 17, 41, 0) != 42 || receiver.calls != 1 || initialized != 1)
        {
            return 6;
        }
        observed->DetachCallbacks();
        SendMessageW(destroyedWindow, WM_APP + 17, 41, 0);
        if (receiver.calls != 1 || window.GetNativeHandle().kind != Core::NativeWindowKind::Windows)
        {
            return 7;
        }
    }
    AppBackendWinAPI::RegisterCreateEventHandler({});
    if (IsWindow(destroyedWindow) || AppBackendWinAPI::GetForCurrentInstance())
    {
        return 8;
    }
    AppBackendWinAPI::SetUnattended(true);
    if (!AppBackendWinAPI::IsUnattended())
    {
        return 9;
    }
    AppBackendWinAPI::SetUnattended(false);
    if (AppBackendWinAPI::IsUnattended())
    {
        return 10;
    }
    return VerifyExternalSdkHeaders() ? 0 : 11;
}
