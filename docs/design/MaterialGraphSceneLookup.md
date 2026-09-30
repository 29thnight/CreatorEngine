# Material Graph Scene의 정확한 픽셀 IBL 캐시

## 입력 수집과 소비

`MaterialGraphSceneLookup.{h,cpp}`는 실제 Scene host가 수집한 Core/Layered 재질의
공간·시선별 IBL 결과를 불변 프레임으로 보관한다. 컬러 fragment의 1,024회 적분과
환경 convolution을 compute bake로 옮겼다. 컬러는 직접광을 계산하고 현재 AO와
캐시의 base/coat/sheen 환경 응답을 소비한다.

```text
같은 GBuffer depth와 draw owner
  -> LX.Scene.LookupCapture0/1 (8 + 3 RGBA32 MRT)
  -> LX.Scene.LookupBake (변경 픽셀만 적분)
  -> LX.Scene.Color (lookup 소비, 기존 HDR 합성)
  -> LX.Scene.LookupReady (다음 프레임의 읽기 상태)
```

입력은 `IblBakePoint`의 11 float4다. graph를 먼저 평가하고 fine UV derivative와
coverage를 계산한 뒤 정확한 draw owner가 일치하는 픽셀만 저장한다. 재질 값,
normal·tangent·회전, coat·sheen·film, 시선과 tier를 모두 포함한다.

## 재사용 판정

- 같은 view ID·scene epoch·viewport·환경 handle·환경 generation의 이전 제출 결과를 찾는다.
- 가시 픽셀의 입력 44개 float를 `asuint`로 정확히 비교한다. hash·양자화·보간을 쓰지 않는다.
- 같고 이전 sample이 유효하면 9 float4의 응답을 복사한다. 다르면 그 픽셀을 새로 적분한다.
- 가림과 배경은 validity만 기록한다. 새로 노출된 픽셀은 이전 배경을 재사용하지 않는다.
- 환경 generation 변경은 모든 가시 픽셀을 갱신한다. 같은 texture handle의 재생성도 포함한다.
- 환경맵이 없어도 Layered 직접광에 필요한 적분은 유지하고 환경 convolution은 생략한다.

material generation 자체를 키로 삼아 무조건 전체 갱신하지 않는다. 현재 평가값이 같으면
안전하게 재사용하며, 실제 카메라·world·UV·parameter 변경은 해당 입력 비교에 반영된다.

## 게시와 수명

프레임 candidate는 준비가 전부 성공한 뒤 교체한다. graph 콜백은 현재 recording,
descriptor와 graph resource epoch를 확인한다. compute와 ready 콜백 기록만으로 캐시를
게시할 수 없다. native upload transaction listener가 같은 recording의 실제 제출과
completion point를 확인한 뒤 `PublishSubmittedCache`를 허용한다.

실패·abort·잘못된 frame·제출 전 게시·중복/과거 게시를 거부한다. graph/submission owner가
이전 sample과 현재 자원을 GPU 완료까지 유지한다. published frame의 history는 weak owner로
두어 프레임 체인이 쌓이지 않는다. 제출된 view는 최근 2개를 보관하며 GPU idle 뒤 정리한다.

## 예산과 남은 비용

기본 host 한도는 viewport 4,096²픽셀·4,096 draw, lookup payload 2 GiB다.
메모리 검사는 현재 candidate와 같은 view의 이전 캐시를 포함한다. 한 픽셀의 입력은
176 B, 응답은 144 B로 프레임당 320 B다. 1920×1080의 두 프레임은 약 1.24 GiB다.
이 값에는 드라이버 정렬·다른 view·GPU에서 아직 쓰는 retirement owner가 포함되지 않는다.

계산 dispatch는 최대 4,096픽셀씩 나누며 그 안에서 배경과 재사용을 빠르게 건너뛴다.
큰 viewport의 자원·상태 전환과 캐시 재사용을 지원하지만, 카메라 이동으로 모든 픽셀이
갱신될 때의 적분 비용과 매 프레임 자원 할당/입력 MRT 대역폭은 아직 높다. 자원 풀·압축·
오차가 판정된 공간/시선 lookup·환경 MIS/수렴·일반 Scene의 시간 예산은 후속이다.

