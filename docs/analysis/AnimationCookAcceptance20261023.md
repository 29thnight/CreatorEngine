# 애니메이션 패키지 쿠커 수정·Player 소비자 수용

2026-10-23 로컬 작업본의 후속 검증이다. [이전 CLI 소비자 수용](CliConsumerMigrationAcceptance20261023.md)에서 남긴 AnimationPlayer의 cook 실패를 추적한다. 정확한 소스·바이너리·SDK·패키지 신원과 실행 경로는 [동명 JSON](AnimationCookAcceptance20261023.json)에 기록한다.

이 기록은 master 동기화 전 `0847ceeb2` 작업본의 실행 증거다. 이후 `a985ee7e1` 통합과 재점검은 [별도 동기화 수용 기록](MasterSyncRecheck20261023.md)을 따른다. 아래 수치와 JSON 지문은 과거 실행의 신원으로 보존한다.

## 원인과 수정

실패는 애니메이션 클립 해석 전, 자동 Scene 재질 프로그램의 SPIR-V 컴파일에서 발생했다. 쿠커는 DX12 패키지도 DXIL·SPIR-V 양쪽의 완전한 stage set을 검증하므로 `-RenderBackend dx12`만 지정해도 실패가 사라지지 않았다.

공통 `RHIShaderCompiler`는 DXIL에 SM 6.x 소스 계약을 지정하지만 SPIR-V에는 출력 버전인 `spirv_1_3` 또는 `spirv_1_5`와 일부 기능만 지정했다. Slang 2026.14의 `NonUniformResourceIndex`는 교차 대상 기능 계약을 요구한다. 재질의 텍스처 테이블 접근에서 이 요구가 암묵적인 profile 확장 경고 `E41012`를 만들었고, 기존 `warnings-as-errors all`에 따라 cook이 중단됐다. 같은 호출과 텍스처 크기 조회를 쓰는 작은 셰이더로 수정 전 실패를 재현했다.

SPIR-V에도 엔진의 기본 소스 계약인 `-capability sm_6_0`을 전달한다. 출력 버전 선택, mesh·64비트 atomic·미분·quad 옵션은 기존 배선을 유지한다. 컴파일러 캐시 신원을 v13에서 v14로 바꿔 이전 결과의 재사용을 차단했다. 경고의 오류 처리와 양쪽 백엔드 검증은 유지한다. SM 6.6 전체를 SPIR-V 기본 계약으로 올리지 않는다.

