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

Content Browser에서 자산을 선택하면 Inspector가 메타데이터를 읽고 `DrawTextureImportEditor`가 typed 설정을 그린다. 유효한 값이 바뀌면 `QueueAutomaticTextureSettings`로 저장을 예약한다. 정상 경로는 별도 Save 버튼을 기다리지 않는다. Inspector를 닫거나 선택을 바꿔도 EditorAssetDatabase가 대기열을 소유한다.

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


## master 통합 후속 기록 (2026-10-17)

아래 21~37절은 master `b767a3c67e0f8474792a0a141a4580c5eac15712`에서 보존한 후속 기록이다. 위 본문은 2026-10-15 기준 설명이며, 조건부 게시·그림자 캐시·GPU 형식 등의 후속 수용을 미구현/미검증으로 되돌리지 않는다. 각 검증의 구성·fixture·횟수 한계를 유지하며 이번 충돌 해결에서 빌드·GPU·UI 검증을 재실행하지 않았다. 원문 1~20절과 진행률 배분은 [통합 master의 원문](https://github.com/29thnight/CreatorEngine/blob/b767a3c67e0f8474792a0a141a4580c5eac15712/docs/plans/TexturePipelinePlan.md)에서 확인한다.

## 21. 2026-10-10 Debug 파일 실패·복구 UI 후속 수용

현재 Debug runtime에서 실제 Ctrl+C/Ctrl+V와 저장 대기 age3ms 외부 충돌 보존,
파일 잠금/손상 상태의 Reload 실패와 편집 값2 보존, 복구 후 Retry의 자동 저장/파싱
오류 해제를 확인했다. 잠금 중 자동 저장도 디스크 bytes를 유지하고 Reload→새 편집으로
복구됐다. 두 격리 실행 모두 원본 PNG/GUID·저장 씬·미저장 객체를 유지하고 정상 종료했다.
현재 Debug 소비자 회귀248 checks/sourceDrift0도 통과했다. 새 빌드나 GPU 픽셀 수용은 아니다.

[Debug 파일 실패·복구 기록](../analysis/TextureAutomaticInspectorDebugFailureValidation20261010.md)에
runtime hash·화면·receipt와 한계를 기록한다. `12-0-D` 0.25일을 기존 T0 잔여에서 분리해
§20의 현재 집계를 66.25%로 최신화했다. 파일 읽기 잠금이 GUID 오류로 표시되는 진단,
삭제·저장 대기 중 손상 외부 bytes·최종 비교 이후 동시 쓰기·반복/장시간·다중 창/배율·
Debug 종료 충돌은 잔여다. T1b 대기와 T3 중단은 유지한다.

## 22. 2026-10-10 sidecar 실패 진단 수정·Debug 후속 검증

파일 읽기·파싱 실패를 GUID 오류로 덮어쓰던 분기를 분리했다. GUID는 이미 읽은 문서에서
검사하고 잘못된 문자열은 예외 대신 거부 결과를 반환한다. Debug Editor/library/runtime
빌드, 자산 서비스 짧은·긴 경로 각59 checks/sourceDrift0, 실제 Inspector 잠금 오류 표시와
Reload→새 편집 자동 저장 복구, 최종 소비자 회귀217 checks/sourceDrift0를 통과했다.
[진단 수정 실행 기록](../analysis/TextureSidecarDiagnosticsValidation20261010.md)에 근거를 남긴다.

§21 당시 발견한 진단 문제는 해결했다. 파일 삭제의 서비스 거부·재생성 금지도 한정
검사했으며 저장 대기/종료 중 삭제 경합 수용으로 확대하지 않는다. 최종 비교 후 동시 쓰기·
저장 대기 중 삭제/손상·반복/장시간·다중 창/배율·Debug 종료 충돌은 남긴다.
12-0-D 후속 보강이며 추가 공수 계상 없이 66.25% (표시66%)를 유지한다. Release 수정 후
빌드/실행은 아직 미검증이며 T1b 대기·T3 중단도 유지한다.

## 23. 2026-10-10 진단 수정 Release 후속 수용

동일 소스의 Release Editor/runtime/launcher 빌드와 자산 서비스 짧은·긴 경로 각59 checks를
통과했다. 실제 Inspector 숫자 입력으로 잠금 오류의 읽기 실패 진단과 파일 bytes 보존,
Reload→새 편집 자동 저장 복구를 확인했다. 미저장 객체·저장 씬·PNG/GUID를 유지하고
정상 Finalize 및 DX12 pending0/0/0으로 종료했다. 최종 Release 소비자 회귀270 checks도
통과했으며 서비스/소비자 sourceDrift0과 runtime hash 일치를 확인했다.
[진단 수정 실행 기록의 Release 후속](../analysis/TextureSidecarDiagnosticsValidation20261010.md)에
누락된 빌드 의존성 복구·병렬 LTCG 중간 파일 충돌과 직렬 재시도 이력, UI/receipt를 남긴다.

§22의 Release 수정 후 미검증 항목을 수용했다. 추가 제품 소스 변경이나 완료 가중치
증가는 없으며 진행률66.25% (표시66%)와 T1b 대기·T3 중단을 유지한다.
저장 대기 중 삭제/손상·최종 비교 이후 동시 쓰기·반복/장시간·다중 창/배율·
Debug 종료 충돌 및 전체 소비자/GPU/품질은 남긴다.

## 24. 2026-10-10 저장 대기 중 metadata 삭제·손상 후속 수용

실제 Inspector 숫자 입력으로 저장을 대기시킨 뒤 400ms 전에 삭제/손상을 주입했다.
Debug delete9ms/corrupt3ms, Release delete7ms/corrupt8ms로 각 모드를 한 번씩 검증했다.
대기 항목 소진 후 750ms 동안 삭제 상태 또는 손상 bytes를 유지하고 자동 저장을 거부했다.
실제 UI 오류 표시·draft2·미저장 객체 유지, 정상 metadata 복원 후 Reload0/오류 해제와
새 편집 자동 저장2를 확인했다. PNG/GUID·저장 씬 유지 및 정상 Finalize/DX12 pending0/0/0도
확인했다. 두 구성의 Editor/runtime/launcher 빌드는 통과했고 실행 전후 runtime hash가 일치했다.
[실행 기록·receipt·화면과 수용 한계](../analysis/TexturePendingMetadataValidation20261010.md)를 남긴다.

§23의 일반 저장 대기 중 삭제/손상 미검증 항목을 수용했다. 12-0-D 후속 보강으로
추가 가중치 없이 66.25% (표시66%) 및 T1b 대기·T3 중단을 유지한다.
최종 디스크 비교 이후 동시 쓰기·삭제/손상 동시 종료 경합·반복/장시간·다중 창/배율·
Debug 종료 충돌 및 전체 소비자/GPU/품질은 잔여다.

## 25. 2026-10-10 metadata 경합과 종료 flush 후속 수용

실제 Inspector 숫자 입력 후 저장 대기400ms 전에 삭제/손상을 주입하고 종료를 요청했다.
Debug 삭제10/41ms·손상4/28ms, Release 삭제7/27ms·손상8/29ms(요청/flush age)에서
종료 flush가 대기 편집을 거부했다. 삭제 상태 또는 주입한 손상 bytes를 보존하고
종료 코드0·정상 Finalize·DX12 pending0/0/0을 확인했다. 기존 미검증 Debug 유효 외부
설정 변경+종료도3/36ms에서 외부 maxDimension1/GUID를 보존했다.
각 모드는1회이며 원본 PNG·저장 씬 및 실행 전후 runtime hash 일치를 확인했다.
Debug/Release Editor/runtime/launcher 빌드를 통과했다.
[종료 후 상태 검증 스크립트·receipt·화면과 한계](../analysis/TexturePendingMetadataShutdownValidation20261010.md)를 남긴다.

§24의 삭제/손상 동시 종료 및 Debug 종료 충돌 항목을 한정 수용했다. 12-0-D 후속 보강으로
추가 가중치 없이66.25% (표시66%) 및 T1b 대기·T3 중단을 유지한다.
최종 디스크 비교 이후 동시 쓰기·반복/장시간·다중 창/배율 및 전체 소비자/GPU/품질은 잔여다.

## 26. 2026-10-10 실제 Editor의 5분 자동 저장 경합 반복 수용

실제 Inspector 숫자 입력으로 첫 저장 대기를 관찰한 뒤 같은 Editor 프레임 루프에서
QueueAutomaticTextureSettings API를 반복했다. 정상/유효 외부 설정 충돌/삭제/손상을
순환하고 각 경우 일반400ms 대기와 명시적 flush를 번갈아 검증했다.
Debug300,347ms/518회·Release300,147ms/532회로 총1,050회 통과했다.
강제 flush522회는 이 회차에 포함된다. 실패 거부·외부 상태 보존·baseline 복원 후
새 저장2/오류 해제, 매 회차 원본 PNG·저장 씬 보존 및 마지막 미저장 객체 조회를 확인했다.
실제 창 종료 후 exit0·정상 Finalize·DX12 pending0/0/0·PNG/GUID/저장 씬/runtime 보존 및
검증 입력4파일의 sourceDrift0을 확인했다. 두 구성의 Editor/runtime/launcher 빌드를 통과했다.
[초기 실제 UI 입력과 이후 API 반복의 구분·receipt·화면·한계](../analysis/TextureAutomaticSaveSoakValidation20261010.md)를 남긴다.

5분의 제어된 자동 저장 반복을12-0-D 후속 보강으로 수용하며 공수를 중복 계상하지 않는다.
진행률66.25% (표시66%) 및 T1b 대기·T3 중단을 유지한다.
최종 디스크 비교 이후 동시 쓰기·실제 UI 입력/종료 반복·다중 창/배율은 잔여다.
수 시간 실행·임의 외부 프로세스 타이밍·메모리 누수/성능·전체 소비자/GPU/품질 수용으로
확대하지 않는다.
## 27. 2026-10-10 파싱·임시 파일 작성 이후 원본 비교 보강

자동 저장 최초 원본 bytes를 callback과 공통 writer에 전달하고 임시 파일 write/flush/close
후 게시 직전에 다시 비교한다. 변경이 있으면 임시 파일을 제거하고 충돌 진단으로 거부한다.
Texture 설정·일반 import metadata·RenderProfile 자동 저장에 적용했다.
수정된 Debug/Release Editor/runtime/launcher 빌드를 통과했다.
제어된 자산 서비스 실행은 Debug/Release 짧은·긴 경로 각2회, 총8회×79=632 checks와
sourceDrift0을 통과했다. 각 실행의 Texture/metadata 게시 구간 충돌 주입 총16회에서 외부
bytes·PNG·기존 owner 보존, 임시 파일 제거 및 새 저장 복구를 확인했다.
[수정 전 실패·검사 타이밍 보정·최종 receipt·수용 한계](../analysis/TextureStagedPublishConflictValidation20261010.md)를 남긴다.

추가 비교는 원자적인 조건부 교체가 아니다. 게시 직전 최종 비교 이후 동시 쓰기는 남긴다.
RenderProfile 경합 실행 및 새 runtime의 실제 UI/종료 반복·다중 창/배율도 미검증이다.
12-0-D 후속 보강으로 중복 공수 없이66.25% (표시66%) 및 T1b 대기·T3 중단을 유지한다.
## 28. 2026-10-10 최신 runtime의 실제 Inspector 입력·종료 반복

게시 직전 비교 보강 후 Debug/Release runtime에서 실제 숫자 키 입력과 저장 대기 중
유효 외부 설정 변경·RequestQuit를 각각3회, 총6개 독립 Editor 프로세스로 반복했다.
Debug 요청/flush age3/37·9/38·4/35ms, Release8/30·7/35·9/35ms로 외부 설정1/GUID·
PNG·저장 씬을 보존하고 exit0·정상 Finalize·DX12 pending0/0/0을 확인했다.
실행 전후 runtime hash와 입력4파일 sourceDrift0을 확인했다. 추가 제품 변경/빌드는 없다.
[실제 입력 화면·receipt·종료 API와 UI 구분·수용 한계](../analysis/TextureActualInspectorShutdownRepeatValidation20261010.md)를 남긴다.

숫자 입력·제어된 유효 충돌·종료 반복을 구성당3회로 한정 수용한다. 다른 실패 유형·
붙여넣기·네이티브 닫기 버튼 반복과 다중 창/배율, 최종 비교 이후 동시 쓰기는 남긴다.
12-0-D 후속 보강이며 추가 완료 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.
## 29. 2026-10-10 RenderProfile 게시 경합 실패 재현 — 미수용

RenderProfile 공개 자동 저장 queue/flush에서 유효 변경·본문 삭제·손상을 일반 대기와
임시 파일 관찰 구간에 주입했다. Debug 짧은·긴 경로 각2회와 Release 짧은 경로1회는
각149 checks를 통과했으나 Release 긴 경로의 게시 구간 손상 주입에서 실제 덮어쓰기를
재현했다. 외부 손상 bytes 대신16,779,168 bytes의 대기 profile이 최종 파일에 남았다.
검사기는 실패 즉시 중단했으며 Release 두 번째 반복과 최종 sourceDrift 검사는 실행하지 않았다.

[실패 로그·덮어쓰기 결과·검사 host 보정·부분 성공의 한계](../analysis/RenderProfileAutomaticSaveConflictValidation20261010.md)를 남긴다.
제품 소스/runtime은 변경하지 않았다. 전체 회귀와 RenderProfile 경합은 미수용이며,
이후 재시도 성공으로 이번 실패를 제외하지 않는다. 최종 비교와 파일 교체 사이 외부
변경을 보존하는 조건부 게시 규약을 구현하고 실패 회귀를 통과시켜야 한다.
12-0 진행/earnedDays0을 유지하고66.25%(표시66%)·T1b 대기·T3 중단도 유지한다.

## 30. 2026-10-10 조건부 게시 수정·중간 종료 복구 수용

§29의 실제 덮어쓰기 실패 기록을 보존하고 자동 Texture 설정·일반 metadata·RenderProfile의
공통 게시 경로를 수정했다. 같은 열린 원본 핸들에서 bytes를 비교하고 원본을 복구 경로로
옮긴 뒤 완성된 staging 파일을 덮어쓰기 없이 게시한다. 잠금 이전 변경 또는 게시 경계의
외부 파일 생성은 거부하며 외부 상태를 보존한다. 잠금 중 외부 쓰기 거부와 실제 쓰기
성공 뒤 외부 bytes 보존을 구분해 검사한다. 직접/수동 저장 정책은 변경하지 않았다.

Debug/Release 짧은·긴 경로 각2회, 총8회×166 checks=1328을 통과했다. 정확한 원본 퇴역
이후 외부 목적지 생성·원본 잠금·게시 실패 rollback·예외·복구 정책 검사를 포함한다.
구성별 별도 프로세스를 퇴역 직후 exit73으로 중단하고 실제 자산 서비스 시작으로 원본
내용·GUID·카탈로그 신원 복구 각6 checks=12도 통과했다. 입력11파일 sourceDrift0 및
Debug/Release Editor 빌드를 확인했다.
[실행 receipt·수정 규약·한계](../analysis/ConditionalAssetPublishValidation20261010.md)를 남긴다.

서비스 조건부 게시와 제어된 프로세스 종료 복구를 한정 수용한다. 완성 파일만 게시하지만
정본 경로가 잠깐 없을 수 있으며 전원 차단 내구성·다중 Editor 동시 게시·실제 RenderProfile
Inspector/GPU 적용 검증은 아니다. 입력/종료 반복 확장·다중 창/배율은 12-0 잔여로 유지한다.
12-0-D 후속 보강으로 추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.

## 31. 2026-10-10 조건부 게시 수정 후 실제 Ctrl+V·종료 수용

§30의 최신 Debug/Release runtime에서 실제 Ctrl+C/Ctrl+V로 Maximum Dimension0→2를
입력했다. 종료 코드까지 기록한 구성별3회, 총6개 독립 Editor 프로세스를 한정 수용한다.
저장 대기 중 유효 외부 변경2회·삭제1회·손상1회는 요청age0~7ms, flush20~32ms에서
거부·외부 상태/PNG/저장 씬 보존·exit0·Finalize·DX12 pending0/0/0을 통과했다.
이4회 종료 요청은 관찰기의 RequestQuit이다. 실제 제목 표시줄 X 클릭2회도 설정2/GUID·
PNG·저장 씬 보존·exit0·빈 종료 큐를 통과했으나 저장 대기 중 클릭이라는 계측은 없다.
[개별 결과·종료 코드 미기록 실행 제외·hash 근거](../analysis/ConditionalPublishActualInspectorValidation20261010.md)를 남긴다.

저장된 제품/UI 입력14파일 hash 및 실행 전후 runtime hash가 일치했다. 제품 변경과
추가 빌드는 없다. 400ms 저장 대기 중 네이티브 닫기, 실패 유형별 반복 확장·다중 창/배율,
RenderProfile 실제 UI/GPU와 전체 소비는 잔여다. 추가 가중치 없이66.25%(표시66%) 및
T1b 대기·T3 중단을 유지한다.

## 32. 2026-10-10 저장 대기 중 WM_CLOSE 경로 수용

실제 Inspector 숫자 입력0→2로 생긴 pending을 관찰해 제목 표시줄 닫기 버튼과 같은
host WM_CLOSE 경로를 제어된 시점에 실행했다. 실제 클릭의400ms 내 타이밍 수용은 아니다.
Debug 정상/충돌 요청7/3·flush30/41ms, Release9/10·flush33/38ms로 구성별2회, 총4회가
WM_CLOSE 수신→pending flush→Finalize·exit0·DX12 pending0/0/0을 통과했다.
정상2회는 설정2를 저장하고 충돌2회는 외부 설정1의 정확한 bytes를 보존했으며,
전4회에서 GUID·PNG·저장 씬을 보존했다.

종료 발원지를 stdout에서도 확인하도록 App.cpp에 WM_CLOSE/CLI quit 수신 진단을 추가했다.
격리 fixture 관찰기·실행기·판정기를 보강했으며 종료/저장 동작은 변경하지 않았다.
Debug/Release 최신 빌드와 입력7파일 sourceDrift0·runtime hash 일치를 확인했다.
[기록·계측 전 실행 제외·판정기 오류 보정](../analysis/TexturePendingNativeCloseValidation20261010.md)을 남긴다.
400ms 안의 실제 닫기 클릭·다중 창/배율·다른 실패 유형 반복과 전체 소비는 잔여다.
추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.
## 33. 2026-10-10 Inspector 내부 분리·UI 배율 한정 수용

최신 Debug UI0.8·Release UI1.5에서 실제 내부 분리·Maximum Dimension0→2 편집·
다른 텍스처 선택·원래 선택 복귀를 확인했다. Debug0.8 재도킹은 통과했으나 Release1.5
재도킹은 확인하지 못해 잔여로 남긴다. 각12개의 파일·theme·종료 checks, 총24개를
통과했다. 설정2/GUID·PNG·저장 씬·저장 배율·runtime hash·WM_CLOSE/Finalize·exit0·
DX12 pending0/0/0과 입력4파일 sourceDrift0을 확인했다.

OS DPI는 두 실행 모두1.5이며 UI 배율과 구분한다. Release 최초240초 관찰 제한 초과를
보존하고 이후 실제 X 종료의 별도 exit0 receipt로 판정했다. 격리 실행기에 선택 배율과
두 theme 스냅샷을 추가했으며 제품 수정·추가 빌드는 없다.
[실행 기록·관찰과 기계 판정·미확인 경계](../analysis/TextureInspectorScaleDockValidation20261010.md)를 남긴다.

별도 OS 창은 ViewportsEnable 보류로 미수용이다. Release1.5 재도킹·좁은 dock 편집,
전체 구성×배율 조합·동적 DPI/다중 모니터는 잔여다. 400ms 안 실제 닫기 클릭과 실패
유형별 반복 확장도 유지한다. 추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단이다.

## 34. 2026-10-10 UI 후속 보류·실제 Shipping 파이프라인 검증

사용자 요청으로 §33 배율/도킹 후속은 보류하고 핵심 cook→PAK→Shipping DX12 실행을 검증했다.
긴 배포 경로의 Slang DLL 로딩 오류206을 확장 절대 경로로 수정했다. 최초 성공 패키지에
PNG/HDR 원본이 남아 원본 부재 수용에서 제외했고, 성공한 cook 선택 목록에 따라 패키지
입력 복사본의 원본/sidecar를 제거했다. 그 뒤 실제 exit161로 재현한 CEMF 원본 요구를
Texture2 artifact 존재/크기 조건으로 수정했다. 프로젝트 원본과 typed payload 검증은 유지한다.

최신 Release Shipping은 원본 이미지 항목0에서 GT1630/display1631·submitted228·promotions120·
exit0, sceneCompiles/parser0, GPUValidation 활성화, 종료 pending0/빈 큐를 통과했다.
texture2/model1/program3 cook, runtime234개·PAK SHA 및 최신 Shipping DLL 일치,
입력761개 sourceDrift0, BuildTool148 checks를 확인했다.
[실패 재현·수정·최신 실행과 경계](../analysis/TextureShippingCoreValidation20261010.md),
`Build/Phase12CoreVerification/result.json`을 정본 근거로 추가한다.

별도 cooked Scene 회귀24frames/57259checks는 합성 texture를 사용하는 shader GPU 검증이다.
CECT payload의 GPU sampling 수용으로 확대하지 않는다. 실제 구성은 Release 하나이며
기존 Debug,Release 하드코딩 보고를 수정했다. 현재 PAK의 GLB 원본은 남는다.
모델 원본 제거/전체 역할, BC5/BC7 CECT의 DX12/Vulkan 실제 GPU 비교, 전체 소비자,
수정 후 missing/truncated CECT 실패 주입은 잔여다. 다음 작업은 이 핵심 GPU 소비 경계다.
T2 잔여 전체 수용 전이므로 추가 가중치 없이66.25%(표시66%), T1b 대기·T3 중단을 유지한다.

## 35. 2026-10-10 실제 Shipping CECT 누락·잘림 거부와 복원

§34의 최신 Shipping runtime을 사용해 원본 PNG/HDR 없는 별도 PAK4개를 실행했다.
두 CECT 각각 삭제/4바이트 잘림은 모두 exit161·catalog mount 실패로 거부했고,
오류의 source identity가 각 주입 대상과 일치했다. 원본 artifact를 복원한 같은 fixture는
GT1581·submitted202·promotions121·ready=true·exit0, GPUValidation 활성화,
sceneCompiles/parser0·Shipping 서비스 격리·종료 pending0/빈 큐를 통과했다.
각 PAK SHA, 기준 정상 패키지235파일·원래 입력278파일을 보존했다.
[실패 주입·복원·판정 범위](../analysis/TextureShippingArtifactFailureValidation20261010.md)를 남긴다.

수정 후 missing/truncated CECT 거부는 잔여에서 제외한다. 동일 크기 payload 손상의
typed acquisition 거부와 실제 BC5/BC7 CECT GPU 픽셀 비교, 전체 소비자/모델/Vulkan은
잔여다. 제품 변경·추가 빌드는 없으며 추가 가중치 없이66.25%, T1b 대기·T3 중단이다.

## 36. 2026-10-10 CECT typed 무결성·새 BC 형식 실제 DX12 sampling

최신 Release RenderEngine 및 별도 offline/runtime 검증 실행 파일을 빌드했다.
제품 CookTexture의 BC5Unorm·BC7Unorm·BC7UnormSrgb CECT를 원본 이미지 없이
메모리 AssetSet에서 RequestAsync<Texture>→정확한 image 재취득→제품 DX12TextureCache→
SampleLevel/GPU readback으로 소비했다. 각8×8·4mip의340개, 총1020개 성분이
형식별 공식 보간/색변환 기준 및 float epsilon을 통과했다. GPUValidation/debug message0,
submission 완료/종료 큐0/0/0, 입력12파일 sourceDrift0·fixture6파일 보존을 확인했다.

동일 크기·유효한 구조의 payload 한 바이트 손상3건은 metadata mount 성공 뒤
typed acquisition에서 Failed/IntegrityFailed·SHA-256 원인·descriptor 없음으로 거부했다.
잘못된 CPU reference 음성 검사는 정확한 texel mismatch/exit1로 실패했다.
[최종 실행·최초 판정기 오류 보정·CPU/GPU 차이와 공식 근거](../analysis/TextureCookedGpuValidation20261010.md)를 남긴다.

§35의 같은 크기 typed 손상 거부와 BC5/BC7 standalone DX124mip sampling은
이 한정 fixture에서 잔여에서 제외한다. Shipping PAK에서 같은 BC 형식 소비,
Vulkan/Debug·cube/array·전체 소비자·모델 역할과 압축 품질/시간/메모리는 남는다.
CECT sampling 수용을 합성 shader 입력 GPU 회귀나 전체 모델 수용과 섞지 않는다.
T2 잔여 gate 전체를 닫지 않아 추가 가중치 없이66.25%, T1b 대기·T3 중단이다.

## 37. 2026-10-10 동일 CECT의 DX12·Vulkan GPU sampling

최신 Release RenderEngine과 검증 실행 파일을 순차 빌드하고 동일한 BC5Unorm·
BC7Unorm·BC7UnormSrgb CECT fixture를 두 백엔드에 전달했다. 원본 없이 typed
acquisition→정확한 image→제품 texture cache→SampleLevel/readback이 각각4mip·
1020성분, 총2040성분 비교를 통과했다. Vulkan validation layer/DX12 GPUValidation
활성화·진단0·exit0·종료 큐0/0/0, Vulkan 미구현 연산0을 확인했다.

동일 크기·구조 유효 payload 손상은 백엔드별3건, 총6건 IntegrityFailed로 거부했다.
잘못된 CPU reference는 양쪽에서 정확한 texel mismatch/exit1로 거부했다.
입력17파일 sourceDrift0·동일 fixture6파일 보존·바이너리/라이브러리 SHA를 기록했다.
[동일 fixture 실행·제품 비교 기준·수용 경계](../analysis/TextureCookedVulkanValidation20261010.md)를 남긴다.

standalone Release Vulkan의 해당3형식4mip sampling은 잔여에서 제외한다.
Shipping PAK에서 동일 BC 형식 소비, Debug·cube/array·전체 소비자/모델 역할·
GLB 원본 제거와 압축 품질/시간/메모리는 남는다. Vulkan 규격 전체 conformance나
두 백엔드 readback의 직접 bit 일치를 주장하지 않는다. 제품 수정은 없으며
T2 전체 gate 수용 전이라 추가 가중치 없이66.25%, T1b 대기·T3 중단이다.