이미지별 texture footprint와 Vector·sampler의 독립 샘플링은
[MaterialGraphTextureFootprints.md](MaterialGraphTextureFootprints.md)에 확장했다.
Special/Blended, 자동 Scene host 쿠킹은 이 캐시 범위에 포함되지 않는다.
실제 Editor Live Tick·Vulkan GPU 실행과 MAT-9 성능 수용도 별도다.

## 검증

`Tools/regression/verify-material-raster-surface.ps1 -SkipDependencyRestore`가 기존
shared depth·HDR reference에 순차/1워커/4워커의 정확한 재사용,
camera/world/environment 갱신, parameter/UV의 부분 갱신, 제출 게시 실패와
1920×1080 viewport를 추가한다.

입력 transport는 별도로 검사한다. SRGB RGB는 정규화 오차 0.001,
나머지 41개 입력은 0.0001을 적용한다. 후속 이미지별 sampling fixture의 선형 필터 Data alpha는
[별도 transport 기준](MaterialGraphTextureFootprints.md)을 따른다.
GPU rasterization의 subpixel vertex snapping으로
시선에 약 1e-5의 차이가 생길 수 있으며 roughness 0의 직접광 반사 피크에 증폭된다.
따라서 검증된 GPU 전달 입력으로 독립 CPU double IBL/직접광 계산을 수행한다.
IBL 36성분의 정규화 0.0001, HDR half 출력의 `0.001 * max(1, abs(expected))`
기준은 유지한다. 캐시 재사용/갱신 카운터와 최종 컬러를 모두 읽어 검사한다.

2026-09-29 캐시 도입 시 동결 소스의 Debug/Release 빌드·native D3D12 GPU validation 결과:

| 검증 | 각 구성의 결과 |
| --- | --- |
| 전체 회귀 | 20,762,086개 검사 · GPU 3,623,244성분 |
| 실제 Scene 합성 | 36 graph · 가시 42,292픽셀 |
| 준비/선언 실패 복구 | 192개 거부 · accepted frame 보존 |
| 입력 갱신 | 10프레임 · 카메라/world/환경 전체 갱신, parameter/UV 부분 갱신 |
| lookup 작업 | 새 적분 20,491픽셀 · 정확한 재사용 21,801픽셀 |
| 1920×1080 | cold/warm 2프레임 · 가시 픽셀 합계 3,120 |
| D3D12 GPU validation | WARNING 이상 0건 |

Full HD fixture는 작은 삼각형의 희소 coverage다. capture/bake/color/ready GPU timestamp:

| 구성 | cold | warm |
| --- | --- | --- |
| Debug | 114.512 ms | 12.676 ms |
| Release | 17.6696 ms | 7.74218 ms |

GPU validation이 켜진 단일 cold/warm 측정으로 성능 수용 기준이나 일반 Scene benchmark가
아니다. 모든 픽셀이 재질인 Full HD와 카메라 이동 시 전체 갱신 비용을 통과한 것으로
해석하지 않는다. 시간값에는 제품 generation 컴파일·자원 할당의 CPU 비용도 포함하지 않는다.

Core/Layered Scene의 VS/GBuffer PS/lookup PS 2종/Color PS는 DXIL·SPIR-V 20개,
lookup bake/clear CS는 두 backend의 4개 artifact를 검증한다. 기존 probe 출력의
`compiled=24`와 별도다. DX12/Vulkan 실제 Scene consumer를 빌드하지만 native GPU 실행은
D3D12다. 실제 Editor Live Tick·Vulkan GPU 실행을 이 회귀에 포함하지 않는다.

로그는 `Build/Obj/MaterialProductProbe/scene-lookup-{Debug,Release}.log`,
빌드 로그는 `scene-lookup-build-{Debug,Release}.log`, 소스 동결 기록은
`scene-lookup-source-hashes.json`이다. 확장 runtime signature로 두 구성의 결과와 소스 hash를
확인한 최종 판정은 `scene-lookup-gate-final.log`다. 후속 이미지별 샘플링을 포함한
44 graph/43,644픽셀·새 적분 21,021/재사용 22,623픽셀의 Debug/Release 결과와
`texture-footprint-*` 로그는 [MaterialGraphTextureFootprints.md](MaterialGraphTextureFootprints.md)가
소유한다. MAT-7은 진행 중이며 완료 공수를 추가하지 않는다.
