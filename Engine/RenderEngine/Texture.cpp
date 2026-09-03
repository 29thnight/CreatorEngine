#include "Texture.h"
#include "PathFinder.h"
#include "Core.Memory.hpp"
// Win32::ThrowIfFailed가 여기 있다. 유니티 빌드에서는 같은 블롭의 앞선
// 파일이 공급했다.
#include "DirectXHelper.h"
#include <DirectXTex.h>

// 유니티 빌드에서는 같은 블롭의 앞선 파일이 이 using을 공급했다.
// ASan 구성은 블롭을 끄므로 직접 받는다(PHASE 9-9).
using namespace DirectX;

// ── DirectXTex 와 나머지 엔진의 유일한 접점 (축 A) ──────────────────────
//
// 이 파일 위쪽(로더 넷)은 DirectXTex 로 디코드·압축하고, 아래 헬퍼가 그
// 결과를 백엔드 중립 이미지로 옮긴다. 그 지점부터는 저장소 어디에도
// DirectX::ScratchImage 가 없다 — 디코더나 압축기를 갈아 끼우는 날 봐야
// 할 파일은 이것 하나다.
//
// ★ 익명 네임스페이스인데 이름이 긴 이유: 유니티 빌드가 여러 .cpp 를 한
//   TU 로 합치므로 흔한 이름은 옆 파일과 충돌한다.
namespace
{
	/// DirectXTex 디코더·압축기가 실제로 내는 포맷만 담은 표.
	///
	/// ★ 백엔드 대응표(DX12Format.h · VulkanFormat.h)와 성격이 다르다.
	///   저쪽은 "엔진 어휘 <-> API 어휘"이고 이것은 "디코더 어휘 -> 엔진
	///   어휘"다. 축 B 에서 디코더가 바뀌면 이 표는 함께 사라진다.
	RHIFormat TextureDecodedFormatToRHI(DXGI_FORMAT format)
	{
		switch (format)
		{
		case DXGI_FORMAT_R8G8B8A8_UNORM:      return RHIFormat::RGBA8Unorm;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return RHIFormat::RGBA8UnormSrgb;
		case DXGI_FORMAT_B8G8R8A8_UNORM:      return RHIFormat::BGRA8Unorm;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return RHIFormat::BGRA8UnormSrgb;
		case DXGI_FORMAT_R16G16B16A16_FLOAT:  return RHIFormat::RGBA16Float;
		case DXGI_FORMAT_R32G32B32A32_FLOAT:  return RHIFormat::RGBA32Float;
		case DXGI_FORMAT_BC1_UNORM:           return RHIFormat::BC1Unorm;
		case DXGI_FORMAT_BC1_UNORM_SRGB:      return RHIFormat::BC1UnormSrgb;
		case DXGI_FORMAT_BC3_UNORM:           return RHIFormat::BC3Unorm;
		default:                              return RHIFormat::Unknown;
		}
	}

	/// 픽셀을 옮긴다. 포맷이 표에 있다는 것은 호출자가 이미 확인했다.
	bool TextureCopyScratchToCpuImage(const ScratchImage& image, RHIFormat format,
		TextureImage& out)
	{
		const TexMetadata& metadata = image.GetMetadata();
		out = TextureImage::Allocate(format,
			static_cast<uint32_t>(metadata.width),
			static_cast<uint32_t>(metadata.height),
			static_cast<uint32_t>(metadata.arraySize),
			static_cast<uint32_t>(metadata.mipLevels),
			metadata.IsCubemap());
		if (!out.IsValid()) return false;

		for (uint32_t item = 0; item < out.ArraySize(); ++item)
		{
			for (uint32_t mip = 0; mip < out.MipLevels(); ++mip)
			{
				const TextureImage::Subresource* destination = out.Find(mip, item);
				const Image* source = image.GetImage(mip, item, 0);
				if (nullptr == destination || nullptr == source
					|| nullptr == source->pixels) return false;
				std::byte* destinationPixels = out.MutablePixelsAt(*destination);
				if (nullptr == destinationPixels) return false;

				CopyImageRows(destinationPixels, destination->rowPitch,
					reinterpret_cast<const std::byte*>(source->pixels),
					source->rowPitch,
					RHIFormatRowCount(format, destination->height),
					destination->rowPitch);
			}
		}
		return true;
	}

