# 텍스처 파이프라인: 저작 설정에서 불변 런타임 세대까지

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 현재 계약과 범위

텍스처 원본 해독·리사이즈·밉 생성·BC 압축은 Editor 및 오프라인 cooker의 책임이다. 런타임은 검증된 CECT 바이트를 읽고 GPU 업로드에 필요한 중립 이미지 뷰를 제공한다. DirectXTex는 현재 저작 구현의 의존성이며 Player가 원본 이미지 SDK를 설치해 복구하는 경로는 없다. 중립 경계의 상세 계약은 [CPU 이미지 경계](../design/TextureCodecBoundaryDesign.md)에 둔다.

이 문서가 이전 T0/T1a/T2 진행률과 날짜별 추가 설명을 대체한다. 구현이 존재한다는 사실은 모든 포맷·백엔드·품질 조합의 수용 완료를 뜻하지 않는다. 현재 제품 대상은 데스크톱 DX12/Vulkan cooked 이미지이며 ASTC, 모바일/Web, GPU 인코더는 구현 범위 밖이다.

## 1. 입력, 설정과 자산 신원

`BuildTextureCookProduct`는 sourcePath와 assetRoot를 받아 원본·sidecar를 함께 캡처한다. 허용 원본은 `.png`, `.hdr`, `.dds`, `.jpg`, `.jpeg`다. 컨테이너 매직도 확장자와 일치해야 하며 `.jpeg`는 JPEG로 정규화한다. 디코더가 더 많은 컨테이너를 해독할 수 있다는 이유로 제품 import allowlist가 넓어지지는 않는다. 원본 루트 탈출, sidecar 누락, 비정규 UUIDv4, 빈 파일, 미지원 확장자는 실패한다. 공급된 captureSource가 실패하면 다른 디스크 읽기로 우회하지 않는다.

신원은 `.meta`의 GUID다. 경로·이름·확장자를 GUID 생성의 대용으로 쓰지 않는다. 출력에는 textureAssetId, artifactPath/bytes, manifestEntry, 원본·메타 SHA-256, importRecipe가 포함된다. producer는 바이트를 반환할 뿐 파일 게시를 하지 않는다.

`TextureImportSettings`의 기본값은 Source 색 공간, None 추가 압축, PreserveAuthored, maxDimension=0, normalMap=false, preserveAlphaCoverage=false, alphaCutoff=0.5, Normal 품질이다. None은 authored DDS 블록을 해제하라는 뜻이 아니다. GenerateFull은 이미 있는 authored mip을 유지하고 부족한 꼬리만 만든다. sampler wrap/filter는 각 텍스처 참조의 정책이며 이미지 import 설정과 분리된다.

파서는 알려진 필드만 기존 기본값 위에 적용하며 실패 시 호출자의 설정을 부분 변경하지 않는다. enum 범위, 최대 16384 치수, 유한 alpha cutoff를 검사한다. alpha coverage 사용 시 cutoff는 (0,1), normal map은 Srgb와 함께 사용할 수 없고 BC5는 normalMap=true를 요구한다. normalMap과 alpha coverage 조합도 거부한다. 형식·필터링·압축 구현을 바꿀 때는 `TextureCookerFingerprint`와 recipe의 캐시 무효화 의미를 함께 검토해야 한다.

## 2. Editor 편집과 저장

Content Browser에서 자산을 선택하면 Inspector가 메타데이터를 읽고 `DrawTextureImportSettingsEditor`가 typed 설정을 그린다. 유효한 값이 바뀌면 `QueueAutomaticTextureSettings`로 저장을 예약한다. 정상 경로는 별도 Save 버튼을 기다리지 않는다. Inspector를 닫거나 선택을 바꿔도 EditorAssetDatabase가 대기열을 소유한다.

