#pragma once
#include "TypeDefinition.h"
#include "ClassProperty.h"
#include "EngineResourceCensus.h"
#include "MetaPolymorphic.h"
#include "Delegate.h"
// m_assetId의 HashedGuid·make_guid()가 여기서 온다 — 전이 include에 기대지 않는다.
#include "TypeTrait.h"
// ★ 여기 있던 <d3d11.h> + <DirectXTex.h> 를 걷었다(축 A).
//
//   그 둘은 m_cpuPixels 가 DirectX::ScratchImage 였기 때문에 있었고,
//   DirectXTex.h 가 __d3d11_h__ 가 켜진 채로 읽혀야 한다는 규약 때문에
//   d3d11.h 를 앞세워야 했다. 결과는 Texture.h 를 읽는 모든 TU 가 —
//   Vulkan 백엔드 구현 파일까지 — DX11 헤더를 컴파일하는 것이었다.
//
//   지금 CPU 픽셀은 백엔드 중립 타입이 든다. 디코더와 BC 압축기는 여전히
//   DirectXTex 이지만 Texture.cpp 안에만 있다.
#include "TextureImage.h"
#include <mathematics/vector2.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <functional>
#include <type_traits>

//-----------------------------------------------------------------------------
// Texture: 자산의 CPU 자료
//
// ★ DX11 표면을 통째로 걷어냈다 (T6, 2026-08-08).
//
//   여기 있던 것: ID3D11Texture2D·SRV·RTV·DSV·UAV 멤버와 그 뷰 생성 함수 여덟,
//   DX11 리소스를 직접 만드는 Create* 계열 열둘(2D·큐브·배열·화면 추종),
//   화면 크기 추종 정책(FollowScreenSize·ApplyScreenSize·Resize* 다섯), 그리고
//   그것들을 감싸던 TextureHelper.
//
//   ★ 마지막에는 전부 자기참조였다. 뷰 생성 호출 21건이 전부 이 헤더 안의
//     인라인 Create* 구현에서 났고, 헤더 밖에서 부르는 곳은 0이었다. 즉
//     '아무도 안 쓰는 것들이 서로를 부르는' 덩어리가 남아 있었다.
//
//   지금 이 타입이 하는 일은 하나다 — 파일에서 읽은 픽셀을 CPU에 들고,
//   텍스처 캐시(DX12·Vulkan)가 그것을 신원(m_assetId)으로 캐싱해 GPU에
//   올린다.
//-----------------------------------------------------------------------------

enum class TextureType
{
	Unknown,
	Texture2D,
	TextureCube,
	TextureArray,
	ImageTexture,
};

