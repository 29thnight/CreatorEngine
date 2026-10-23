# N1 Archive·Parser 경계 수용 — 2026-10-22

N1 사내 경계 구현과 기존 호출자 이주를 완료했다. [기계 판독 기록](ArchiveParserAcceptance20261022.json), [경계 계약](../design/ArchiveParserBoundaryContract.md), [네트워크 계획](../plans/NetworkFrameworkPlan.md).

## 구현과 기존 소비자

TemporalMotionFixture.cpp와 EnhancedPbrCapture.h의 직접 ryml tree 소유 2곳을 Utility 내부 Diagnostics::Document writer로 밀봉했다. EnhancedSceneRenderer.cpp의 manifest 호출자도 이주했다. parser/backend 타입을 공개하지 않으며 키/문자열 복사·문자열 인용·bool/숫자 구분과 document 이동 시 node 안정성을 검증했다.

ComponentFactory의 Authoring::NodeView와 scene/prefab/material/behavior-tree/terrain/audio 등의 기존 로컬 archive 읽기·쓰기 facade는 유지한다. 이번 소스 inventory는 ParseText/File/Cooked 호출 66곳을 파일·행·API로 기록한다. 개발/저작 입력에서 텍스트를 허용하는 호출을 무조건 삭제하지 않는다. Network에는 저작/진단 archive를 넘기지 않는다.

## 실행 결과

| 범위 | 결과 | 증거 |
|---|---|---|
| 실제 Editor·Player·AssetCooker·AssetPacker Release 빌드 | 통과 | Build/n1-*-build.log, Build/n1-editor-final.log |
| 실제 production codec·진단 writer | Debug/Release 각각 226검사 통과 | Build/Verification/N1/Contracts-6d039bf4a5794eb5933216e2c88fa6bb |
| 역할별 경계 검사 | runtime 860소스·방문 역할/파일 1014·중립 root 7 | Build/Verification/N1/Contracts-6d039bf4a5794eb5933216e2c88fa6bb/boundary.json |
| backend/include/타입 주입·등록 누락 | 격리 변이 35건 통과, production source 불변 | Build/Verification/N1/Contracts-6d039bf4a5794eb5933216e2c88fa6bb/canaries.json |
| 필수 suite 등록/실패 전파 | ND-a·W4/ND-b·N1 등록 삭제 거부·exit37 전파 | Build/Verification/N1/Wiring/RunnerCanary/result.json |
| NetworkHandoff 전체 등록 회귀 | **13그룹 통과**, 실패 0 | Build/Verification/NDA/Suite-54fb57db264041989367e7f5274c39b4/result.json |
| 새 cooked Player 실제 실행 | parser 호출 0·직접 backend 참조 0·패키지 불변 | Build/Verification/N1/Player-9d8e1dd932f4497cb55b965e1570042b/result.json |
| 실제 DX12 PBR 소비자 | manifest schema/자료형·첨부 7개·실제 byte 크기·유한값·GPU validation 0·정상 종료 | Build/Verification/N1/Diagnostic-43307fbe1b0c4f26a2cde22039266063/result.json |

공통 reflgen schema의 Authoring/CEDO roundtrip과 모든 잘린 입력·변이 header/node flags/trailing bytes·text fallback 거부는 production codec을 사용했다. 별도 Python JSON 소비자가 문자열 true/123과 bool/숫자의 자료형을 확인한다. 이 schema는 wire serializer 구현이 아니다.

Player runtime SHA256: `C69413E74C40F3E021CCA4A67728B3E4306F85B9965C2EFFFC1DE7DA3226AAB4`. Editor runtime SHA256: `D06A92517BD07ADF7352A967028E9F1474308E3A17A769992FB7E25694A7472F`. 상세 소스/도구 hash와 기존 호출 목록은 JSON에 보존한다. SDK를 새로 export한 뒤 isolated project를 package/smoke하고 해당 Stage를 명시해 검증했다.

## 회귀 배선과 수용 한계

NetworkHandoff 기본 9그룹에 실제 제품 4그룹을 더한다. `-Product`에는 `-EditorExe`와 현재 runtime hash가 일치하는 `-CookedStage`가 필수다. CI에는 N1 필수 등록·dependency 설치·실패 결과 업로드를 배선했으며 원격 CI 실행은 확인하지 않았다.

Motion fixture 준비 실패는 현재 바이너리와 변경 전 SDK에서 동일하게 재현했다. 원본 commands.jsonl을 보존했으며 Motion 품질 통과로 계산하지 않는다. 더 넓은 temporal native gate도 Prim_Cone 입력 및 SDK history 조건 때문에 실패했으므로 별도 잔여다. PBR 진단 gate는 실제 출력 schema/첨부 소비 범위이며 일반 픽셀 품질·성능 수용이 아니다.

전체 runtime parser 라이브러리 물리 unlink·Dedicated Server 제품·N2 wire schema/권한·replication/transport는 별도다. 전체 문서 index 검사는 기존 RHI src HTML drift가 남아 있고, 이번 변경의 Markdown/계획 HTML·inline script 검증을 구분해 기록한다.

대상 문서 6페이지의 로컬 링크 134건 누락 0, 전체 문서 inline script 668페이지/348개 실패 0, 계획 3종 HTML 재생성/390·1280 visual check 및 변경 범위 whitespace 검사를 통과했다. 전체 문서 index drift는 이 결과와 구분한다.