같은 sidecar의 연속 편집은 마지막 writer와 시각을 갱신하며 최초 디스크 바이트를 기준으로 잡는다. 400ms 동안 편집이 안정되면 저장한다. 배출 직전 디스크가 기준과 다르면 쓰기를 거부하고 오류를 유지한다. `Reload Disk Settings`는 디스크 값을 draft에 다시 읽고 오류를 지우지만 새 쓰기를 예약하지 않는다. `Retry Import`는 현재 유효한 draft로 typed 재임포트를 다시 요청한다. 사용자는 외부 변경을 보존할지 자신의 값을 다시 적용할지 구별할 수 있어야 한다.

`SetTextureImportSettingsAndReimport`는 Assets 내부의 지원 원본과 canonical UUIDv4 sidecar를 확인하고 알려진 import 필드를 갱신한다. 다른 사용자 필드·GUID를 새 값으로 대체하지 않는다. sidecar 파일 게시 성공과 이후 cook 성공은 별개다. cook이 실패해도 설정 파일은 이미 저장되었을 수 있고, 정상 런타임 세대는 유지된다.

## 3. worker와 게시 경계

1. 원본 및 메타 stamp와 요청 revision을 캡처하고 scheduler 작업을 제출한다. texture cook은 tail 의존성을 통해 직렬화한다
2. `Library/TextureAssetGenerations` 아래에서 내용 주소 기반 세대를 찾는다. 같은 원본·recipe·artifact 형식은 기존 불변 세대를 재사용할 수 있다
3. 새 artifact는 private staging에 기록한 뒤 디렉터리 rename으로 게시한다. 기존 세대가 손상되었으면 완성된 대체본을 준비한 뒤 격리·교체하며 실패 시 복원을 시도한다
4. worker 종료 직전 원본·메타 stamp와 최신 요청 revision을 다시 확인한다. 오래된 작업은 최신 요청 상태를 지우거나 게시하지 않는다
5. `RuntimeTexturePublication`을 `DataSystem::QueueAssetChange`로 넘긴다. owner의 queued asset drain에서 카탈로그를 바꾸고 descriptor 준비를 시작한다

`TextureImportStatus`는 cooking, frame-boundary 대기, publication 실패, superseded, 현재 resolver에 대한 준비, Ready를 구분한다. `TextureImportReady`는 현재 descriptor를 획득할 수 있다는 의미다. CPU image 준비·RHI upload·GPU fence 완료를 모두 포함하는 단일 완료 신호로 사용해서는 안 된다.

## 4. description, CPU pixels와 소비자 수명

`TextureAssetKey`는 논리 자산 참조·blob·resolver revision·variant를 구별한다. `TextureImageKey`는 콘텐츠 hash, 크기, 형식/표현 버전, platform/ABI와 recipe로 실제 이미지 payload의 호환성을 구별한다. role이나 sampler가 다르더라도 바이트가 호환되면 CPU image를 공유할 수 있다.

`TextureAssetOrigin`은 선택된 resolver 결과와 byte source를 pin한다. descriptor와 CPU image는 별도 캐시이며 기본 보유 예산은 각각 4MiB와 64MiB다. 논리 퇴거가 외부 owner, accepted job 또는 이전 프레임의 참조를 강제로 파괴하지 않는다. retainedCharge는 보수적 계산값이며 실제 allocator 또는 GPU 회수 바이트가 아니다. staging, live, in-flight 값도 서로 중복될 수 있어 합산하면 안 된다.

Editor의 `TickAutomaticChanges`는 resolver 변경 뒤 새 descriptor와 CodecImage를 비동기로 준비한다. 둘 다 준비된 이후 owner 스레드에서 ImageComponent, SpriteRenderer, SpriteSheetComponent, DecalComponent 및 MeshRenderer 재질 참조를 교체한다. 프레임 pin 테이블을 복사해 갱신하므로 이미 seal된 프레임은 이전 테이블을 계속 보유한다. 이것은 씬 재로드가 아니며 모든 미래 컴포넌트가 자동 갱신된다는 일반 계약도 아니다. 새 소비자를 추가하면 수집·교체·직렬화 경로를 함께 확장해야 한다.

## 5. CECT와 CPU cook 정책

