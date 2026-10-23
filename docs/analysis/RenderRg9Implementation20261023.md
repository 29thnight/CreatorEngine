# RG9 범위·split barrier·Inspector 구현 — 2026-10-23

master `0509753a3`와 기존 로컬 RG8·viewer 변경을 보존한 작업트리에서 진행했다.

## 재사용한 구현

`EnhancedRenderGraphSubresources.cpp`의 mip/layer/aspect·buffer byte-range 검증, 접근 cell 분할, RAW/WAR/WAW·culling, backend state granularity와 범위별 barrier 계획을 재사용했다. RHI range 자료형과 DX12/Vulkan 변환, snapshot·HTTP range 직렬화도 기존 구현이다. 기존 Matrix·Queue·DAG·Freeze/Refresh와 선택 모델을 유지했다.

## 추가한 구현

- `RGTextureDesc.mipLevels`를 transient 생성·declaration packet·resource description·pool key·alias allocation key·placed-resource identity·program 구조 서명에 전달한다. 0과 크기보다 긴 mip chain은 선언 단계에서 거부한다. 기본값은 1이다.
- 중립 transition에 Full/Begin/End를 추가했다. DX12는 BEGIN_ONLY/END_ONLY로 기록하며 software command 복사도 해당 값을 보존한다. 직접 device 전이와 encoder 전이가 같은 flag 변환을 사용한다.
- split은 반복 pass의 phase 사이에만 계획한다. 반복 pass는 병렬 기록에서도 하나의 encoder에서 직렬 기록된다. 이전 접근 뒤 독립 phase가 있을 때 시작하고 소비 phase 앞에서 끝낸다. 자원의 다른 range도 중간에 접근하면 분할하지 않는다. 같은 자원의 사용이 인접하거나 backend가 지원하지 않으면 소비 지점에 Full 전이를 남긴다. Common에서 시작하는 전이와 외부 공유 상태 전이는 분할하지 않는다. 큐·제출·pass 경계에 pending half를 넘기지 않는다.
- split 통계에 BEGIN 절반과 반복 횟수를 포함하고, 반복 barrier 카운터 상한도 두 절반을 포함한다.
- Inspector에 선언 범위와 실제 barrier 범위, Full/Begin/End, 반복 첫/후속 단계 barrier, 선택 pass의 실행 큐를 표시한다. snapshot이 가진 상태·수명·alias 표시와 함께 읽는다.

## 지원 행렬

| 기능 | DX12 | Vulkan |
|---|---|---|
| texture mip/layer/aspect | native subresource 전이 | image subresource 전이 |
| buffer byte range 접근 | 정확한 접근 의존성, 상태 전이는 whole-buffer | 정확한 접근·buffer 전이 범위 |
| 서로 다른 범위의 동시 상태 | native 상태 단위로 표현 가능한 경우 허용 | backend capability에 따라 허용 |
| split transition | 동일 반복 pass 안의 대응 BEGIN/END | compiler Full fallback |
| 큐·제출 경계 전이 | 기존 완전 전이·wait·retirement 유지 | 기존 완전 전이 유지 |

Vulkan이 split 미지원인데 외부 호출자가 수동 half를 넘기면 명시적으로 거부한다. Vulkan 실제 장치 비교는 PHASE 4.9에서 수행한다.

## 검증

최종 소스 hash와 Debug/Release exe·runtime hash가 실행 신원과 일치한다.

| 검증 | 결과 |
|---|---|
| VS 18/v145 전체 빌드 | Debug 오류 0·경고 3, Release 오류 0·경고 9 |
| RG9 native GPU — 각 구성 | 512프레임·13,315검사·exit 0 |
| split / Full — 각 구성 | 각각 256프레임, 프레임마다 3회 반복 |
| mip/layer·픽셀 | mip 1 / layer 1, 프레임 마지막 반복의 4×4 RGBA 결과 정확 일치 |
| 기록·생성 경로 | 직접/software 기록 × imported/transient mip texture 교차 |
| GPU validation | Debug·Release 모두 오류 0 |
| 공유 큐·전체 RenderGraph | D/R 각각 native 441·계획 82·전체 회귀 통과 |
| software RHI | Debug native clear/barrier/readback·async/parallel 번역·pool/arena/큐 수명·전체 RG 통과 |
| CPU 실제 graph TU | 범위 의존성·잘못된 범위 거부/Reset 복구·split 쌍/인접 접근·transient mip 풀/program 무효화 통과 |
| viewer | 실제 snapshot 자료형·80 pass/120 resource·두 크기·범위/split 렌더·선택 클릭 통과 |
| 할당 예산 | 8회 warmup 뒤 각 512회·10항목 통과; 선언/compile/계획 추가 요청 0, 제출 lease 1회/56 B 유지 |

Debug 경고는 SceneRuntime의 중복 생성 항목 MSB8027 1개와 중복 심볼 LNK4006 2개다. Release는 MSB8027 1개와 Utility_Framework.pdb 타입 기록 손상 LNK4020 8개이며 일부 디버거 타입 정보 접근에 영향을 준다.

현재 범위를 RG9 제품 수용으로 반영한다. 범위별 import 초기 상태와 반환 상태는 기존 단일 상태 계약에 맞춰 합류한다. split은 동일 반복 pass 안에서 닫는다. 실제 Vulkan 장치 비교는 PHASE 4.9에서 수행한다. RG8 기본 mode 0과 RG8-F 미래 계획을 유지한다.

[검증 요약·신원](RenderRg9Implementation20261023.json). 전체 로그와 native 결과는 `Artifacts/rg9-20261023/`에 있다.

## ImGui 회귀 assertion 수정

추가한 range 표시 검사가 새 자동 크기 창의 첫 프레임에서 곧바로 정점 수를 단정해 abort했다. 창 크기를 명시하고 안정된 다음 프레임을 검사하도록 수정했다. 테스트 실행파일은 CRT assertion을 stderr로 보내고 abort 보고 팝업을 끈다. wrapper도 stderr를 로그에 포함한다. 수정 후 모델·실제 snapshot 자료형을 사용하는 ImGui 검사와 선택 클릭이 통과했다.

## 증거 보존

시작 시 기존 수정 파일의 hash와 작업 대상 원본을 보관했다. 작업 외 기존 수정은 byte 단위로 비교한다. 이전 RG8 측정의 프레임·캡처 원장은 유지한다. 초기 호환 확인에서 이전 RG8 보조 CPU runner를 사용해 그 폴더의 graph-probe 실행파일·빌드/결과 로그를 재생성했다. 이후 RG9 CPU 실행과 native·빌드 증거는 모두 새 RG9 폴더에 분리했다.
