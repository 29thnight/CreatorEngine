#pragma once

// CPU-only declarations for the small D3D12/DXGI/WRL surface used by the real
// Vulkan shared-image implementation and DX12 range lowering. They deliberately
// do not emulate a Windows driver, GPU execution, or the native COM ABI.
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <utility>

using HRESULT = int32_t;
using UINT = uint32_t;
constexpr HRESULT S_OK = 0;
constexpr HRESULT E_FAIL = -1;
#define FAILED(result) ((result) < 0)
#define IID_PPV_ARGS(address) 0, reinterpret_cast<void**>(address)
constexpr uint32_t GENERIC_ALL = 0x10000000;

enum DXGI_FORMAT
{
    DXGI_FORMAT_UNKNOWN,
    DXGI_FORMAT_R8G8B8A8_UNORM,
    DXGI_FORMAT_D24_UNORM_S8_UINT,
    DXGI_FORMAT_R24G8_TYPELESS,
    DXGI_FORMAT_D32_FLOAT_S8X24_UINT,
    DXGI_FORMAT_R32G8X24_TYPELESS,
    DXGI_FORMAT_D16_UNORM,
    DXGI_FORMAT_D32_FLOAT
};
enum D3D12_RESOURCE_DIMENSION
{
    D3D12_RESOURCE_DIMENSION_BUFFER,
    D3D12_RESOURCE_DIMENSION_TEXTURE2D,
    D3D12_RESOURCE_DIMENSION_TEXTURE3D
};
enum D3D12_RESOURCE_FLAGS
{
    D3D12_RESOURCE_FLAG_NONE = 0,
    D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET = 1,
    D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL = 2
};
enum D3D12_RESOURCE_STATES
{
    D3D12_RESOURCE_STATE_COMMON = 0,
    D3D12_RESOURCE_STATE_COPY_DEST = 1,
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE = 2
};
enum D3D12_TEXTURE_LAYOUT
{
    D3D12_TEXTURE_LAYOUT_UNKNOWN
};
enum D3D12_HEAP_TYPE
{
    D3D12_HEAP_TYPE_DEFAULT
};
enum D3D12_HEAP_FLAGS
{
    D3D12_HEAP_FLAG_SHARED = 1
};
enum D3D_FEATURE_LEVEL
{
    D3D_FEATURE_LEVEL_11_0
};
enum D3D12_RESOURCE_BARRIER_TYPE
{
    D3D12_RESOURCE_BARRIER_TYPE_TRANSITION
};
constexpr uint32_t D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES = UINT32_MAX;
struct D3D12_HEAP_PROPERTIES
{
    D3D12_HEAP_TYPE Type{};
    uint32_t CreationNodeMask{}, VisibleNodeMask{};
};
struct D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension{};
    uint64_t Width{};
    uint32_t Height{};
    uint16_t DepthOrArraySize{}, MipLevels{};
    DXGI_FORMAT Format{};
    struct
    {
        uint32_t Count{}, Quality{};
    } SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout{};
    D3D12_RESOURCE_FLAGS Flags{};
};

namespace ce::interop_boundary
{
    inline uint32_t failStep{};
    inline std::atomic<int> liveObjects{}, liveHandles{};
    inline D3D12_RESOURCE_DESC resourceDescription;
    inline D3D12_HEAP_PROPERTIES resourceHeap;
    inline D3D12_RESOURCE_STATES resourceInitialState{};
    inline LUID adapterLuid{0x04030201u, 0x08070605};

    struct ComObject
    {
        ComObject() { ++liveObjects; }
        virtual ~ComObject() { --liveObjects; }
        void AddRef() { ++references; }
        void Release()
        {
            if (--references == 0)
            {
                delete this;
            }
        }
        int references{1};
    };
} // namespace ce::interop_boundary

struct ID3D12Resource : ce::interop_boundary::ComObject
{
    D3D12_RESOURCE_DESC description;
    D3D12_RESOURCE_DESC GetDesc() const { return description; }
};
struct D3D12_RESOURCE_BARRIER
{
    D3D12_RESOURCE_BARRIER_TYPE Type{};
    struct
    {
        ID3D12Resource* pResource{};
        uint32_t Subresource{};
        D3D12_RESOURCE_STATES StateBefore{}, StateAfter{};
    } Transition;
};
struct IDXGIAdapter1 : ce::interop_boundary::ComObject
{
};
struct IDXGIFactory4 : ce::interop_boundary::ComObject
{
    HRESULT EnumAdapterByLuid(LUID, int, void** result)
    {
        if (ce::interop_boundary::failStep == 2)
        {
            return E_FAIL;
        }
        *result = new IDXGIAdapter1;
        return S_OK;
    }
};
struct ID3D12Device : ce::interop_boundary::ComObject
{
    HRESULT CreateCommittedResource(const D3D12_HEAP_PROPERTIES* heap, D3D12_HEAP_FLAGS,
                                    const D3D12_RESOURCE_DESC* description, D3D12_RESOURCE_STATES initial, const void*,
                                    int, void** result)
    {
        using namespace ce::interop_boundary;
        resourceHeap = *heap;
        resourceDescription = *description;
        resourceInitialState = initial;
        if (failStep == 4)
        {
            return E_FAIL;
        }
        auto* resource = new ID3D12Resource;
        resource->description = *description;
        *result = resource;
        return S_OK;
    }
    HRESULT CreateSharedHandle(ID3D12Resource*, const SECURITY_ATTRIBUTES*, uint32_t, const wchar_t*, HANDLE* result)
    {
        if (ce::interop_boundary::failStep == 5)
        {
            return E_FAIL;
        }
        *result = new uint32_t(1);
        ++ce::interop_boundary::liveHandles;
        return S_OK;
    }
};
inline HRESULT CreateDXGIFactory1(int, void** result)
{
    if (ce::interop_boundary::failStep == 1)
    {
        return E_FAIL;
    }
    *result = new IDXGIFactory4;
    return S_OK;
}
inline HRESULT D3D12CreateDevice(IDXGIAdapter1*, D3D_FEATURE_LEVEL, int, void** result)
{
    if (ce::interop_boundary::failStep == 3)
    {
        return E_FAIL;
    }
    *result = new ID3D12Device;
    return S_OK;
}
inline int CloseHandle(HANDLE handle)
{
    delete static_cast<uint32_t*>(handle);
    --ce::interop_boundary::liveHandles;
    return 1;
}

namespace Microsoft::WRL
{
    template<class T>
    class ComPtr
    {
      public:
        ComPtr() = default;
        ~ComPtr()
        {
            if (m_pointer)
            {
                m_pointer->Release();
            }
        }
        ComPtr(const ComPtr& other) : m_pointer(other.m_pointer)
        {
            if (m_pointer)
            {
                m_pointer->AddRef();
            }
        }
        ComPtr(ComPtr&& other) noexcept : m_pointer(std::exchange(other.m_pointer, nullptr)) {}
        ComPtr& operator=(const ComPtr& other)
        {
            ComPtr copy(other);
            std::swap(m_pointer, copy.m_pointer);
            return *this;
        }
        ComPtr& operator=(ComPtr&& other) noexcept
        {
            ComPtr moved(std::move(other));
            std::swap(m_pointer, moved.m_pointer);
            return *this;
        }
        T* Get() const { return m_pointer; }
        T* operator->() const { return m_pointer; }
        T** operator&() { return &m_pointer; }
        explicit operator bool() const { return m_pointer != nullptr; }

      private:
        T* m_pointer{};
    };
} // namespace Microsoft::WRL
