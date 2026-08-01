#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "TypeDefinition.h"
#include "ClassProperty.h"
#include "EngineResourceCensus.h"
#include "ManagedHeapObject.h"
#include "Delegate.h"
#include "RHI/ScreenSizedResource.h"
#include <d3d11.h>
#include <string_view>
#include <functional>

enum class TextureType
{
	Unknown,
	Texture2D,
	TextureCube,
	TextureArray,
	ImageTexture,
};

class Texture : public Managed::HeapObject,
	private Diagnostics::CountedResource<Diagnostics::EngineResource::Texture>
{
public:
	Texture() = default;
	Texture(ID3D11Texture2D* texture, std::string_view name, TextureType type, CD3D11_TEXTURE2D_DESC desc);
	Texture(const Texture&) = delete;
	Texture(Texture&& texture) noexcept;
	~Texture();
	//texture creator functions (static)
	static Texture* Create(
		_In_ uint32 width, 
		_In_ uint32 height, 
		_In_ std::string_view name, 
		_In_ DXGI_FORMAT textureFormat, 
		_In_ uint32 bindFlags, 
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::UniquePtr<Texture> CreateManaged(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::SharedPtr<Texture> CreateShared(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	/// 창 크기를 따라가는 텍스처.
	///
	/// 크기를 인자로 받지 않는 것이 요점이다. 호출부가 g_ClientRect를 읽어
	/// 넘기면 그건 '만들어진 시점의 크기'라, 창이 바뀌어도 그 값이 그대로
	/// 남는다. 크기를 이쪽이 들고 있으면 만들 때와 따라갈 때가 같은 출처를
	/// 본다.
	static Texture* CreateScreenSized(
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 divisorX = 1,
		_In_opt_ uint32 divisorY = 1
	);

	static Managed::SharedPtr<Texture> CreateSharedScreenSized(
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 divisorX = 1,
		_In_opt_ uint32 divisorY = 1
	);

	static Texture* Create(
		_In_ uint32 ratioX,
		_In_ uint32 ratioY,
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::UniquePtr<Texture> CreateManaged(
		_In_ uint32 ratioX,
		_In_ uint32 ratioY,
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* CreateCube(
		_In_ uint32 size,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 mipLevels = 1,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::UniquePtr<Texture> CreateManagedCube(
		_In_ uint32 size,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 mipLevels = 1,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::SharedPtr<Texture> CreateSharedCube(
		_In_ uint32 size,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_opt_ uint32 mipLevels = 1,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* CreateArray(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_ uint32 arrsize = 3,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Managed::UniquePtr<Texture> CreateManagedArray(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_ uint32 bindFlags,
		_In_ uint32 arrsize = 3,
		_In_opt_ D3D11_SUBRESOURCE_DATA* data = nullptr
	);

	static Texture* LoadFormPath(_In_ const file::path& path, bool isCompress = false);

	static Managed::SharedPtr<Texture> LoadSharedFromPath(
		_In_ const file::path& path, 
		bool isCompress = false
	);

	static Managed::UniquePtr<Texture> LoadManagedFromPath(
		_In_ const file::path& path,
		bool isCompress = false
	);


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
	std::string m_extension;

	float2 GetImageSize() const;

	bool IsTextureAlpha() const
	{
		return m_isTextureAlpha;
	}

	void SetTextureAlpha(bool isAlpha)
	{
		m_isTextureAlpha = isAlpha;
	}

	void ApplyScreenSize(_In_ uint32 width, _In_ uint32 height);

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

	/// 이 텍스처가 창 크기를 따라가게 한다.
	///
	/// 선언하지 않으면 따라가지 않는다. 기본을 '따라감'으로 두면 그림자 맵이나
	/// LUT처럼 화면과 무관한 텍스처가 창 크기가 되는데, 빠뜨렸을 때 그쪽이
	/// 훨씬 나쁘다 — 선언을 빠뜨리면 지금 상태(따라오지 않음)로 남을 뿐이다.
	///
	/// divisor는 화면의 1/N 해상도로 쓰는 버퍼용이다.
	void FollowScreenSize(uint32 divisorX = 1, uint32 divisorY = 1)
	{
		m_screenPolicy.follows = true;
		m_screenPolicy.divisorX = divisorX;
		m_screenPolicy.divisorY = divisorY;
	}

	const ScreenSizePolicy& GetScreenPolicy() const { return m_screenPolicy; }

	float GetWidth() const { return m_desc.Width; }
	float GetHeight() const { return m_desc.Height; }
	float2 GetSize() const { return m_size; }

private:
	float2 m_size{};
	float2 m_sizeRatio{ 1.f, 1.f };

	std::vector<ID3D11RenderTargetView*> m_pRTVs;
	std::vector<CD3D11_RENDER_TARGET_VIEW_DESC> m_rtvDescs;
	DXGI_FORMAT m_format{ DXGI_FORMAT_UNKNOWN };
	CD3D11_TEXTURE2D_DESC m_desc{};
	bool m_hasRTV{ false };
	uint32_t m_rtvCount = 0;
	bool m_isTextureAlpha{ false };

	ScreenSizePolicy m_screenPolicy{};

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
	inline Managed::UniquePtr<Texture> CreateRenderTexture(int width, int height, const std::string name, DXGI_FORMAT format)
	{
		Managed::UniquePtr<Texture> tex = Texture::CreateManaged(
			width, 
			height, 
			name, 
			format, 
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		);
		tex->CreateRTV(format);
		tex->CreateSRV(format);
		tex->CreateUAV(format);

		return tex;
	}

	inline Managed::SharedPtr<Texture> CreateSharedRenderTexture(int width, int height, const std::string name, DXGI_FORMAT format)
	{
		Managed::SharedPtr<Texture> tex = Texture::CreateShared(
			width,
			height,
			name,
			format,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		);
		tex->CreateRTV(format);
		tex->CreateSRV(format);
		tex->CreateUAV(format);

		return tex;
	}

	// ── 화면 크기를 따라가는 변형 ──
	//
	// 크기를 받지 않는다. 호출부가 g_ClientRect를 읽어 넘기던 것이 문제의
	// 절반이었다 — 그 값은 만들어진 순간의 스냅샷이라 창이 바뀌어도 남는다.

	inline Managed::UniquePtr<Texture> CreateScreenRenderTexture(const std::string name,
		DXGI_FORMAT format, uint32_t divisorX = 1, uint32_t divisorY = 1)
	{
		const ScreenSizePolicy policy{ true, divisorX, divisorY };
		Managed::UniquePtr<Texture> tex = Texture::CreateManaged(
			policy.ApplyX(ScreenResizeBus::Get().GetWidth()),
			policy.ApplyY(ScreenResizeBus::Get().GetHeight()),
			name,
			format,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		);
		tex->CreateRTV(format);
		tex->CreateSRV(format);
		tex->CreateUAV(format);
		tex->FollowScreenSize(divisorX, divisorY);

		return tex;
	}

	inline Managed::SharedPtr<Texture> CreateSharedScreenRenderTexture(const std::string name,
		DXGI_FORMAT format, uint32_t divisorX = 1, uint32_t divisorY = 1)
	{
		const ScreenSizePolicy policy{ true, divisorX, divisorY };
		Managed::SharedPtr<Texture> tex = Texture::CreateShared(
			policy.ApplyX(ScreenResizeBus::Get().GetWidth()),
			policy.ApplyY(ScreenResizeBus::Get().GetHeight()),
			name,
			format,
			D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS
		);
		tex->CreateRTV(format);
		tex->CreateSRV(format);
		tex->CreateUAV(format);
		tex->FollowScreenSize(divisorX, divisorY);

		return tex;
	}

	inline Managed::UniquePtr<Texture> CreateScreenDepthTexture(const std::string name,
		uint32_t divisorX = 1, uint32_t divisorY = 1)
	{
		const ScreenSizePolicy policy{ true, divisorX, divisorY };
		Managed::UniquePtr<Texture> tex = Texture::CreateManaged(
			policy.ApplyX(ScreenResizeBus::Get().GetWidth()),
			policy.ApplyY(ScreenResizeBus::Get().GetHeight()),
			name,
			DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		);
		tex->CreateDSV(DXGI_FORMAT_D24_UNORM_S8_UINT);
		tex->CreateSRV(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
		tex->FollowScreenSize(divisorX, divisorY);

		return tex;
	}

	inline Managed::UniquePtr<Texture> CreateDepthTexture(int width, int height, const std::string name)
	{
		Managed::UniquePtr<Texture> tex = Texture::CreateManaged(
			width,
			height,
			name,
			DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		);
		tex->CreateDSV(DXGI_FORMAT_D24_UNORM_S8_UINT);
		tex->CreateSRV(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

		return tex;
	}

	inline Managed::SharedPtr<Texture> CreateSharedDepthTexture(int width, int height, const std::string name)
	{
		Managed::SharedPtr<Texture> tex = Texture::CreateManaged(
			width,
			height,
			name,
			DXGI_FORMAT_R24G8_TYPELESS,
			D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE
		);
		tex->CreateDSV(DXGI_FORMAT_D24_UNORM_S8_UINT);
		tex->CreateSRV(DXGI_FORMAT_R24_UNORM_X8_TYPELESS);

		return tex;
	}
}
#else
class Texture {};
#endif // !DYNAMICCPP_EXPORTS