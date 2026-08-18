#ifndef DYNAMICCPP_EXPORTS
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

//static functions
Texture* Texture::CreateFromPixels(_In_ uint32 width, _In_ uint32 height,
	_In_ std::string_view name, _In_ DXGI_FORMAT textureFormat,
	_In_reads_bytes_(rowPitch* height) const void* pixels, _In_opt_ size_t rowPitch)
{
	if (0 == width || 0 == height || nullptr == pixels) return nullptr;

	DirectX::ScratchImage image;
	if (FAILED(image.Initialize2D(textureFormat, width, height, 1, 1))) return nullptr;

	const DirectX::Image* destination = image.GetImage(0, 0, 0);
	if (nullptr == destination) return nullptr;

	// 원본 행 간격을 안 주면 빈틈없이 채워진 것으로 본다.
	//
	// ★ 행 단위로 옮긴다. ScratchImage의 행 간격은 정렬 때문에 원본보다 클 수
	//   있고, 그때 통째로 memcpy하면 그림이 한 행씩 밀려 비스듬해진다 —
	//   1x1에서는 안 드러나고 폭이 커지는 순간 나타나는 부류다.
	const size_t bitsPerPixel = DirectX::BitsPerPixel(textureFormat);
	if (0 == bitsPerPixel) return nullptr;

	const size_t packedPitch = (static_cast<size_t>(width) * bitsPerPixel + 7u) / 8u;
	const size_t sourcePitch = (0 != rowPitch) ? rowPitch : packedPitch;
	const size_t copyBytes = (std::min)(sourcePitch, destination->rowPitch);

	const auto* source = static_cast<const uint8_t*>(pixels);
	for (uint32 y = 0; y < height; ++y)
	{
		std::memcpy(destination->pixels + static_cast<size_t>(y) * destination->rowPitch,
			source + static_cast<size_t>(y) * sourcePitch, copyBytes);
	}

	Texture* texture = new Texture();
	texture->m_name = std::string(name);
	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(width), float(height) };

	// 파일 로더가 남기는 자리와 같다 — DX12 캐시가 여기서 가져간다(T1·T4).
	texture->m_cpuPixels = std::make_shared<DirectX::ScratchImage>(std::move(image));

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

			// DXGI_FORMAT_BC1_UNORM (== DXT1)
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

			metadata = compressedImage.GetMetadata(); // ��Ÿ������ ����
			image = std::move(compressedImage); // ����� �̹����� ��ü
		}
	}

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
	// 압축까지 끝난 최종 이미지를 DX12가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::make_shared<DirectX::ScratchImage>(std::move(image));

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

			// DXGI_FORMAT_BC1_UNORM (== DXT1)
			Win32::ThrowIfFailed(
				DirectX::Compress(
					image.GetImages(),
					image.GetImageCount(),
					image.GetMetadata(),
					DXGI_FORMAT_BC1_UNORM,
					TEX_COMPRESS_SRGB | TEX_COMPRESS_DITHER | TEX_COMPRESS_UNIFORM,
					0.5f,
					compressedImage
				)
			);

			metadata = compressedImage.GetMetadata(); // ��Ÿ������ ����
			image = std::move(compressedImage); // ����� �̹����� ��ü
		}
	}

	auto texture = std::make_shared<Texture>();

	// ★ DX11 SRV 생성 제거 (T6) - 위 LoadFormPath의 주석과 같은 이유다.

	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(image.GetMetadata().width),float(image.GetMetadata().height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	// 압축까지 끝난 최종 이미지를 DX12가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::make_shared<DirectX::ScratchImage>(std::move(image));

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

			metadata = compressedImage.GetMetadata(); // ��Ÿ������ ����
			image = std::move(compressedImage); // ����� �̹����� ��ü
		}
	}

	auto texture = std::make_unique<Texture>();

	// ★ DX11 SRV 생성 제거 (T6) - 위 LoadFormPath의 주석과 같은 이유다.

	texture->m_textureType = TextureType::ImageTexture;
	texture->m_size = { float(metadata.width),float(metadata.height) };
	texture->m_isTextureAlpha = !image.IsAlphaAllOpaque();
	// 압축까지 끝난 최종 이미지를 DX12가 가져가도록 남긴다(T1).
	// 예전에는 여기서 버렸고 DX12는 방금 만든 DX11 텍스처에서 되읽었다.
	texture->m_cpuPixels = std::make_shared<DirectX::ScratchImage>(std::move(image));

	return texture;
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

float2 Texture::GetImageSize() const
{
	// ★ 예전에는 m_sizeRatio로 나눴다 (T6에서 정리).
	//   그 비율은 '화면의 1/N 해상도로 따라가는 렌더 타깃'을 위한 것이었고,
	//   화면 추종 정책과 함께 사라졌다. 파일에서 읽은 텍스처의 비율은 늘
	//   1이었으므로 이 함수가 돌려주던 값은 그대로다.
	return m_size;
}

#endif // !DYNAMICCPP_EXPORTS