	/// 디코드 결과를 중립 이미지로 옮긴다. 실패하면 nullptr.
	std::shared_ptr<const TextureImage> TextureMakeCpuImage(const ScratchImage& image)
	{
		const TexMetadata& metadata = image.GetMetadata();
		if (TEX_DIMENSION_TEXTURE2D != metadata.dimension
			|| 0 == metadata.width || 0 == metadata.height
			|| metadata.width > UINT32_MAX || metadata.height > UINT32_MAX)
			return nullptr;

		TextureImage result;
		const RHIFormat known = TextureDecodedFormatToRHI(metadata.format);
		if (RHIFormat::Unknown != known)
		{
			if (!TextureCopyScratchToCpuImage(image, known, result)) return nullptr;
			return std::make_shared<const TextureImage>(std::move(result));
		}

		// ★ 표에 없는 포맷은 거절하지 않고 RGBA8 로 내려 받는다.
		//
		//   거절하면 그 자산이 화면에서 통째로 사라진다. 어휘에 이름이
		//   없다는 이유로 그림을 잃는 것보다 한 번 변환하는 편이 낫고,
		//   여기 걸리는 포맷이 실제로 나오면 그것이 어휘에 더할 후보다.
		//   전달 함수는 건드리지 않는다 — 목표의 sRGB 성질을 소스와 같게
		//   두면 DirectXTex 가 감마에 손대지 않는다(아래 DecodeToRgba8 의
		//   주석에 그 사정이 자세히 있다).
		const DXGI_FORMAT lowered = IsSRGB(metadata.format)
			? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
		ScratchImage converted;
		const HRESULT lowering = IsCompressed(metadata.format)
			? Decompress(image.GetImages(), image.GetImageCount(), metadata,
				lowered, converted)
			: Convert(image.GetImages(), image.GetImageCount(), metadata, lowered,
				TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
		if (FAILED(lowering) || 0 == converted.GetImageCount()) return nullptr;

		const RHIFormat loweredFormat = TextureDecodedFormatToRHI(
			converted.GetMetadata().format);
		if (RHIFormat::Unknown == loweredFormat) return nullptr;
		if (!TextureCopyScratchToCpuImage(converted, loweredFormat, result))
			return nullptr;
		return std::make_shared<const TextureImage>(std::move(result));
	}

	/// 인코딩된 바이트를 매직으로 갈라 디코드한다.
	///
	/// DDS·HDR 는 매직이 있고, 나머지는 WIC 가 스스로 판별한다. TGA 는
	/// 매직이 없어 WIC 가 거절한 뒤에만 시도한다.
	bool TextureDecodeImageBytes(std::span<const std::byte> bytes,
		TexMetadata& metadata, ScratchImage& image, bool& outAlreadyFinal)
	{
		outAlreadyFinal = false;
		if (bytes.empty()) return false;

		const auto startsWith = [&bytes](std::string_view magic)
		{
			if (bytes.size() < magic.size()) return false;
			for (size_t index = 0; index < magic.size(); ++index)
			{
				if (static_cast<char>(bytes[index]) != magic[index]) return false;
			}
			return true;
		};

		HRESULT result = E_FAIL;
		if (startsWith("DDS "))
		{
			result = LoadFromDDSMemory(bytes.data(), bytes.size(),
				DDS_FLAGS_FORCE_RGB, &metadata, image);
			outAlreadyFinal = true;
		}
		else if (startsWith("#?RADIANCE") || startsWith("#?RGBE"))
		{
			result = LoadFromHDRMemory(bytes.data(), bytes.size(), &metadata, image);
			outAlreadyFinal = true;
		}
		else
		{
			result = LoadFromWICMemory(bytes.data(), bytes.size(),
				WIC_FLAGS_IGNORE_SRGB, &metadata, image);
			if (FAILED(result))
			{
				result = LoadFromTGAMemory(bytes.data(), bytes.size(),
					TGA_FLAGS_NONE, &metadata, image);
			}
		}
		return SUCCEEDED(result) && 0 != image.GetImageCount();
	}
}

//static functions
Texture* Texture::CreateFromPixels(_In_ uint32 width, _In_ uint32 height,
	_In_ std::string_view name, _In_ RHIFormat textureFormat,
	_In_reads_bytes_(rowPitch* height) const void* pixels, _In_opt_ size_t rowPitch)
{
	if (0 == width || 0 == height || nullptr == pixels) return nullptr;

	// ★ 예전에는 여기서도 ScratchImage 를 세웠다(축 A 전). 만들려는 것이
	//   CPU 픽셀 한 장뿐인데 디코더 라이브러리의 컨테이너를 거칠 이유가
	//   없다 — 지금은 중립 이미지를 직접 잡는다.
	TextureImage image = TextureImage::Allocate(textureFormat, width, height, 1, 1);
	if (!image.IsValid()) return nullptr;

	const TextureImage::Subresource* destination = image.Find(0, 0);
	if (nullptr == destination) return nullptr;
	std::byte* destinationPixels = image.MutablePixelsAt(*destination);
	if (nullptr == destinationPixels) return nullptr;

	// 원본 행 간격을 안 주면 빈틈없이 채워진 것으로 본다.
	//
	// ★ 행 단위로 옮긴다. 대상의 행 간격은 정렬 때문에 원본보다 클 수
	//   있고, 그때 통째로 memcpy하면 그림이 한 행씩 밀려 비스듬해진다 —
	//   1x1에서는 안 드러나고 폭이 커지는 순간 나타나는 부류다.
	const size_t sourcePitch = (0 != rowPitch)
		? rowPitch : static_cast<size_t>(RHIFormatRowPitch(textureFormat, width));

	CopyImageRows(destinationPixels, destination->rowPitch,
		static_cast<const std::byte*>(pixels), sourcePitch,
		RHIFormatRowCount(textureFormat, height), destination->rowPitch);

	Texture* texture = new Texture();
	texture->m_name = std::string(name);
	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(width), float(height) };