CECT schema/representation은 현재 2다. 64-byte little-endian header와 40-byte subresource entry를 사용한다. pointer, native struct padding, DXGI enum을 디스크 ABI로 쓰지 않는다. 최대 dimension 16384, array 2048, artifact 1GiB 제한과 정확한 payload 범위를 검증한다. table 전체가 유효한지 확인한 뒤 픽셀 저장소를 할당한다. trailing bytes, 겹치는 범위, 비정상 pitch는 거부한다.

서브리소스 순서는 array item 바깥·mip 안쪽이다. BC base 치수는 4의 배수여야 하며 작은 mip tail은 허용한다. 하드웨어별 unaligned BC 기능을 전제하지 않는다. HDR은 일반 LDR BC 압축·normal·alpha coverage 설정과 동일하게 취급하지 않고 지원하지 않는 조합을 거부한다. cook 품질 enum이 있다고 실제 콘텐츠 품질·시간 목표가 입증되는 것은 아니다.

## 6. 실패를 읽는 순서

- 설정 오류: typed validator와 Inspector 메시지 확인. 디스크 저장 전 거부와 cook 이후 실패를 구분한다
- 디스크 충돌: 자동 저장 오류를 확인하고 Reload Disk Settings 또는 의도적인 재편집을 선택한다
- cook 실패: 원본/sidecar/recipe 및 worker 오류를 확인한다. 기존 정상 세대 보존 여부를 검사한다
- descriptor가 Ready인데 화면이 이전 상태: image 요청, resolver revision, 소비자 교체 및 frame pin을 확인한다
- 저장 후 재열기만 실패: ImageComponent의 texturePaths와 SpriteRenderer의 m_SpritePath를 확인한다. cooked description의 빈 loose 경로를 저장하지 말고 origin GUID로 등록 경로를 복원한다
- 종료 직전 편집: App의 `FlushAutomaticSaves`는 파일 대기열을 배출한다. 이것만으로 모든 GPU 작업과 화면 수용 완료를 주장하지 않는다

## 7. 회귀 검증과 남은 요구

[임포트 수명](../analysis/TextureImportLifecycleValidation20261010.md)은 UI 없는 asset service, [씬 재로드](../analysis/TextureSceneReloadValidation20261010.md)는 실제 비동기 scene 준비, [종단 소비](../analysis/TextureReimportSceneValidation20261010.md)는 실제 컴포넌트·Undo·prefab을 다룬다. [자동 Inspector](../analysis/TextureAutomaticInspectorUiValidation20261010.md), [Release 입력](../analysis/TextureAutomaticInspectorReleaseValidation20261010.md), [디스크 충돌](../analysis/TextureAutomaticInspectorConflictValidation20261010.md)은 별도 UI 검증 경계다.

미해결 수용 요구는 다양한 크기·array/cube·authored mip·alpha/normal/HDR 조합의 품질 및 시간 측정, DX12/Vulkan 실제 소비 비교, 장시간 반복 재임포트와 삭제/rename/종료 경합, 패키지의 cold/warm 로딩 비용이다. T1b의 품질·추가 포맷 판단은 이 측정 후 결정한다. T3 모바일/Web은 제품 타깃과 포맷 계약이 정해질 때까지 보류한다. 예전 날짜별 성공 기록이나 공수 비율을 최신 HEAD의 통과율로 재사용하지 않는다.

## 구현 근거

- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp)
- [Editor/EngineEntry/EditorAssetDatabase.cpp](../../Editor/EngineEntry/EditorAssetDatabase.cpp)
- [Editor/EngineEntry/App.cpp](../../Editor/EngineEntry/App.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureImportSettings.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureImportSettings.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureCookProducer.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureCookProducer.cpp)
- [Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp](../../Engine/RenderEngine/Experiment/Cooked/TextureCooker.cpp)
- [Engine/RenderEngine/Experiment/Cooked/CookedTexture.h](../../Engine/RenderEngine/Experiment/Cooked/CookedTexture.h)
- [Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h](../../Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h)
- [Engine/SceneRuntime/ImageComponent.cpp](../../Engine/SceneRuntime/ImageComponent.cpp)
- [Engine/SceneRuntime/SpriteRenderer.cpp](../../Engine/SceneRuntime/SpriteRenderer.cpp)
