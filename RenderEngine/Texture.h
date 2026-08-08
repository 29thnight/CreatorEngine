#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "TypeDefinition.h"
#include "ClassProperty.h"
#include "EngineResourceCensus.h"
#include "ManagedHeapObject.h"
#include "Delegate.h"
#include "RHI/ScreenSizedResource.h"
#include <d3d11.h>
#include <DirectXTex.h>
#include <memory>
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

	/// 코드가 만든 픽셀로 텍스처를 만든다. DX11 리소스를 만들지 않는다.
	///
	/// ── 왜 이것이 따로 필요한가 ──
	///
	/// 위 Create는 DX11 텍스처를 만든다. DX12가 그것을 쓰려면 예전에는 캐시가
	/// DX11에서 되읽었는데(스테이징 복사 → Map → 업로드 링), T4가 그 폴백을
	/// 걷었다. 걷은 판단은 옳다 — 도달할 수 없는 경로는 죽었는지 살았는지 알
	/// 수 없고, 지형(T5)이 DX11 배열 텍스처를 밀어 넣는 손쉬운 길이 되어
	/// T6을 막는다.
	///
	/// ★ 다만 T4가 소비자를 셀 때 자가 검증 하네스를 빠뜨렸다. 하네스는
	///   Texture::Create로 만든 1x1 텍스처를 캐시에 넘기는데, 그 경로는
	///   m_cpuPixels를 채우지 않는다. 그래서 T4 뒤로 하네스 텍스처가 전부
	///   흰색 폴백이 됐고, dx12.decal이 그것을 '셰이더 블렌드가 어긋났다'로
	///   오진했다(확산 1.0000).
	///
	/// 그 자리를 메우는 것이 이 함수다. 파일 로더와 같은 자리에 CPU 픽셀을
	/// 남기므로 DX12가 그대로 가져간다. DX11 텍스처를 만들지 않는 것이 핵심이다
	/// — 하네스는 그것을 쓰지 않고, 안 만들면 T6이 DX11 멤버를 걷을 때
	/// 이 경로가 걸림돌이 되지 않는다.
	///
	/// rowPitch가 0이면 width * (포맷 바이트)로 본다. 실패하면 nullptr.
	static Texture* CreateFromPixels(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ DXGI_FORMAT textureFormat,
		_In_reads_bytes_(rowPitch* height) const void* pixels,
		_In_opt_ size_t rowPitch = 0
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

	// ── DX12 직결 업로드용 CPU 픽셀 (PHASE 3-1 재정의, T1) ──
	//
	// 파일에서 읽어 압축까지 끝낸 최종 이미지다. 예전에는 이것으로 DX11 SRV를
	// 만든 뒤 그냥 버렸고, DX12가 쓸 때는 그 DX11 텍스처에서 되읽었다
	// (DX11 스테이징 복사 → Map → 업로드 링). 파일을 읽는 시점에 이미 손에
	// 있던 픽셀을 GPU까지 갔다가 도로 가져오던 셈이다.
	//
	// ★ DX12가 가져가면 즉시 놓는다(TakeCpuPixels). 그래서 이 사본은 첫 DX12
	//   사용 전까지만 산다 — 재질 텍스처는 프레임 안에서 쓰이므로 사실상
	//   로딩 직후 사라지고, 아무도 안 쓰는 텍스처(에디터 전용 아이콘 등)만
	//   남는다. 그 잔량은 DX12TextureCache의 통계로 관찰한다.
	//
	// shared_ptr인 이유: Texture가 복사·이동되는 경로가 있어 소유권을 하나로
	// 묶어야 하고, unique_ptr이면 그 경로들이 전부 깨진다.
	std::shared_ptr<DirectX::ScratchImage> m_cpuPixels;

	/// DX12가 가져간다. 돌려준 뒤에는 비어 있다 — 두 번 올리지 않는다.
	std::shared_ptr<DirectX::ScratchImage> TakeCpuPixels()
	{
		return std::move(m_cpuPixels);
	}

	// ── 자산 신원 (PHASE 3-1 재정의, 자산 상주 관리 ①) ──
	//
	// GPU 캐시(DX12TextureCache)가 이 값을 키로 쓴다. 예전에는 Texture*
	// 원시 포인터가 키였는데, 자산 수명이 shared_ptr 공동 소유라(2-2~2-5)
	// 언제 어느 스레드에서 죽는지 정해져 있지 않고, 죽은 뒤 같은 주소에
	// 새 자산이 올라오면 캐시가 이전 것의 GPU 데이터를 돌려줬다.
	//
	// ★ 그 오염은 조용하다 — 검증 레이어가 잡지 않고 화면에 '가끔 다른
	//   텍스처'로만 나타난다. 신원으로 키를 잡으면 성립 자체가 불가능하다.
	//
	// Mesh::m_hashingMesh와 같은 규약이다(그쪽은 이미 이렇게 되어 있었다).
	HashedGuid m_assetId{ make_guid() };

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

		// 진단 명부에 올린다. 따라가야 하는데 안 따라간 것을 이름으로 짚을 수
		// 있어야 한다 — 렌더 타깃은 대부분 중간 결과라 화면에 직접 보이지 않는다.
		ScreenSizedRegistry::Get().Register(this, m_name,
			[this]() { return std::make_pair(
				static_cast<uint32_t>(m_desc.Width), static_cast<uint32_t>(m_desc.Height)); });
	}

	const ScreenSizePolicy& GetScreenPolicy() const { return m_screenPolicy; }

	float GetWidth() const { return m_desc.Width; }
	float GetHeight() const { return m_desc.Height; }
	float2 GetSize() const { return m_size; }

	/// 화면에 보여 줄 그림이 있는가 (PHASE 3-1 재정의, T2).
	///
	/// ★ 이 술어가 없어서 에디터 쪽 가드가 `if (texture->m_pSRV)`를 직접 봤다.
	///   묻고 싶은 것은 "보여 줄 그림이 있나"인데 물은 것은 "DX11 뷰가 있나"다.
	///   지금은 답이 같아서 드러나지 않지만, DX12 이관이 끝나 DX11 SRV 생성이
	///   사라지는 순간 가드가 통째로 거짓이 되어 인스펙터의 썸네일이 조용히
	///   없어진다 — 컴파일도 되고 경고도 없다. 백엔드를 안 묻는 질문으로 바꾼다.
	///
	/// ★ 크기 출처가 둘인 이유: 파일에서 읽은 텍스처는 m_size만 채우고
	///   (LoadFormPath가 메타데이터에서 넣는다), 렌더 타깃·깊이처럼 코드가
	///   만든 텍스처는 m_desc만 채운다. 둘 중 하나라도 있으면 그림이 있는
	///   것이다. 이 어긋남 자체는 T2의 범위가 아니라 여기서 흡수한다.
	bool HasImage() const
	{
		return (0.f < m_size.x && 0.f < m_size.y)
			|| (0 < m_desc.Width && 0 < m_desc.Height);
	}

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