	// 파일 로더가 남기는 자리와 같다 — 텍스처 캐시가 여기서 가져간다(T1·T4).
	texture->m_cpuPixels = std::make_shared<const TextureImage>(std::move(image));

	return texture;
}

std::shared_ptr<Texture> Texture::CreateSharedFromCpuImage(
	std::string_view name, std::shared_ptr<const TextureImage> image)
{
	if (!image || !image->IsValid()) return nullptr;

	auto texture = std::shared_ptr<Texture>(new Texture());
	texture->m_name = std::string(name);
	texture->m_textureType = image->IsCube()
		? TextureType::TextureCube
		: (image->ArraySize() > 1 ? TextureType::TextureArray : TextureType::ImageTexture);
	texture->m_size = { float(image->Width()), float(image->Height()) };
	// ★ 예전에는 DirectX::HasAlpha(포맷)를 물었다. 그 물음은 "이 포맷에
	//   알파 채널이 있는가"이지 "이 그림에 투명한 데가 있는가"가 아니다 —
	//   중립 어휘의 8비트 색 포맷은 전부 알파를 가지므로 답이 늘 참이었다.
	//   지금은 채널 수로 같은 답을 준다(뜻이 바뀌지 않는다).
	texture->m_isTextureAlpha = (4 == RHIFormatChannels(image->Format()))
		|| RHIFormatIsBlockCompressed(image->Format());
	texture->m_cpuPixels = std::move(image);
	return texture;
}

