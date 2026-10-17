# PHASE 12 CECT typed acquisition·DX12 sampling — 2026-10-10

## 판정과 실제 경로

최신 Release RenderEngine 라이브러리와 새 검증 실행 파일에서 BC5Unorm·BC7Unorm·BC7UnormSrgb의 **CECT2 typed acquisition→정확한 image 취득→제품 DX12TextureCache 업로드→SampleLevel→GPU readback**을 한정 수용한다. 각8×8·4개 mip(8/4/2/1)의85 texel×4성분=340개, 총1020개를 비교했다. 이 실행의 이미지 입력은 offline cooker가 출력한 실제 압축 CECT payload이며 runtime에 PNG/DDS 원본을 넘기지 않는다.

offline fixture 실행 파일만 DirectXTex 및 TextureCooker.cpp를 연결한다. 메모리 DDS gradient를 제품 CookTexture로 BC5 normal/BC7 linear/sRGB 및 GenerateFull로 cook하고, CECT를 strict decode한 압축 블록을 DirectXTex로 CPU decompression한다. sRGB RGB는 명시적 transfer를 적용하고 alpha는 유지한다. runtime 실행 파일은 DirectXTex 처리 TU를 연결하지 않고 제품 AssetSet/DataSystem과 DX12 캐시를 사용한다. 이 분리를 Shipping 배포 바이너리 전체 감사로 확대하지 않는다.

명령: `Tools/regression/verify-texture-cooked-gpu.ps1`.

최종 case: `Build/Obj/TextureCookedGpu/bcd1f3a6d278485dbfe03f4f434578c0`. `result.json`, `fixture.log`, `probe.log`, `oracle-negative.log`와 세 build log가 근거다. 최신 위치는 `Build/Phase12CoreVerification/cooked-gpu-root.txt`다. RenderEngine→offline fixture→runtime probe를 정상 Release Build하고 실행했다. 원래 다른 단계의 최신 Shipping 라이브러리와 달리 Release 라이브러리에는 DataSystem mount 변경이 없던 첫 성공 실행은 최종 근거로 사용하지 않고, 현재 Release 라이브러리 빌드 후 다시 연결/실행했다.

| 형식 | mip/성분 | 최대 linear 절대차 | 판정 |
|---|---:|---:|---|
| BC5Unorm | 4/340 | 0.0104634 | 블록별 BC5 보간 허용치, endpoint/상수 검사 통과 |
| BC7Unorm | 4/340 | 5.96046e-08 | float 비교 epsilon1e-7 통과 |
| BC7UnormSrgb | 4/340 | 0.00169456 | RGB의 sRGB 측0.5 ULP, alpha/끝점 epsilon1e-7 통과 |

DX12 DebugLayer/GPUValidation 활성화·debug message0, 실제 submission ticket/fence 완료·readback 비교·GPU 대기 후 해제와 종료 큐0/0/0을 확인했다. source12파일 변경0, fixture6파일 bytes 보존, 두 실행 파일과 RenderEngine.lib SHA를 기록했다. GPU 통과를 이미지 압축 품질/성능 수용으로 사용하지 않는다.

## 동일 크기 손상·판정기 민감도

각 형식에서 CECT 마지막 payload 한 바이트를 변경하고 **원래 manifest SHA-256을 유지**했다. artifact 크기와 strict CECT 구조는 유효했고 metadata mount도 성공했다. 그 뒤 제품 RequestAsync<Texture>가 전3건 Failed/IntegrityFailed·SHA-256 원인·descriptor 없음으로 거부했다. 정상 artifact를 새 mount에서 취득한 뒤 정확한 CodecImage를 재취득하고 GPU에 올렸다. GPU 캐시의 중립 텍스처 대체/업로드 실패0도 확인했다.

별도 복사본에서 첫 BC5 CPU reference를1.0으로 바꾸면 실제0.098039와 비교해 exit1·정확한 texel mismatch를 반환했다. 원래 reference/CECT는 유지했다. 이 음성 검사는 비교기가 무조건 성공하지 않음을 확인한다.

## 최초 실패를 보존한 이유와 기준 보정

초기 검증 코드의 누락 include/cast, Find 인자 순서, BC5 normalMap 설정 및 AssetSet host ABI 불일치를 고쳤다. 정상 실행으로 계상하지 않았다. 최초 BC5 mip1의 실제0.279362/CPU0.284594 차이0.005232는 일률적인1/255 비교를 넘었지만 블록 endpoint244/4에서 공식 허용 범위에 해당한다. 초기 sRGB linear 오차0.000755도 임의의 절대0.0005 기준을 넘었으며, 해당 규격은 linear 절대 오차 대신 sRGB 측 오차를 검사한다. 실패 case/log를 삭제하지 않았다.

BC5만 블록 endpoint span에 따른 `1/65535 + 0.03*span`을 적용하며 endpoint·0/1·미사용 채널은 float epsilon1e-7로 확인한다. BC7 linear는 float epsilon1e-7을 유지한다. BC7 sRGB RGB는 ideal 역변환 후 반올림하지 않은8bit sRGB 값에서0.5 ULP를 확인하고, alpha·0/1은 엄격 비교한다. 공식 근거는 [Microsoft Direct3D Functional Specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)의 §19.5.2/§19.5.9/§19.5.11 및 §3.2.3.7이다. CPU float 계산의 작은 epsilon을 포함한 제품 회귀이며 공식 하드웨어 conformance 시험은 아니다.

## 미수용 범위

메모리 cooked AssetSet의 standalone2D 경계다. Shipping PAK에서 같은 BC 형식의 typed acquisition/sampling, Vulkan·Debug, cube/array·다른 dimensions/포맷·전체 모델 역할/소비자·BC5 normal Z 재질 결합은 이 실행에 포함하지 않는다. 기존 §34~35 Shipping 원본 부재/누락·잘림 증거와 구분한다. 별도 oracle는 압축 전 저작 이미지의 시각 품질이 아니라 **압축 artifact의 CPU decode와 GPU 소비 비교**다.

새 제품 수정은 없다. 검증 실행 파일2개·shader·프로젝트2개·실행기를 추가했다. T2 전체 잔여 gate는 계속 열려 있으며 추가 가중치 없이66.25%, T1b 대기·T3 중단을 유지한다. 다음은 동일 fixture의 Vulkan sampling 및 실제 제품 소비자/Shipping PAK 연결이다.
