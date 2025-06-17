#include "Texture.h"
#include "DeviceState.h"
#include "ResourceAllocator.h"

std::function<void(Texture*)> TextureHelper::deleter = [](Texture* texture)
{
    if (texture)
    {
        ResourceAllocator::GetInstance()->DeallocateTexture(texture);
    }
};

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

	return AllocateResource<Texture>(texture, name, TextureType::Texture2D, textureDesc);
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

    return AllocateResource<Texture>(texture, name, TextureType::TextureCube, textureDesc);
}


Texture* Texture::CreateArray(uint32 width, uint32 height, const std::string_view& name, DXGI_FORMAT textureFormat, uint32 bindFlags, uint32 arrsize, D3D11_SUBRESOURCE_DATA* data)
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
	textureDesc.ArraySize = arrsize;

	ID3D11Texture2D* texture;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateTexture2D(
			&textureDesc, data, &texture
		)
	);

    return AllocateResource<Texture>(texture, name, TextureType::TextureArray, textureDesc);
}

Texture* Texture::LoadFormPath(_In_ const file::path& path)
{
	file::path matPath = PathFinder::RelativeToMaterial(path.string());
	if (!file::exists(path) && !file::exists(matPath))
	{
		return nullptr;
	}

	file::path preparePath{};
	if (file::exists(matPath))
	{
		preparePath = matPath;
	}
	else
	{
		preparePath = path;
	}

	ScratchImage image{};
	TexMetadata metadata{};

    Benchmark banch3;
	if (path.extension() == ".dds")
	{
		//load dds
		DirectX11::ThrowIfFailed(
			LoadFromDDSFile(
				preparePath.c_str(),
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
				preparePath.c_str(),
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
				preparePath.c_str(),
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
				preparePath.c_str(),
				WIC_FLAGS_NONE,
				&metadata,
				image
			)
		);
	}

    Texture* texture = AllocateResource<Texture>();

	DirectX11::ThrowIfFailed(
		CreateShaderResourceView(
			DeviceState::g_pDevice,
			image.GetImages(),
			image.GetImageCount(),
			image.GetMetadata(),
			&texture->m_pSRV
		)
	);

	texture->m_textureType = TextureType::ImageTexture;
	texture->size = { float(metadata.width),float(metadata.height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();

	return texture;
}

Texture::Texture(ID3D11Texture2D* texture, const std::string_view& name, TextureType type, CD3D11_TEXTURE2D_DESC desc) :
	m_pTexture(texture),
	m_name(name),
	m_textureType(type),
	m_desc(desc)
{
	DirectX::SetName(m_pTexture, name);
	m_onReleaseHandle = OnResizeReleaseEvent.AddRaw(this, &Texture::ResizeRelease);
	m_onResizeHandle = OnResizeEvent.AddRaw(this, &Texture::ResizeViews);
}

Texture::Texture(Texture&& texture) noexcept
{
	m_pTexture = texture.m_pTexture;
	m_pSRV = texture.m_pSRV;
	m_pDSV = texture.m_pDSV;
	m_pRTVs = std::move(texture.m_pRTVs);
	m_name = std::move(texture.m_name);
	m_textureType = texture.m_textureType;
	m_desc = texture.m_desc;
	if (texture.m_onReleaseHandle.IsValid())
	{
		OnResizeReleaseEvent -= texture.m_onReleaseHandle;
	}

	if (texture.m_onResizeHandle.IsValid())
	{
		OnResizeEvent -= texture.m_onResizeHandle;
	}
	m_onReleaseHandle = OnResizeReleaseEvent.AddRaw(this, &Texture::ResizeRelease);
	m_onResizeHandle = OnResizeEvent.AddRaw(this, &Texture::ResizeViews);

	texture.m_pTexture = nullptr;
	texture.m_pSRV = nullptr;
	texture.m_pDSV = nullptr;
	texture.m_pRTVs.clear();
	texture.m_textureType = TextureType::Unknown;
	texture.m_desc = CD3D11_TEXTURE2D_DESC();
	texture.m_onReleaseHandle = Core::DelegateHandle();
	texture.m_onResizeHandle = Core::DelegateHandle();
}

Texture::~Texture()
{
	Memory::SafeDelete(m_pTexture);
	Memory::SafeDelete(m_pSRV);
	Memory::SafeDelete(m_pDSV);
	Memory::SafeDelete(m_pUAV);
	for (auto& rtv : m_pRTVs)
	{
		Memory::SafeDelete(rtv);
	}
	m_pRTVs.clear();

	if (m_onReleaseHandle.IsValid())
	{
		OnResizeReleaseEvent -= m_onReleaseHandle;
	}

	if (m_onResizeHandle.IsValid())
	{
		OnResizeEvent -= m_onResizeHandle;
	}
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
	m_srvDesc = srvDesc;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateShaderResourceView(
			m_pTexture, &srvDesc, &m_pSRV
		)
	);

	DirectX::SetName(m_pSRV, m_name + "SRV");
	m_hasSRV = true;
}

void Texture::ResizeSRV()
{
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateShaderResourceView(
			m_pTexture, &m_srvDesc, &m_pSRV
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

	m_rtvDescs.push_back(rtvDesc);

	ID3D11RenderTargetView* rtv;
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateRenderTargetView(
			m_pTexture, &rtvDesc, &rtv
		)
	);
	DirectX::SetName(rtv, m_name + "RTV");
	m_pRTVs.push_back(rtv);
	m_hasRTV = true;
	m_rtvCount = static_cast<uint32>(m_pRTVs.size());
}

void Texture::ResizeRTV(uint32 index)
{
	CD3D11_RENDER_TARGET_VIEW_DESC rtvDesc = m_rtvDescs[index];

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

			m_rtvDescs.push_back(rtvDesc);

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
	m_hasRTV = true;
	m_rtvCount = static_cast<uint32>(m_pRTVs.size());
}

void Texture::ResizeCubeRTVs()
{
	for (uint32 i = 0; i < m_rtvCount; ++i)
	{
		ResizeRTV(i);
	}
}

void Texture::CreateDSV(_In_ DXGI_FORMAT textureFormat)
{
	CD3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc
	{
		D3D11_DSV_DIMENSION_TEXTURE2D,
		textureFormat
	};

	m_dsvDesc = dsvDesc;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilView(
			m_pTexture, &dsvDesc, &m_pDSV
		)
	);

	DirectX::SetName(m_pDSV, m_name + "DSV");

	m_hasDSV = true;
}