Texture* Texture::LoadFormPath(_In_ const file::path& path, bool isCompress)
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
			LoadFromWICFile(
				preparePath.c_str(),
				WIC_FLAGS_IGNORE_SRGB,
				&metadata,
				image
			)
		);
	}
	if(isCompress)
	{
		ScratchImage compressedImage{};
		if (!IsCompressed(metadata.format) && path.extension() != ".hdr" && path.extension() != ".dds")
		{
			DirectX::TexMetadata tempMetadata = metadata;

			// DXGI_FORMAT_BC1_UNORM_SRGB (== DXT1, 감마 디코드 라벨)
			Win32::ThrowIfFailed(
				DirectX::Compress(
					image.GetImages(),
					image.GetImageCount(),
					metadata,
					DXGI_FORMAT_BC1_UNORM_SRGB,
					TEX_COMPRESS_PARALLEL,
					0.5f,
					compressedImage
				)
			);

			metadata = compressedImage.GetMetadata(); // 메타데이터 갱신
			image = std::move(compressedImage); // 압축된 이미지로 교체
		}
	}

	// 중립 이미지로 옮긴 뒤에 자산을 세운다 — 옮기지 못하면 텍스처 자체를
	// 만들지 않는다(픽셀 없는 Texture는 캐시에서 흰색으로 나온다).
	std::shared_ptr<const TextureImage> cpuImage = TextureMakeCpuImage(image);
	if (!cpuImage) return nullptr;

    Texture* texture = new Texture();
	// ★ 여기 있던 DX11 SRV 생성을 걷었다 (T6, 2026-08-08).
	//
	//   이 로더가 만들던 것은 둘이었다 - CPU 픽셀(m_cpuPixels)과 DX11
	//   SRV. 앞의 것은 DX12TextureCache가 읽어 올리고, 뒤의 것은
	//   에디터 썸네일의 폴백 하나가 마지막 소비자였다(EditorImGuiTexture).
	//   그 폴백이 사라지면서 소비자가 0이 됐다.

	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(metadata.width),float(metadata.height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	// 압축까지 끝난 최종 이미지를 캐시가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::move(cpuImage);

	return texture;
}

std::shared_ptr<Texture> Texture::LoadSharedFromPath(const file::path& path, bool isCompress)
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
			LoadFromWICFile(
				preparePath.c_str(),
				WIC_FLAGS_IGNORE_SRGB,
				&metadata,
				image
			)
		);
	}

	if (isCompress)
	{
		ScratchImage compressedImage{};
		if (!IsCompressed(metadata.format) && path.extension() != ".hdr" && path.extension() != ".dds")
		{
			DirectX::TexMetadata tempMetadata = metadata;

			// DXGI_FORMAT_BC1_UNORM_SRGB (== DXT1, 감마 디코드 라벨)
			Win32::ThrowIfFailed(
					// ★ _SRGB 라벨을 단다. isCompress 가 켜지는 자리는 baseColorMap
					// 하나뿐이고(FinalizeMaterialRuntime · MaterialResolver 모두
					// 그 property 에서만 true 를 넘긴다), 그 텍스처의 바이트는
					// sRGB 로 인코딩돼 있다. 예전처럼 BC1_UNORM 으로 라벨하면
					// 샘플러가 감마 디코드를 하지 않아 셰이더가 받는 알베도가
					// sRGB 값 그대로였다 — 밝고 탈색된 그림의 legacy 경로판이다.
					// 아래 TEX_COMPRESS_SRGB 도 같은 전제를 이미 깔고 있었다.
				DirectX::Compress(
					image.GetImages(),
					image.GetImageCount(),
					image.GetMetadata(),
					DXGI_FORMAT_BC1_UNORM_SRGB,
					TEX_COMPRESS_SRGB | TEX_COMPRESS_DITHER | TEX_COMPRESS_UNIFORM,
					0.5f,
					compressedImage
				)
			);

			metadata = compressedImage.GetMetadata(); // 메타데이터 갱신
			image = std::move(compressedImage); // 압축된 이미지로 교체
		}
	}

	std::shared_ptr<const TextureImage> cpuImage = TextureMakeCpuImage(image);
	if (!cpuImage) return nullptr;

	auto texture = std::make_shared<Texture>();

	// ★ DX11 SRV 생성 제거 (T6) - 위 LoadFormPath의 주석과 같은 이유다.

	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(image.GetMetadata().width),float(image.GetMetadata().height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	// 압축까지 끝난 최종 이미지를 캐시가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::move(cpuImage);

	return texture;
}