근거는 고정한 Slang 버전의 [기능 별칭 정의](https://github.com/shader-slang/slang/blob/v2026.14/source/slang/slang-capabilities.capdef), [HLSL intrinsic 요구](https://github.com/shader-slang/slang/blob/v2026.14/source/slang/hlsl.meta.slang), [명령 인자 계약](https://docs.shader-slang.org/en/stable/external/slang/docs/command-line-slangc-reference.html)이다. 기능 별칭을 컴파일러에 전달하는 것과 Vulkan 장치의 모든 선택 기능을 활성화하는 것은 별도다. 이번 수정은 장치 기능 협상을 바꾸지 않는다.

셰이더 단계가 열린 뒤 전체 패키지는 두 번째 경계인 의존성 밀봉에서 거부됐다. AnimationPlayer fixture가 현재 CreatorRobot 모델·메시 신원 대신 이전 GUID를 유지하고 있었다. 모델 참조5곳과 메시 참조4곳을 현재 sidecar의 `CreatorRobot_Body_0`~`CreatorRobot_Body_3`에 맞춰 갱신했다. Animator의 모델 기반 Motion 참조와 Walk 선택 인덱스1은 유지한다. 누락된 의존성을 무시하거나 빈 모델로 대체하지 않는다.

이후 쿠커의 전체20모델·73재질 생성과 밀봉은 성공했지만 BuildTool의 검사에서 중단됐다. 검사기가 이미 런타임에서 은퇴한 `model.cemc`와 generation당 동반 파일2개를 가정했다. 현재 런타임이 요구하는 `metadata.cemc`·`streaming.cemf`·`Derived/model.cemd`와 지오메트리/CPU/선택적 스켈레톤 경로로 검사기를 이주했다. generation의 필수 파일, canonical GUID/지문 경로와 소유 레코드를 검사하며, 은퇴한 monolithic 형식은 거부한다. 병합 때는 전체 파일 재고와 SHA-256을 원래 cook 결과와 대조하여 같은 크기의 변조도 잡는다. 바이너리 형식 판독과 의미 검증은 네이티브 쿠커·런타임이 계속 담당한다.

PAK 생성 후 실제 Player 시작에서도 fixture의 이전 재질 연결이 발견됐다. CreatorRobot 재질4개가 Code 형식과 사라진 텍스처 GUID를 유지하여 필수 인라인 재질 준비가 실패했다. 실패 종료가 임시 로그를 회수하므로 별도 진단 실행에서 해당 프로세스의 로그를 종료 전에 복사해 원인을 확인했다. 현재 모델의 Material Graph 인스턴스4개로 이주하고 기존 색·스칼라·양면·블렌드 설정과 기본 UV 변환을 보존했다. 텍스처는 동일 부위/용도의 현재 모델 신원에 연결했다. 없는 텍스처를 무시하거나 빈 재질을 수용하지 않는다.

비활성 Primitive8개도 레거시 재질이었고, 씬 준비는 이 의존성도 검사했다. 이들 역시 현재 모델 Graph로 이주해 전체12개 메시의 연결을 유지했다. 기존 색·metallic·roughness·양면 설정을 보존했고 IOR/flow/UV가 기본값인지 검사했다. Primitive8개의 기존 원본 텍스처와 모델에 포함된 텍스처의 SHA-256이 모두 동일함을 확인했다.

재질 준비 뒤의 `view_inactive`는 엔티티69개의 이전 `m_layer`/`m_collisionType` 필드가 stable layer 스키마에서 거부된 결과였다. 카메라 설정을 바꿔 우회하지 않고 기존 Default 의미를 `m_layerId: 1`로 이주했다. 현재 CameraSystem은 primary가 없는 경우에도 유효한 fallback 카메라를 선택하므로 기존 카메라 설정은 유지한다. 빈 씬 상태로 화면 제출을 충족하지 못한 실행은 실패로 보존했다.

실제 소비자에서69엔티티·Walk·포즈 진행을 확인한 뒤 정상 종료가 exit5로 실패했다. 실패 시점의 진단에서 CreatorRobot의 스트리밍 `.cege` 파일에 읽기 lease가 남아 있음을 확인했다. 프로세스 정적 메시 분할 캐시가 스트림의 파일 읽기 함수를 보관했고 명시적인 세션 종료 절차가 없었다. 마지막 프레임의 핀만 해제한 중간 수정도 실패했다.

`MeshSurfacePlan::ReleaseCachedScenePlans`로 캐시 소유권을 명시적으로 회수한다. 기존 프레임이 보유한 계획은 독립적인 shared owner로 계속 유효하고 다음 세션은 같은 내용을 다시 생성한다. `ShutdownLive`는 양쪽 백엔드의 최종 소비가 확인된 뒤 draw의 차용 주소·핀 테이블·정적 캐시를 순서대로 해제한다. GPU 종료 증명이 실패하면 기존 소유권 보존 분기로 돌아간다. 파일 공유 제한을 낮추거나 임시 디렉터리 회수 실패를 무시하지 않는다.

## 기존 호출자와 회귀 배선

자동 Scene cook과 런타임 재질 stage는 `MaterialGraphProduct::VerifyProduct`를 통해 공통 컴파일러를 소비한다. Editor 재질 저작·기본 렌더 pass·환경 생성기도 같은 컴파일러를 사용한다. 호출자별 우회나 DX12 전용 cook 분기는 추가하지 않았다. AssetCooker·Player·Editor의 Release 산출물과 새 로컬 SDK로 후속 제품 검증을 수행한다.

BuildTool의 `GamePackager`는 cook·병합 후 검사·전체 파일 지문 대조로 이어진다. `RuntimeBootstrap`도 같은 cook 함수를 사용하며, 모델을 만들지 않는 bootstrap 분기와 별도 문서 생성 계약은 유지한다. 실제 AnimationPlayer 패키지는 이 공통 `GamePackager` 경로로 검증했다.

`verify-shader-verified-cache.ps1`에 기능 계약 회귀를 추가했다. DXIL/SPIR-V reflection 동등성, SPIR-V 1.3 헤더, 기본 계약 밖의 ray-query 기능 요구가 `E41012`로 거부되는지와 실패 시 기존 verified 결과 보존을 검사한다. 기존 메모리/디스크 hit·재시작·include/profile/옵션/permutation 무효화·손상 복구·동시 요청 회귀도 함께 실행한다. standalone probe는 현재의 명시적인 고전 헤더 계약을 사용하도록 `CE_CONTRACT_STANDALONE=1`을 지정한다.

애니메이션 소비자는 기존 smoke·HTTP 판정을 유지하고 실행 PID와 smoke 원문 자료를 `verification.json`에 남긴다. F# fixture GameScripts·공통 관리 런타임·수명/핸들 계약은 유지한다.

소비자 회귀는 씬69엔티티, Walk의 프레임·클립 시간·본 팔레트 지문 변화에 더해 정상 종료0·stderr0·RHI/작업 큐 drain과 해당 PID의 런타임 디렉터리 제거까지 요구한다. smoke의 준비 성공만으로 서비스의 수명 검증을 대신하지 않는다.

fixture 빌더에는 `verify-animation-fixture.ps1`을 연결해 모델 저작 후, 긴 cook 전에 씬의 모델9개·메시 참조12개 소유 관계, Walk 제공 여부, 재질 Graph12개와 텍스처의 현재 소유 신원을 검사한다. 모델 저작이 끝난 Assets 경로를 명시적으로 받는다. 이 검사는 해당 fixture의 사전 점검이며 범용 씬 파서를 대체하지 않는다. 최종 의존성 밀봉은 계속 쿠커가 책임진다.

종료 호출자는 Player의 정상/부분 초기화 정리와 Editor 종료 경로에서 공통 `ShutdownLive`로 이어진다. 현재·이전 포즈의 Scene 입력은 모두 같은 메시 분할 캐시를 소비한다. 캐시 회수 회귀는 정점 형식8종에서 기존 프레임의 유효성, 새 세션 재생성, 마지막 프레임 소유권 해제와 반복 종료를 검사한다.

Tools·엔진·Editor·ScriptCore/F# 실행 소스와 Dynamic_CPP의 씬/Prefab에서 위 이전5개 GUID의 추가 참조가 없음을 확인했다. 과거 실패 입력과 로그는 Build 아래 별도 증거로 보존한다.

## 실행 결과

| 실행 | 결과 |
|---|---|
| 수정 전 canary | 실제 Scene cook과 작은 비균일 텍스처 접근 셰이더에서 `E41012` 재현 |
| 컴파일러/캐시 | codec 회귀·프로브 **44검사**, 별도 두 프로세스의 동시 요청과 재시작 통과. 새로운 기능 계약 검사 5건 포함 |
| 실제 자동 Scene cook | Release 두 재질 등급, 산출물5개·반복 바이트 동일. 잘못된 입력6종 거부·기존 출력 보존 |
| 실제 cooked 재질 GPU 소비 | 암호화 PAK/loose 일치, DXIL/SPIR-V stage set 읽기. DX12 **24프레임·114778검사**, GPU 비교 성분12501·런타임 Scene 컴파일0·validation 오류0. Debug Layer/GPU Validation 활성. 소스838파일 drift0 |
| 메시 분할/캐시 수명과 GPU | Release **1264244검사·GPU 비교 성분594828**, 정점 형식8종·포즈3종. 캐시 종료/재생성/프레임 보존40검사 포함. Debug Layer/GPU Validation 활성 |
| 빌드/SDK | Release AssetCooker·Player·Editor 빌드, Script API45 새 로컬 SDK 생성. Player/Editor는 Windows SDK `10.0.26100.0`을 명시 |
| fixture PS5/7 | 양 셸에서 현재 입력 통과, 이전 GUID·없는 메시·다른 모델의 메시·Walk 없는 Motion·이전 Code 재질·이전 텍스처·잘못된 Graph·이전 layer 필드인 변이8종씩 모두 거부. 모델 저작 후 Assets에서69엔티티·12Graph를 점검 |
| BuildTool | **188검사** 통과. 필수 동반 파일 누락·잘못된 지문/GUID 경로·고아 동반 파일·은퇴/혼합 형식·병합 중 동일 크기 변조/누락 거부 포함 |
| 전체 cook의 실제 출력 재판독 | 20모델·73재질, 산출물269개·동반 파일421개·전체691개를 수정한 검사기로 수용 |
| 최종 애니메이션 패키지 | 새 SDK `6565e208-d463-421f-8008-c8e6a8faacf2`의 일반 `package-game` 절차 통과. 9모델·14재질 프로그램·산출물97개, CEMF121항목/208원본 신원. smoke284프레임·화면 승격2회·실제 Game 화면 제출5회·파서 호출0 |
| 실제 애니메이션 소비자 PS5/7 | 각 독립 smoke와 HTTP 서비스 통과. 씬69엔티티·Walk·54본·스킨 메시4개, 프레임/시간/팔레트 지문 진행. 각 정상 종료0·stderr0·RHI/작업 큐 drain·자기 PID의 런타임 디렉터리 회수 통과. 실행한 Player 바이너리가 최종 패키지와 일치하는지 대조 |
| CLI 소비자 PS5/7 | 각82검사, 합164. 래칫 상한0·관측0 유지 |

최초 재시도는 긴 SDK 경로에서의 .NET 파일 매핑 오류와 기존 프로젝트의 이전 SDK pin 불일치로 패키징을 시작하지 못했다. 짧은 독립 SDK 경로를 사용하고 기존 `select-engine` 절차로 fixture pin을 갱신했다. 새 SDK를 선택한 실제 cook 결과를 위 제품 수용의 기준으로 삼는다.

전체 cook 증거와 최종 Player 실행 입력을 구분한다. 전체20모델·73재질의 성공 출력은 별도 보존했고, 최종 Player 패키지는 기존 `--asset-list`로 동일 씬이 참조하는9모델·12메시와 필요한 입력을 선택한다. 선택 목록·패키지 manifest·SDK 신원은 동명 JSON에 남긴다. 기존 씬의 엔티티·골격·Walk 선택과 애니메이션 판정 기준은 유지한다.

최종 패키지와 두 셸의 실행은 F# 어셈블리 로드·등록도 확인했다. fixture에는 이 스크립트를 붙인 컴포넌트가 없으므로 해당 스크립트의 생명주기 실행을 수용했다고 주장하지 않는다. 패키지 smoke의 화면 제출과 본 팔레트 진행은 실제 소비 증거이며 포즈의 픽셀 비교나 일정한 프레임률 측정은 아니다.

## 수용 경계와 남은 작업

이 검증은 기존 CreatorRobot 클립 소비 경로에 한정한다. Animation Graph AG0~AG5, 100체 상태 격리, Root Motion·IK·Notify 신규 기능, 성능 개선의 수용으로 확대하지 않는다.

LC9의 실제 Inspector pending 종료/native close/5분 soak, 일반 GUI Undo/Redo, 전체 서비스/Shipping 수용은 별도 잔여다. 오디오 100회 프로세스 종료도 이번 범위가 아니다. 원격 CI, 커밋·푸시·공개 SDK 게시는 수행하지 않는다.
