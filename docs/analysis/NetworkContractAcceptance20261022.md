# ND-a 계약 6종·변경 절차·경계 회귀 수용

2026-10-22 · 현재 작업 트리 · Windows x64 · Debug/Release · contract v1 / protocol v1

서버 작업자 인계 전 ND-a를 구현했다. [계약과 변경 절차](../design/NetworkContractChangePolicy.md), `Engine/Network/Contracts`의 6종 헤더, ID 원장·동결 lock이 인계 정본이다. [기계 판독 수용 기록](NetworkContractAcceptance20261022.json)은 최종 소스 SHA-256과 실제 시험 경로를 보존한다.

## 확정한 구현

- 고정 폭의 Object/Peer/Session/type/message/field/tick 값 타입과 36바이트 little-endian 프로토콜 헤더를 제공한다. 제한 길이 Reader/Writer와 완전한 단일 메시지 decode는 version/schema/길이/baseline/padding을 검증한다. host 구조체를 memcpy해 패킷으로 쓰지 않는다.
- transport·channel policy·telemetry·entity registry 인터페이스를 정의했다. 전송은 전체 메시지 복사와 명시적 Full/작은 수신 버퍼 재시도를 사용한다. registry는 전체 expected binding을 비교하는 Rebind/Unbind와 ID tombstone 규칙을 따른다.
- 정본 static ID 원장은 초기 Handshake=1만 할당한다. 할당/폐기 이력은 append-only이며 ID 재사용·무음 이름 변경을 거부한다. 실제 replicated type/field 배정과 생산 registry는 N2 범위다.
- 헤더·ID 원장·필드/선언 inventory를 lock으로 고정하고 변경 시 계약 버전, wire 변경 시 프로토콜 버전, 원장/변경 이력의 보존을 검사한다. 로컬은 HEAD, PR/push CI는 변경 이전 커밋과 비교한다. 기준 커밋을 해석하지 못하면 실패한다.
- 금지 타입, 별칭, 숨은 include/header, 미등록 public network header, wire/local 재분류와 새 필드 유입을 검사한다. whitelist 밖 선언을 안전하다고 추정하지 않는다. 일반 C++ 전체를 해석하는 분석기는 아니므로 새 wire 구문은 검사와 canary를 함께 검토한다.

## 실행한 검증

| 검사 | 결과 |
|---|---|
| 원본 경계 | 6종 헤더, 16개 값 레코드 중 wire 8종, 원장/정본 상수/버전/fingerprint 검사 통과 |
| 독립 include | 각 헤더를 단독으로 Debug/Release 컴파일 — 총12회, MSVC C++20 `/W4 /WX` |
| 실제 헤더 소비자 | Debug/Release 각132단정: golden36바이트, 모든 짧은 헤더, overflow·schema/version·baseline·payload/padding 오류, 빈 payload, 제한 버퍼, lane, telemetry, 로컬 fixture의 copy/drain/registry CAS·tombstone |
| source·원장·등록 canary | 38사례: 금지 타입·별칭·숨은 의존·wire 재분류·미등록 header·ID 재사용·버전/이력 변경과 필수 회귀 등록 누락/우회 검사. 정상 기준선·CRLF·정당한 계약 변경의 양성 사례 포함 |
| native endian 변이 | 격리 복사본에서 writer endian을 반전한 실행 파일이 golden byte 단정으로 실패 — 단순 compile 실패로 대체하지 않음 |
| 실제 회귀 launcher 변이 | 격리 launcher의 ND-a를 exit37로 바꾸면 전체 세트가 실패하고 코드37을 보존. ND-a 등록 제거도 실제 launcher가 거부 |
| 등록 전체 회귀 | NetworkHandoff Debug/Release의 ND-a/N3/physics ABI·identity/lifecycle enum 및 실제 Editor N3 소비자 — 8개 실행 그룹 통과 |

전체 회귀를 연결하면서 기존 PhysicsScriptABI가 API41과 camera-cut을 표의 끝으로 가정하는 것을 확인했다. 현재 API43과 status/submit/read 연속 슬롯·전체 크기·32바이트 값 배치를 검사하도록 이주했다. 물리 POD·기존 슬롯 순서 검사는 유지했다.

## 회귀 진입점과 CI

기존 무범위 run-all은 저장소에서 폐지되어 있었다. 이번 `run-all.ps1`은 필수 `-Suite NetworkHandoff`로 대상 세트를 지정하며 등록 gate를 전부 실행한다. ND-a 검사 누락·우회·실패를 성공으로 처리하지 않는다. 단독 경계 검사는 `verify-net-wire-boundary.ps1 -Configuration All -BaseRef HEAD`다. 실제 Editor 검사는 `-Product -EditorExe <명시 경로>`로 추가한다.

`.github/workflows/network-contracts.yml`은 PR/push에서 같은 등록 세트를 실행하고 증거를 보관하도록 배선했다. 이 작업에서는 로컬 등록 세트를 실행했으며 **원격 GitHub Actions는 실행하지 않았다**. 이 결과를 모든 엔진/GPU/장치/장시간 회귀의 통과로 표시하지 않는다.

## 인계 판정과 잔여

ND-a는 완료이며 N0/N4의 제한 착수를 열 수 있다. 실제 socket/loopback transport, schema/권한, 생산 entity registry와 ND-b 수명/쓰기 관찰, full snapshot, N1 경계 회귀, N3 실제 FPS 품질 수용, N9 서버 제품은 별도다. native transport/registry 소비자는 계약을 구현한 시험 fixture이며 N2/N4 생산 구현 완료가 아니다. 기존 N3 C# API43/엔진 tick 동작은 변경하지 않았다.

커밋/푸시는 수행하지 않았고 기존 다른 작업의 변경을 보존했다.

[네트워크 계획](../plans/NetworkFrameworkPlan.md) · [N3 수용 기록](N3ExchangeAcceptance20261022.md)