void Texture::ResizeDSV()
{
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateDepthStencilView(
			m_pTexture, &m_dsvDesc, &m_pDSV
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

	m_uavDesc = uavDesc;

	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateUnorderedAccessView(
			m_pTexture, &uavDesc, &m_pUAV
		)
	);

	DirectX::SetName(m_pUAV, m_name + "UAV");

	m_hasUAV = true;
}

void Texture::ResizeUAV()
{
	DirectX11::ThrowIfFailed(
		DeviceState::g_pDevice->CreateUnorderedAccessView(
			m_pTexture, &m_uavDesc, &m_pUAV
		)
	);
	DirectX::SetName(m_pUAV, m_name + "UAV");
}

ID3D11RenderTargetView* Texture::GetRTV(uint32 index)
{
	return m_pRTVs[index];
}

float2 Texture::GetImageSize() const
{
	return size;
}

void Texture::ResizeViews(_In_ uint32 width, _In_ uint32 height)
{
	if (m_textureType == TextureType::ImageTexture)
	{
		return;
	}

	switch (m_textureType)
	{
	case TextureType::Texture2D:
		Resize2DViews(width / sizeRatio.x, height / sizeRatio.y);
		break;
	case TextureType::TextureCube:
		ResizeCubeViews(width / sizeRatio.x); // ť��� width = height
		break;
	case TextureType::TextureArray:
		ResizeArrayViews(width / sizeRatio.x, height / sizeRatio.y);
		break;
	default:
		break;
	}

}

void Texture::Resize2DViews(_In_ uint32 width, _In_ uint32 height)
{
	DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateTexture2D(&m_desc, nullptr, &m_pTexture));
	DirectX::SetName(m_pTexture, m_name);

	size = { (float)width, (float)height };

	if (m_hasSRV) ResizeSRV();
	if (m_hasRTV)
	{
		for (uint32 i = 0; i < m_rtvCount; ++i)
		{
			ResizeRTV(i);
		}
	}
	if (m_hasDSV) ResizeDSV();
	if (m_hasUAV) ResizeUAV();
}

void Texture::ResizeCubeViews(_In_ uint32 size)
{
	const uint32 mipLevels = 1;

	DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateTexture2D(&m_desc, nullptr, &m_pTexture));
	DirectX::SetName(m_pTexture, m_name);

	if (m_hasSRV) ResizeSRV();
	if (m_hasRTV) ResizeCubeRTVs();
	if (m_hasDSV) ResizeDSV();
	if (m_hasUAV) ResizeUAV();
}

void Texture::ResizeArrayViews(_In_ uint32 width, _In_ uint32 height)
{
	DirectX11::ThrowIfFailed(DeviceState::g_pDevice->CreateTexture2D(&m_desc, nullptr, &m_pTexture));
	DirectX::SetName(m_pTexture, m_name);

	size = { (float)width, (float)height };

	if (m_hasSRV) ResizeSRV();
	if (m_hasRTV)
	{
		for (uint32 i = 0; i < m_rtvCount; ++i)
		{
			ResizeRTV(i);
		}
	}
	if (m_hasDSV) ResizeDSV();
	if (m_hasUAV) ResizeUAV();
}

void Texture::ResizeRelease()
{
	if (m_textureType == TextureType::ImageTexture)
	{
		return;
	}

	Memory::SafeDelete(m_pTexture);
	Memory::SafeDelete(m_pSRV);
	Memory::SafeDelete(m_pDSV);
	Memory::SafeDelete(m_pUAV);

	for (auto& rtv : m_pRTVs) 
	{
		Memory::SafeDelete(rtv);
	}
	m_pRTVs.clear();
}


