#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../Utility_Framework/Core.Minimal.h"

enum class TextureType
{
	Unknown,
	Texture2D,
	TextureCube,
	TextureArray,
	ImageTexture,
};

class Texture
{
public:
	Texture() = default;
	Texture(ID3D11Texture2D* texture, const std::string_view& name, TextureType type, CD3D11_TEXTURE2D_DESC desc);
	Texture(const Texture&) = delete;
	Texture(Texture&& texture) noexcept;
	~Texture();
	//texture creator functions (static)
	static Texture* Create(
		_In_ uint32 width, 
		_In_ uint32 height, 
		_In_ const std::string_view& name, 
		_In_ DXGI_FORMAT textureFormat, 
		_In_ uint32 bindFlags, 
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* Create(
		_In_ uint32 ratioX,
		_In_ uint32 ratioY,
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ const std::string_view& name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* CreateCube(
		_In_ uint32 size,
		_In_ const std::string_view& name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 mipLevels = 1,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* CreateArray(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ const std::string_view& name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_ uint32 arrsize = 3,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* LoadFormPath(_In_ const file::path& path, bool isCompress = false);

	void CreateSRV(
		_In_ DXGI_FORMAT textureFormat,
		_In_opt_ D3D11_SRV_DIMENSION viewDimension = D3D11_SRV_DIMENSION_TEXTURE2D,
		_In_opt_ uint32 mipLevels = 1
	);

	void ResizeSRV();

	void CreateRTV(_In_ DXGI_FORMAT textureFormat);

	void ResizeRTV(uint32 index);

	void CreateCubeRTVs(
		_In_ DXGI_FORMAT textureFormat,
		_In_opt_ uint32 mipLevels = 1
	);

	void ResizeCubeRTVs();

	void CreateDSV(_In_ DXGI_FORMAT textureFormat);

	void ResizeDSV();

	void CreateUAV(_In_ DXGI_FORMAT textureFormat);

	void ResizeUAV();

	ID3D11RenderTargetView* GetRTV(uint32 index = 0);

	ID3D11Texture2D* m_pTexture{};
	TextureType m_textureType = TextureType::Unknown;
	ID3D11ShaderResourceView* m_pSRV{};
	CD3D11_SHADER_RESOURCE_VIEW_DESC m_srvDesc{};

	ID3D11DepthStencilView* m_pDSV{};
	CD3D11_DEPTH_STENCIL_VIEW_DESC m_dsvDesc{};

	ID3D11UnorderedAccessView* m_pUAV{};
	CD3D11_UNORDERED_ACCESS_VIEW_DESC m_uavDesc{};

	bool m_hasSRV{ false };
	bool m_hasDSV{ false };
	bool m_hasUAV{ false };

	std::string m_name;

	float2 GetImageSize() const;

	bool IsTextureAlpha() const
	{
		return m_isTextureAlpha;
	}

	void SetTextureAlpha(bool isAlpha)
	{
		m_isTextureAlpha = isAlpha;
	}

	void ResizeViews(_In_ uint32 width, _In_ uint32 height);

	void Resize2DViews(_In_ uint32 width, _In_ uint32 height);
	void ResizeCubeViews(_In_ uint32 size);
	void ResizeArrayViews(_In_ uint32 width, _In_ uint32 height);

	void ResizeRelease();

	void SetSize(float2 size) {
		m_size = size;
		m_desc.Width = static_cast<uint32>(m_size.x / m_sizeRatio.x);
		m_desc.Height = static_cast<uint32>(m_size.y / m_sizeRatio.y);
	}

	void SetSizeRatio(float2 ratio) 
	{
		m_sizeRatio = ratio;
		m_desc.Width = static_cast<uint32>(m_size.x / m_sizeRatio.x);
		m_desc.Height = static_cast<uint32>(m_size.y / m_sizeRatio.y);
	}

	float GetWidth() const { return m_desc.Width; }
	float GetHeight() const { return m_desc.Height; }

private:
	float2 m_size{};
	float2 m_sizeRatio{ 1.f, 1.f };

	std::vector<ID3D11RenderTargetView*> m_pRTVs;
	std::vector<CD3D11_RENDER_TARGET_VIEW_DESC> m_rtvDescs;
	DXGI_FORMAT m_format{ DXGI_FORMAT_UNKNOWN };
	CD3D11_TEXTURE2D_DESC m_desc{};
	bool m_hasRTV{ false };
	uint32 m_rtvCount = 0;
	bool m_isTextureAlpha{ false };

	Core::DelegateHandle m_onReleaseHandle{};
	Core::DelegateHandle m_onResizeHandle{};
};

class TextureManager : public Singleton<TextureManager>
{
private:
	friend class Singleton;
	TextureManager() = default;
	~TextureManager() = default;

public:
	Core::Delegate<void> OnTextureReleaseEvent{};
	Core::Delegate<void, uint32, uint32> OnTextureResizeEvent{};
};

static auto& OnResizeReleaseEvent = TextureManager::GetInstance()->OnTextureReleaseEvent;
static auto& OnResizeEvent = TextureManager::GetInstance()->OnTextureResizeEvent;

namespace TextureHelper
{
    extern std::function<void(Texture*)> deleter;

	inline std::unique_ptr<Texture, decltype(deleter)> CreateRenderTexture(int width, int height, const std::string name, DXGI_FORMAT format)
	{
		Texture* tex = Texture::Create(
			width, 
			height, 
			name, 
			format, 
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		);
		tex->CreateRTV(format);
		tex->CreateSRV(format);
		tex->CreateUAV(format);

        return std::unique_ptr<Texture, decltype(deleter)>(tex, deleter);
	}

	inline std::unique_ptr<Texture, decltype(deleter)> CreateDepthTexture(int width, int height, const std::string name)
	{
		Texture* tex = Texture::Create(
			width,
			height,
			name,
			DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		);
		tex->CreateDSV(DXGI_FORMAT_D24_UNORM_S8_UINT);
		tex->CreateSRV(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

		return std::unique_ptr<Texture, decltype(deleter)>(tex, deleter);
	}
}

using UniqueTexturePtr = std::unique_ptr<Texture, decltype(TextureHelper::deleter)>;

#ifndef TEXTURE_NULL_INITIALIZER
#define TEXTURE_NULL_INITIALIZER UniqueTexturePtr(nullptr, TextureHelper::deleter)
#endif

inline UniqueTexturePtr MakeUniqueTexturePtr(Texture* texture)
{
    return UniqueTexturePtr(texture, TextureHelper::deleter);
}
#endif // !DYNAMICCPP_EXPORTS