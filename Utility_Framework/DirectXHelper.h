#pragma once
#include "Core.Definition.h"
#include <ppltasks.h>

namespace DirectX11
{
    constexpr D3D_SHADER_MACRO ShaderMacroTerminator = { nullptr, nullptr };

    class ComException : public std::exception
    {
    public:
        ComException() = default;
        const char* what() const override
        {
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X", result);
            return s_str;
        }

        static std::exception CreateException(HRESULT hr)
        {
            return ComException(hr);
        }

    private:
        ComException(HRESULT hr) : result(hr) {}

    private:
        HRESULT result;
    };

    inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            throw DirectX11::ComException::CreateException(hr);
        }
    }

    inline Concurrency::task<std::vector<byte>> ReadDataAsync(const file::path& fileName)
    {
        using namespace Concurrency;

        auto folder = file::current_path();
        auto path = folder / fileName;

        return create_task([path]() -> std::vector<byte> {

            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                throw std::runtime_error("Failed to open the file.");
            }

            std::streamsize fileSize = file.tellg();
            if (fileSize <= 0) {
                throw std::runtime_error("File is empty or size is invalid.");
            }
            file.seekg(0, std::ios::beg);

            std::vector<byte> buffer(static_cast<size_t>(fileSize));
            if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize))
            {
                throw std::runtime_error("Failed to read the file.");
            }

            return buffer;
            });
    }

    inline float ConvertDipsToPixels(float dips, float dpi)
    {
        static const float dipsPerInch = 96.0f;
        return floorf(dips * dpi / dipsPerInch + 0.5f);
    }

    template <typename T>
    inline void ReleaseResources(std::vector<T*>& resources, T*& pResourceView)
    {
        if (0 < resources.size())
        {
            for (auto& resource : resources)
            {
                if (resource)
                {
                    resource->Release();
                    resource = nullptr;
                }
            }
        }
        else
        {
            if (pResourceView)
            {
                pResourceView->Release();
                pResourceView = nullptr;
            }
        }
    }

    class SharedMap final
    {
    public:
        SharedMap(ID3D11DeviceContext* pDeviceContext, ID3D11Resource* pResource, D3D11_MAPPED_SUBRESOURCE* pMappedResource, uint32 subresource = 0, D3D11_MAP mapType = D3D11_MAP_WRITE_DISCARD, uint32 mapFlags = 0)
            : m_pDeviceContext(pDeviceContext), m_pResource(pResource), m_Subresource(subresource)
        {
            m_pDeviceContext->Map(m_pResource, m_Subresource, mapType, mapFlags, pMappedResource);
        }
        ~SharedMap()
        {
            m_pDeviceContext->Unmap(m_pResource, m_Subresource);
        }

    private:
        ID3D11DeviceContext* m_pDeviceContext;
        ID3D11Resource* m_pResource;
        uint32 m_Subresource;
    };

#if defined(_DEBUG)
    inline bool SdkLayersAvailable()
    {
        HRESULT hr = D3D11CreateDevice(
            nullptr,
            D3D_DRIVER_TYPE_NULL,
            0,
            D3D11_CREATE_DEVICE_DEBUG,
            nullptr,
            0,
            D3D11_SDK_VERSION,
            nullptr,
            nullptr,
            nullptr
        );

        return SUCCEEDED(hr);
    }
#endif
}

namespace DirectX
{
    inline HRESULT CreateTGATextureFormFile(ID3D11Device* pDevice, const wchar_t* pTexturePath, ID3D11ShaderResourceView** ppTexture)
    {
        HRESULT hResult = S_OK;
        ScratchImage image;
        TexMetadata metadata;

        hResult = LoadFromTGAFile(pTexturePath, &metadata, image);
        if (FAILED(hResult))
        {
            return hResult;
        }

        return CreateShaderResourceView(pDevice, image.GetImages(), image.GetImageCount(), metadata, ppTexture);
    }

