# 텍스처 CPU 이미지와 저작 코덱 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 경계의 현재 형태

이 문서는 과거의 «미착수 RHIImageView 설계»를 대체한다. 현재 공개 타입은 `TextureImageDescription`, `TextureSubimage`, `TextureImageView`, `TextureImage`, `Texture::CodecImage`다. RHI 전용 자원 타입으로 옮기는 설계가 아니라 GPU에 올리기 전 자산 데이터의 중립 경계다. 현재 파이프라인은 [TexturePipelinePlan](../plans/TexturePipelinePlan.md)에 정리한다.

`TextureImageDescription`은 format, width/height, mipLevels, arraySize, isCube만 가진 작은 값이다. 픽셀 포인터를 포함하지 않으므로 CPU bulk가 퇴거해도 descriptor 조회는 유효하다. `TextureSubimage`는 소유하지 않는 pixels, rowPitch, slicePitch와 치수를 가진다. `TextureImageView`를 보관하려면 그 바이트 및 subresource 표의 owner가 반드시 살아 있어야 한다. view 복사는 수명을 연장하지 않는다.

## 소유권과 함수 계약

`Texture::CodecImage`는 neutral owned 이미지 또는 저작 어댑터가 유지하는 SDK 저장소와 subresource 표를 감싼다. format/치수/alpha/color-space 정보, optional image key, accounting 정보를 보유한다. 저작 파생 객체는 DirectXTex 픽셀을 원래 할당에 유지할 수 있어 SDK 결과 전체를 중립 컨테이너로 복사할 필요가 없다. Player는 cooked/procedural 데이터를 소유하는 중립 base를 사용한다.

`Texture::GetImageView(imageOwner)`에는 명시적으로 획득한 image owner를 전달한다. 호출자가 description만 가지고 있다고 픽셀도 상주한다고 가정하지 않는다. render frame은 `TextureFramePins`로 description과 image owner를 함께 유지하고 RHI upload는 그 준비된 view를 소비한다. 외부 owner가 남아 있으면 캐시에서 제거되어도 이미지가 살아 있다.

`CopyImageRows`는 행별로 source/destination pitch를 고려한다. 단순한 width×height memcpy는 BC 블록 또는 upload alignment에서 올바르지 않다. subresource index는 item×mipLevels+mip다. view의 치수·pitch·subresource 개수 검증과 소유권 유지는 별개의 조건이다.

## DirectXTex가 있는 곳과 없는 곳

`Editor/EngineEntry/TextureSourceProcessing.cpp`와 오프라인 `TextureCooker.cpp`가 원본 해독·변환을 담당한다. Editor와 AssetCooker 프로젝트가 이 translation unit을 링크한다. `AssetAuthoringPort`에는 source loader, mip generator, RGBA decoder, terrain reader callback이 있다. Editor/오프라인 도구가 설치하고 제거하며, Player가 설치하지 않은 handler는 null/false로 실패한다. 런타임에 원본 SDK fallback을 암묵적으로 만드는 구조가 아니다.

`Texture.cpp`는 strict CECT decoder로 중립 TextureImage를 만들 수 있다. 이 바이트 형식은 RHIFormat 어휘를 사용하지만 DirectXTex나 DXGI 객체를 직렬화하지 않는다. RHI에서 물어야 할 것은 준비된 포맷·서브리소스·pitch이며 원본 경로나 importer 상태가 아니다. source-loading API 잔여가 있다는 사실과 Player에 원본 decoder 구현이 있다는 주장을 구분해야 한다.

## 압축과 백엔드 수용

BC1/BC3/BC5/BC7 등의 cook 선택지는 원본 변환 정책이고, RHI가 그 형식을 업로드·샘플링하는 능력은 별도다. CECT는 데스크톱 baseline으로 base BC 치수의 4배수 정렬을 요구한다. 과거 문서에 있던 특정 Vulkan BC1 흰색 화면은 당시 결함의 동기다. 현재 공통 어휘와 decoder가 존재한다는 사실만으로 실제 Vulkan 장비에서 모든 포맷이 통과했다고 결론내리지 않는다.

회귀 검증은 ① header/table 손상 거부 ② 동일 artifact의 description·image 재획득 ③ owner pin을 유지한 eviction/reload ④ 각 backend에서 mip·array/cube·sRGB 샘플링 결과를 분리한다. 별도 검증 시 소스 및 binary hash, 장비, 설정, 입력 artifact와 출력 비교를 남겨야 한다.

## 유지해야 할 미래 요구

외부 코덱을 바꿀 수 있는 경계는 형성되어 있지만 교체 구현이나 모바일 포맷 지원을 의미하지 않는다. 새로운 코덱은 동일한 immutable image·pitch·metadata 계약과 cooker fingerprint 갱신을 따라야 한다. decoder 내부 scratch와 allocator overhead까지 포함하는 실제 메모리 측정, 두 backend의 다층/블록 형식 동등성, 유효하지 않은 view를 RHI에 전달하기 전의 방어는 계속 검증해야 할 요구다.

## 구현 근거

- [Engine/RenderEngine/TextureImage.h](../../Engine/RenderEngine/TextureImage.h)
- [Engine/RenderEngine/TextureCodecImage.h](../../Engine/RenderEngine/TextureCodecImage.h)
- [Engine/RenderEngine/Texture.h](../../Engine/RenderEngine/Texture.h)
- [Engine/RenderEngine/Texture.cpp](../../Engine/RenderEngine/Texture.cpp)
- [Engine/RenderEngine/TextureFramePins.h](../../Engine/RenderEngine/TextureFramePins.h)
- [Engine/RenderEngine/Interfaces/AssetAuthoringPort.h](../../Engine/RenderEngine/Interfaces/AssetAuthoringPort.h)
- [Editor/EngineEntry/TextureSourceProcessing.cpp](../../Editor/EngineEntry/TextureSourceProcessing.cpp)
- [Editor/Editor.vcxproj](../../Editor/Editor.vcxproj)
- [Tools/AssetCooker/AssetCooker.vcxproj](../../Tools/AssetCooker/AssetCooker.vcxproj)
- [Engine/RenderEngine/Experiment/Cooked/CookedTexture.h](../../Engine/RenderEngine/Experiment/Cooked/CookedTexture.h)