std::shared_ptr<Texture> Texture::LoadSharedFromMemory(
	std::span<const std::byte> bytes, bool isCompress)
{
	ScratchImage image{};
	TexMetadata metadata{};
	// DDS/HDR 는 LoadSharedFromPath 와 같이 재압축하지 않는다.
	bool alreadyFinal = false;
	if (!TextureDecodeImageBytes(bytes, metadata, image, alreadyFinal)) return nullptr;

	if (isCompress && !alreadyFinal && !IsCompressed(metadata.format))
	{
		ScratchImage compressedImage{};
		// DXGI_FORMAT_BC1_UNORM_SRGB (== DXT1, 감마 디코드 라벨) — LoadSharedFromPath 와 같은 정책.
		if (SUCCEEDED(DirectX::Compress(image.GetImages(), image.GetImageCount(),
			image.GetMetadata(), DXGI_FORMAT_BC1_UNORM_SRGB,
			TEX_COMPRESS_SRGB | TEX_COMPRESS_DITHER | TEX_COMPRESS_UNIFORM,
			0.5f, compressedImage)))
		{
			metadata = compressedImage.GetMetadata();
			image = std::move(compressedImage);
		}
	}

	std::shared_ptr<const TextureImage> cpuImage = TextureMakeCpuImage(image);
	if (!cpuImage) return nullptr;

	auto texture = std::make_shared<Texture>();
	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(image.GetMetadata().width), float(image.GetMetadata().height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	texture->m_cpuPixels = std::move(cpuImage);
	return texture;
}

std::unique_ptr<Texture> Texture::LoadManagedFromPath(const file::path& path, bool isCompress)
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
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
		Win32::ThrowIfFailed(
			LoadFromWICFile(
				preparePath.c_str(),
				WIC_FLAGS_IGNORE_SRGB,
				&metadata,
				image
			)
		);
	}

	if (isCompress)
	{
		ScratchImage compressedImage{};
		if (!IsCompressed(metadata.format) && path.extension() != ".hdr" && path.extension() != ".dds")
		{
			DirectX::TexMetadata tempMetadata = metadata;

			// DXGI_FORMAT_BC1_UNORM (== DXT1)
			Win32::ThrowIfFailed(
				DirectX::Compress(
					image.GetImages(),
					image.GetImageCount(),
					metadata,
					DXGI_FORMAT_BC1_UNORM,
					TEX_COMPRESS_PARALLEL,
					0.5f,
					compressedImage
				)
			);

			metadata = compressedImage.GetMetadata(); // 메타데이터 갱신
			image = std::move(compressedImage); // 압축된 이미지로 교체
		}
	}

	std::shared_ptr<const TextureImage> cpuImage = TextureMakeCpuImage(image);
	if (!cpuImage) return nullptr;

	auto texture = std::make_unique<Texture>();

	// ★ DX11 SRV 생성 제거 (T6) - 위 LoadFormPath의 주석과 같은 이유다.

	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(metadata.width),float(metadata.height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	// 압축까지 끝난 최종 이미지를 캐시가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::move(cpuImage);

	return texture;
}