class Texture : public meta::polymorphic,
	private Diagnostics::CountedResource<Diagnostics::EngineResource::Texture>
{
public:
	Texture() = default;
	Texture(const Texture&) = delete;
	Texture(Texture&& texture) noexcept;
	~Texture();

	/// CPU 픽셀에서 바로 만든다. 자가 검증이 작은 더미 텍스처를 세울 때 쓴다.
	static Texture* CreateFromPixels(
		_In_ uint32 width,
		_In_ uint32 height,
		_In_ std::string_view name,
		_In_ RHIFormat textureFormat,
		_In_reads_bytes_(rowPitch* height) const void* pixels,
		_In_opt_ size_t rowPitch = 0
	);

	static Texture* LoadFormPath(_In_ const file::path& path, bool isCompress = false);

    // Shares encoded CPU pixels, but owns a distinct GPU cache identity when
    // the sampling format changes. Never mutates a texture used by another role.
    static std::shared_ptr<Texture> WithColorSpace(
        const std::shared_ptr<Texture>& source, bool srgb);

	static std::shared_ptr<Texture> LoadSharedFromPath(
		const file::path& path, bool isCompress = false);

	// I2-E 후속 — 컨테이너 안에 묻힌 이미지(glb 임베디드 등)를 디스크에 뽑지
	// 않고 바이트에서 바로 만든다. 포맷은 매직으로 가른다(DDS·HDR, 나머지는
	// WIC → 실패 시 TGA). 실패는 nullptr — 예외를 밖으로 내지 않는다.
	// LoadSharedFromPath 와 같은 결과물(압축 정책 포함)을 만든다.
	static std::shared_ptr<Texture> LoadSharedFromMemory(
		std::span<const std::byte> bytes, bool isCompress = false);

	// PHASE 3.75 MBC7 — 이미 디코드된 이미지(mip·array 포함)를 그대로 CPU 픽셀로
	// 삼는다. ModelAssetGeneration의 embedded texture는 generation load가 검증·
	// 디코드해 둔 RGBA8 픽셀이라 파일 로더를 다시 태울 이유가 없다 — 여기서
	// 두 번째 디코드가 생기면 generation의 SHA-256 검증이 뜻을 잃는다. 이미지가
	// 비었으면 nullptr.
	static std::shared_ptr<Texture> CreateSharedFromImage(
		std::string_view name, TextureImage image);

	// ── 디코드 전용 창구 (축 A) ──
	//
	// 인코딩된 이미지 바이트를 RGBA8 중립 이미지로 푼다. 압축(BC)이면 풀고,
	// 색공간 전달 함수는 건드리지 않는다 — 레이아웃만 RGBA8로 맞춘다.
	//
	// ★ 이 함수가 있는 이유는 ModelAssetGeneration 의 cook 경로다. 그쪽은
	//   "바이트 → 중립 RGBA8"만 필요한데 그것 하나 때문에 DirectXTex 의
	//   Convert·Decompress·IsSRGB 를 직접 불렀다. 디코더를 갈아 끼우려면
	//   봐야 할 파일이 둘이 되는 자리였고, 지금은 Texture.cpp 하나다.
	static bool DecodeToRgba8(std::span<const std::byte> bytes,
		TextureImage& outImage, std::string& outFailure);

	static std::unique_ptr<Texture> LoadManagedFromPath(
		const file::path& path, bool isCompress = false);

	// ── 업로드용 CPU 픽셀 (PHASE 3-1 재정의, T1 · 축 A) ──
	//
	// 파일에서 읽어 압축까지 끝낸 최종 이미지다. 예전에는 이것으로 DX11 SRV를
	// 만든 뒤 그냥 버렸고, DX12가 쓸 때는 그 DX11 텍스처에서 되읽었다
	// (DX11 스테이징 복사 → Map → 업로드 링). 파일을 읽는 시점에 이미 손에
	// 있던 픽셀을 GPU까지 갔다가 도로 가져오던 셈이다.
	//
	// ★ 정정(2026-08-08, 자산 상주 관리 ③): 예전에는 DX12가 가져가면서
	//   놓았다(TakeCpuPixels). 그 전제는 "한 번 올리면 캐시가 영원히 들고
	//   있다"였는데, ③(미사용 기반 은퇴)이 그 전제를 깼다.
	//
	//   실측으로 드러났다 — 은퇴한 텍스처를 다시 요청하니 CPU 픽셀이 이미
	//   비어 있어 재업로드가 실패했다(씬 왕복 후 실패 3102건, 화면에 흰색).
	//   T4에서 DX11 폴백까지 걷어낸 뒤라 되읽을 곳도 없었다.
	//
	//   그래서 놓지 않는다. 비용은 실측이 정당화한다: 재질 텍스처 전체가
	//   3.8MB이고, 그 정도면 재업로드 실패보다 훨씬 싸다.
	//
	// ★ 큰 것 하나는 예외다. 4K HDR equirect(128MB)는 IBL 생성 뒤로는
	//   진짜 안 쓰이므로 CPU 사본을 계속 드는 것이 아깝다. 지금은 그대로
	//   두고 관측만 한다 — 은퇴 시점에 함께 놓는 것은 별도 판단이다.
	//
	// shared_ptr인 이유: Texture가 이동되는 경로가 있어 소유권을 하나로
	// 묶어야 하고, unique_ptr이면 그 경로들이 깨진다.
	//
	// ★ 불투명 타입인 이유(축 A). 코덱이 낸 픽셀을 **그 자리에 그대로 둔다**.
	//   컨테이너로 옮기면 로드마다 전량 복사가 한 번 붙는데, 4K HDR equirect
	//   한 장이 128MB 라 그 한 번이 무시할 수 없다(저장소에 19장). 그래서
	//   소유는 코덱 산출물 그대로 들고, 경계로는 뷰만 낸다.
	//
	//   정의가 Texture.cpp 에만 있으므로 이 헤더는 DirectXTex 는 물론
	//   DirectX:: 이름조차 알지 않는다. 디코더를 갈아 끼우는 날 바뀌는 것은
	//   그 struct 의 정의 하나뿐이고 이 선언은 그대로다.
	struct CodecImage;
	std::shared_ptr<CodecImage> m_codecImage;

	/// GPU 업로드용 뷰. 소유권을 넘기지 않는다 — 은퇴 후 재업로드가 같은
	/// 픽셀을 다시 읽어야 한다.
	///
	/// ★ 뷰는 이 Texture 보다 오래 살 수 없다. 소비자 둘(DX12·Vulkan 텍스처
	///   캐시)은 GetOrUpload 안에서 받아 그 안에서 다 쓰고 버린다. 멤버로
	///   들거나 프레임을 넘기면 그 순간 규약이 깨진다.
	TextureImageView GetImageView() const;

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

	TextureType m_textureType = TextureType::Unknown;

	std::string m_name;
	std::string m_extension;

	math::vector2 GetImageSize() const;
	math::vector2 GetSize() const { return m_size; }

	bool IsTextureAlpha() const { return m_isTextureAlpha; }
	void SetTextureAlpha(bool isAlpha) { m_isTextureAlpha = isAlpha; }

	/// 보여 줄 그림이 있는가.
	///
	/// ★ 이 술어가 없어서 에디터 쪽 가드가 `if (texture->m_pSRV)`를 직접 봤다(T2).
	///   질문이 "DX11 뷰가 있나"였는데 알고 싶은 것은 "그림이 있나"였고, 그래서
	///   DX11이 사라지는 순간 그림이 조용히 없어질 자리였다. 지금은 그 DX11 뷰
	///   자체가 없으므로 이쪽이 유일한 답이다.
	bool HasImage() const
	{
		return (0.f < m_size.x && 0.f < m_size.y);
	}

private:
	friend class DataSystem;

	RHIFormat m_samplingFormat{ RHIFormat::Unknown };
	math::vector2 m_size{};
	static_assert(std::is_same_v<decltype(m_size), math::vector2>);
	bool   m_isTextureAlpha{ false };
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

