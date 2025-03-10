#include "Texture.h"
#include "DeviceState.h"

//static functions
Texture* Texture::Create(_In_ uint32 width, _In_ uint32 height, _In_ const std::string_view& name, _In_ DXGI_FORMAT textureFormat, _In_ uint32 bindFlags, _In_opt_ D3D11_SUBRESOURCE_DATA* data)
{
    CD3D11_TEXTURE2D_DESC textureDesc
    {
		textureFormat,
        width,
		height,
		1,
		1,
		bindFlags,
		D3D11_USAGE_DEFAULT
    };

	ID3D11Texture2D* texture;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateTexture2D(
			&textureDesc, data, &texture
		)
	);

	return new Texture(texture, name);
}

Texture* Texture::CreateCube(_In_ uint32 size, _In_ const std::string_view& name, _In_ DXGI_FORMAT textureFormat, _In_ uint32 bindFlags, _In_opt_ uint32 mipLevels, _In_opt_ D3D11_SUBRESOURCE_DATA* data)
{
	CD3D11_TEXTURE2D_DESC textureDesc
	{
		textureFormat,
		size,
		size,
		6,
		mipLevels,
		bindFlags,
		D3D11_USAGE_DEFAULT,
		0,
		1,
		0,
		D3D11_RESOURCE_MISC_TEXTURECUBE
	};

	ID3D11Texture2D* texture;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateTexture2D(
			&textureDesc, data, &texture
		)
	);

	return new Texture(texture, name);
}

Texture* Texture::LoadFormPath(_In_ const file::path& path)
{
	if (!file::exists(path))
	{
		return nullptr;
	}

	ScratchImage image{};
	TexMetadata metadata{};

	if (path.extension() == ".dds")
	{
		//load dds
		DirectX11::ThrowIfFailed(
			LoadFromDDSFile(
				path.c_str(),
				DDS_FLAGS_FORCE_RGB,
				&metadata,
				image
			)
		);
	}
	else if (path.extension() == ".tga")
	{
		//load tga
		DirectX11::ThrowIfFailed(
			LoadFromTGAFile(
				path.c_str(),
				&metadata,
				image
			)
		);
	}
	else if (path.extension() == ".hdr")
	{
		//load hdr
		DirectX11::ThrowIfFailed(
			LoadFromHDRFile(
				path.c_str(),
				&metadata,
				image
			)
		);
	}
	else
	{
		//load wic
		DirectX11::ThrowIfFailed(
			LoadFromWICFile(
				path.c_str(),
				WIC_FLAGS_NONE,
				&metadata,
				image
			)
		);
	}

	Texture* texture = new Texture;

	DirectX11::ThrowIfFailed(
		CreateShaderResourceView(
			DeviceState::g_pDevice,
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			&texture->m_pSRV
		)
	);

	return texture;
}

//member functions
Texture::Texture(ID3D11Texture2D* texture, const std::string_view& name) :
	m_pTexture(texture),
	m_name(name)
{
	DirectX::SetName(m_pTexture, name);
}

Texture::Texture(Texture&& texture) noexcept
{
	m_pTexture = texture.m_pTexture;
	m_pSRV = texture.m_pSRV;
	m_pDSV = texture.m_pDSV;
	m_pRTVs = std::move(texture.m_pRTVs);
	m_name = std::move(texture.m_name);

	texture.m_pTexture = nullptr;
	texture.m_pSRV = nullptr;
	texture.m_pDSV = nullptr;
	texture.m_pRTVs.clear();
}

Texture::~Texture()
{
	Memory::SafeDelete(m_pTexture);
	Memory::SafeDelete(m_pSRV);
	Memory::SafeDelete(m_pDSV);
	for (auto& rtv : m_pRTVs)
	{
		Memory::SafeDelete(rtv);
	}
	m_pRTVs.clear();
}

void Texture::CreateSRV(_In_ DXGI_FORMAT textureFormat, _In_opt_ D3D11_SRV_DIMENSION viewDimension, _In_opt_ uint32 mipLevels)
{
	CD3D11_SHADER_RESOURCE_VIEW_DESC srvDesc
	{
		viewDimension,
		textureFormat,
		0, 
		mipLevels
	};

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateShaderResourceView(
			m_pTexture, &srvDesc, &m_pSRV
		)
	);

	DirectX::SetName(m_pSRV, m_name + "SRV");
}

void Texture::CreateRTV(_In_ DXGI_FORMAT textureFormat)
{
	CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc
	{
		D3D11_RTV_DIMENSION_TEXTURE2D,
		textureFormat,
	};

	ID3D11RenderTargetView* rtv;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRenderTargetView(
			m_pTexture, &rtvDesc, &rtv
		)
	);
	DirectX::SetName(rtv, m_name + "RTV");
	m_pRTVs.push_back(rtv);
}

void Texture::CreateCubeRTVs(_In_ DXGI_FORMAT textureFormat, _In_opt_ uint32 mipLevels)
{
	for (uint32 mip = 0; mip < mipLevels; ++mip)
	{
		for (uint32 face = 0; face < 6; ++face)
		{
			CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc
			{
				D3D11_RTV_DIMENSION_TEXTURE2DARRAY,
				textureFormat,
				mip,
				face,
				1,
			};

			ID3D11RenderTargetView* rtv;
			DirectX11::ThrowIfFailed(
				DeviceState::g_pDevice->CreateRenderTargetView(
					m_pTexture, &rtvDesc, &rtv
				)
			);

			DirectX::SetName(rtv, m_name + std::to_string(face) + "RTV");
			m_pRTVs.push_back(rtv);
		}
	}
}

void Texture::CreateDSV(_In_ DXGI_FORMAT textureFormat)
{
	CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc
	{
		D3D11_DSV_DIMENSION_TEXTURE2D,
		textureFormat
	};

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilView(
			m_pTexture, &dsvDesc, &m_pDSV
		)
	);

	DirectX::SetName(m_pDSV, m_name + "DSV");
}

void Texture::CreateUAV(DXGI_FORMAT textureFormat)
{
	CD3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc
	{
		D3D11_UAV_DIMENSION_TEXTURE2D,
		textureFormat
	};

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateUnorderedAccessView(
			m_pTexture, &uavDesc, &m_pUAV
		)
	);

	DirectX::SetName(m_pUAV, m_name + "UAV");
}

ID3D11RenderTargetView* Texture::GetRTV(uint32 index)
{
	return m_pRTVs[index];
}