    inline HRESULT CreateTGATextureFormFileEx(_In_ ID3D11Device* d3dDevice,
        _In_z_ const wchar_t* szFileName,
        _In_ size_t maxsize,
        _In_ D3D11_USAGE usage,
        _In_ unsigned int bindFlags,
        _In_ unsigned int cpuAccessFlags,
        _In_ unsigned int miscFlags,
        _In_ CREATETEX_FLAGS loadFlags,
        _Outptr_opt_ ID3D11Resource** texture,
        _Outptr_opt_ ID3D11ShaderResourceView** textureView) noexcept
    {
		if(!textureView)
        {
            return E_INVALIDARG;
        }
		if (!texture)
		{
			return E_INVALIDARG;
		}
		if (!szFileName)
		{
			return E_INVALIDARG;
		}
		if (!d3dDevice)
		{
			return E_INVALIDARG;
		}
		HRESULT hr = S_OK;
		// Create texture
		ScratchImage image;
		TexMetadata metadata;
		hr = LoadFromTGAFile(szFileName, &metadata, image);
		if (FAILED(hr))
		{
			return hr;
		}
		hr = CreateTextureEx(d3dDevice, image.GetImages(), image.GetImageCount(), metadata, usage, bindFlags, cpuAccessFlags, miscFlags, loadFlags, texture);
		if (FAILED(hr))
		{
			return hr;
		}
		hr = CreateShaderResourceView(d3dDevice, image.GetImages(), image.GetImageCount(), metadata, textureView);
		if (FAILED(hr))
		{
			if (texture)
			{
				(*texture)->Release();
				*texture = nullptr;
			}
			return hr;
		}
		return hr;
    }

    inline HRESULT CreateTextureFromFile(ID3D11Device* pDevice, const file::path& fileName, ID3D11ShaderResourceView** ppTextureView)
    {
        HRESULT hResult = S_OK;
        auto extension = fileName.extension();

        if (extension == L".tga")
        {
            hResult = CreateTGATextureFormFile(pDevice, fileName.c_str(), ppTextureView);
        }
        else if (extension == L".dds")
        {
            hResult = CreateDDSTextureFromFile(pDevice, fileName.c_str(), nullptr, ppTextureView);
        }
        else
        {
            hResult = CreateWICTextureFromFile(pDevice, fileName.c_str(), nullptr, ppTextureView);
        }

        return hResult;
    }

    inline HRESULT CreateTextureFromFile(ID3D11Device* pDevice, const file::path& fileName, ID3D11Resource** ppTexture, ID3D11ShaderResourceView** ppTextureView)
    {
        HRESULT hResult = S_OK;
        auto extension = fileName.extension();

        if (extension == L".tga")
        {
            hResult = CreateTGATextureFormFile(pDevice, fileName.c_str(), ppTextureView);
        }
        else if (extension == L".dds")
        {
            hResult = CreateDDSTextureFromFile(pDevice, fileName.c_str(), ppTexture, ppTextureView);
        }
        else
        {
            hResult = CreateWICTextureFromFile(pDevice, fileName.c_str(), ppTexture, ppTextureView);
        }

        return hResult;
    }

    template <typename T>
    concept DXObjects = requires(T t)
    {
        t->SetPrivateData(WKPDID_D3DDebugObjectName, 0, nullptr);
    };
    // 디버깅을 지원하려면 개체에 이름을 할당하세요.
#if defined(_DEBUG)
    inline void SetName(DXObjects auto pObject, const std::string_view& name)
    {
        pObject->SetPrivateData(WKPDID_D3DDebugObjectName, (uint32)name.length(), name.data());
    }
#else
    inline void SetName(DXObjects auto, const std::string_view&)
    {
    }
#endif
    static inline DXGI_FORMAT NoSRGB(DXGI_FORMAT fmt)
    {
        switch (fmt)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:   return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:   return DXGI_FORMAT_B8G8R8X8_UNORM;
        default:                                return fmt;
        }
    }
}