// ── cook 경로의 디코드 창구 (축 A) ──────────────────────────────────────
//
// ★ 이 함수의 몸통은 ModelAssetGeneration::CopyTexturePixels 에 있던 것이다.
//   그쪽이 필요한 것은 "인코딩된 바이트 -> 중립 RGBA8" 하나였는데, 그것
//   때문에 DirectXTex 의 Convert·Decompress·IsSRGB 를 직접 불렀다. 디코더를
//   갈아 끼우려면 봐야 할 파일이 둘이 되는 자리였다.
bool Texture::DecodeToRgba8(std::span<const std::byte> bytes,
	TextureImage& outImage, std::string& outFailure)
{
	ScratchImage image{};
	TexMetadata metadata{};
	bool alreadyFinal = false;
	if (!TextureDecodeImageBytes(bytes, metadata, image, alreadyFinal))
	{
		outFailure = "image decoder가 픽셀을 만들지 못했다.";
		return false;
	}

	// ── 색공간은 라벨로만 정한다 ──────────────────────────────────
	//
	// ★ 여기 있던 코드는 target 을 semantic(_UNORM_SRGB)으로 잡고
	//   Convert 를 불렀다. DirectXTex 는 **출력 포맷이 IsSRGB 면
	//   SRGB_OUT 이 기본 on** 이라고 스스로 문서화한다
	//   (DirectXTex.h "if the output format type is IsSRGB(), then
	//   SRGB_OUT is on by default"). 입력은 _UNORM 이라 SRGB_IN 이
	//   꺼진 채로, 이미 sRGB 로 인코딩된 PNG 바이트에 linear→sRGB
	//   인코드가 한 번 더 먹었다.
	//
	//   런타임 SRV 는 호출자가 정하는 semantic 포맷을 쓰므로 하드웨어가
	//   디코드를 한 번 한다. 두 연산이 정확히 상쇄돼 **셰이더가 받는
	//   알베도가 sRGB 바이트값 그대로**였다 — Gunner 본체 텍스처
	//   기준 밝기 2.1~3.7 배, 채도비 2.96 → 1.70 (43% 탈색).
	//
	//   고침은 "바이트를 건드리지 않는다"다. 레이아웃만 RGBA8 로
	//   맞추되 목표 포맷의 sRGB 성질을 **소스와 같게** 두면
	//   DirectXTex 가 전달 함수에 손대지 않는다. 최종 라벨은 호출자가
	//   색공간으로 따로 정한다.
	const DXGI_FORMAT layoutTarget = IsSRGB(metadata.format)
		? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
		: DXGI_FORMAT_R8G8B8A8_UNORM;

	ScratchImage converted;
	const ScratchImage* finalImage = &image;
	HRESULT conversion = S_OK;
	if (IsCompressed(metadata.format))
	{
		conversion = Decompress(image.GetImages(), image.GetImageCount(),
			metadata, layoutTarget, converted);
		finalImage = &converted;
	}
	else if (metadata.format != layoutTarget)
	{
		conversion = Convert(image.GetImages(), image.GetImageCount(), metadata,
			layoutTarget, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, converted);
		finalImage = &converted;
	}
	if (FAILED(conversion) || 0 == finalImage->GetImageCount())
	{
		outFailure = "image를 backend-neutral RGBA8로 변환하지 못했다.";
		return false;
	}

	const TexMetadata& finalMetadata = finalImage->GetMetadata();
	if (TEX_DIMENSION_TEXTURE2D != finalMetadata.dimension
		|| 0 == finalMetadata.width || 0 == finalMetadata.height
		|| 0 == finalMetadata.mipLevels || 0 == finalMetadata.arraySize
		|| finalMetadata.width > UINT32_MAX || finalMetadata.height > UINT32_MAX)
	{
		outFailure = "2D texture descriptor 범위를 벗어났다.";
		return false;
	}

	const RHIFormat format = TextureDecodedFormatToRHI(finalMetadata.format);
	if (RHIFormat::Unknown == format
		|| !TextureCopyScratchToCpuImage(*finalImage, format, outImage))
	{
		outFailure = "decoded texture subresource가 비었다.";
		return false;
	}
	return true;
}

// ★ DX11 생성자·뷰 이관을 걷어낸 뒤의 수명 (T6, 2026-08-08).
//
//   예전 이동 생성자는 ID3D11 핸들 다섯과 desc를 옮기고, 화면 리사이즈
//   델리게이트 둘을 해제했다가 새 객체로 다시 걸었다. 그 전부가 DX11
//   자원과 화면 추종 정책의 것이라 함께 사라졌다.
//
//   남은 것은 CPU 자료뿐이고, 그것은 shared_ptr과 값이라 옮기기만 하면 된다.
Texture::Texture(Texture&& texture) noexcept
{
	m_cpuPixels = std::move(texture.m_cpuPixels);
	m_assetId = texture.m_assetId;
	m_textureType = texture.m_textureType;
	m_name = std::move(texture.m_name);
	m_extension = std::move(texture.m_extension);
	m_size = texture.m_size;
	m_isTextureAlpha = texture.m_isTextureAlpha;

	texture.m_textureType = TextureType::Unknown;
	texture.m_size = {};
	texture.m_isTextureAlpha = false;
}

Texture::~Texture() = default;

math::vector2 Texture::GetImageSize() const
{
	// ★ 예전에는 m_sizeRatio로 나눴다 (T6에서 정리).
	//   그 비율은 '화면의 1/N 해상도로 따라가는 렌더 타깃'을 위한 것이었고,
	//   화면 추종 정책과 함께 사라졌다. 파일에서 읽은 텍스처의 비율은 늘
	//   1이었으므로 이 함수가 돌려주던 값은 그대로다.
	return m_size;
}

