# ND-a 네트워크 계약 v1과 변경 절차

2026-10-22 · 사내 소유 · C++20 · `Engine/Network/Contracts`

이 문서는 서버 작업자에게 넘기는 6종 계약의 의미와 변경 절차를 고정한다. native 로컬 인터페이스와 wire 값은 구분한다. 생산 transport/registry와 엔티티 관찰자는 각각 N4/N2/ND-b에서 구현한다. 기존 N3의 `SimulationExchange`는 로컬 명령/커밋 경계이며 패킷이나 wire ID가 아니다.

## 동결한 6종

| 헤더 | 확정한 경계 |
|---|---|
| `NetWireIds.h` | Object/Peer/Session/Tick은 uint64, component type은 uint32, message/field는 uint16의 별도 값 타입. 0은 ID 무효값이며 tick 0은 baseline 없음/세션 시작에 사용 |
| `INetworkTransport.h` | 메시지 단위 복사 Send/Receive, 명시적 lane/delivery, 연결 상태·통계·drain. 구체 socket/library/암호화/큐 임계는 외부 adapter 소유 |
| `NetProtocolHeader.h` | 36바이트 little-endian 헤더, 제한 길이 Reader/Writer, 완전한 단일 메시지 decode와 실패 출력 초기화 |
| `INetChannelPolicy.h` | handshake/lifecycle/확정 RPC는 reliable ordered, snapshot/input은 unreliable sequenced, chat은 별도 낮은 우선순위. asset/patch는 replication lane 금지 |
| `INetTelemetrySink.h` | enqueued/applied/dropped/pending/invalid/unauthorized, bytes·encode/decode nanoseconds·allocation·queue depth의 값 이벤트 |
| `INetworkEntityRegistry.h` | GT Bind/Rebind/Unbind/Resolve. DDOL/씬 이송은 같은 ObjectId와 증가 revision, 오래된 binding은 변경 불가, Unbind 후 tombstone으로 부활 차단 |

레코드는 검토된 고정 폭 정수·enum·값 레코드 필드만 허용한다. 포인터·reference·size_t·플랫폼 폭 정수·STL 소유 컨테이너·로컬 엔티티 핸들·HashedGuid·YAML/JSON은 wire에 들어갈 수 없다. 로컬 `NetEntityToken`/`NetSceneToken`은 ND-b adapter가 해석하는 숫자 토큰이며 wire 레코드에 포함할 수 없다. `std::span`은 로컬 함수의 즉시 빌린 버퍼로만 사용한다. 가상 함수 표와 span 표현을 네트워크로 직렬화하지 않는다.

검사는 8종 wire 값의 wire/local 구분을 lock 수정만으로 뒤집을 수 없게 한다. 새 public network header도 검토된 catalog에 먼저 등록해야 하며, 현재 목록 밖의 header는 실패한다. 엔진 핸들을 해석하는 로컬 구현은 `Engine/Network/Adapters`에 분리한다. adapter에 wire 레코드를 숨기거나 local 토큰을 payload로 쓰는 것은 계약 위반이며 N2/N4 소비자 검증에서도 다룬다. 새 wire catalog/구문은 검사 코드·canary·버전을 함께 확장한다.

## 메시지 헤더

| offset | 바이트 | 필드 |
|---:|---:|---|
| 0 | 4 | magic — `43 45 4e 54` (CENT) |
| 4 | 2 | protocol version — 초기 1 |
| 6 | 2 | nonzero MessageTypeId |
| 8 | 8 | nonzero CompatibilityId — 초기에는 같은 배포 schema만 허용 |
| 16 | 8 | ServerTick |
| 24 | 8 | BaselineTick — 0 또는 ServerTick 이하 |
| 32 | 4 | PayloadBitCount |

host 구조체 padding과 endian을 복사하지 않는다. EncodeHeader/DecodeMessage를 사용한다. Decode는 정확히 36+ceil(bits/8)바이트를 요구하며 마지막 바이트의 사용하지 않는 상위 bit는 0이어야 한다. 최대 payload bytes는 호출자가 제공한다. 짧은 입력·다른 magic/version/schema·ID 0·잘못된 baseline·초과 payload·trailing bytes·잘못된 padding은 실패한다. 실패 시 header는 기본값, payload는 빈 span이다. 알 수 없는 nonzero message ID의 payload dispatch는 N2/N4 allowlist에서 추가로 거부한다.

transport의 Send Ok는 payload 전체가 복사되어 접수되었다는 뜻이며 상대 수신/적용 ack가 아니다. Full은 하나도 접수하지 않는다. Receive BufferTooSmall은 필요한 bytes/metadata만 반환하고 메시지를 소비하지 않는다. Empty/실패는 이전 metadata를 남기지 않는다. BeginDrain 이후 신규 Send를 거절하며 이미 접수한 송신을 처리한 뒤 Closed가 된다. 강제 Disconnect는 미처리 메시지를 버리고 통계에 반영한다. 소멸 전에 소유자가 pump/drain 및 작업자 종료를 마친다. 호출은 하나의 transport 소유 스레드에서 비차단으로 수행한다.

Registry는 세션마다 새 인스턴스를 소유한다. Bind는 살아 있는 유일한 object/token 쌍만 받는다. Rebind와 Unbind는 object/entity/scene/revision 전체의 expected binding을 비교한다. 실패한 출력 binding은 기본값이다. tombstone과 revision을 wrap/evict해 오래된 ID가 재사용되는 정책은 금지한다. 용량 소진은 Full이다. 이 계약은 엔진 Entity 주소나 EntityHandle을 외부로 내보내지 않는다.

## 정본과 ID 규칙

- `NetWireIds.registry.json`이 static message/component/field ID 및 tombstone의 정본이다. 문서 표나 코드 주석에 별도 ID 정본을 만들지 않는다. v1은 message 1=Handshake만 할당하며 실제 replicated type/field schema는 N2에서 배정한다. static ID는 각 domain 안에서 고유하다.
- `history`는 할당/폐기의 append-only 원장이다. 기존 항목의 이름/번호/동작을 고치거나 삭제하지 않는다. 폐기한 번호를 다시 할당하지 않는다. active/tombstones는 원장을 재생한 결과와 같아야 한다. 이름을 바꾸려면 새 번호 할당과 이전 번호 폐기, 호환 변경을 함께 검토한다.
- 런타임 NetworkObjectId는 세션 안에서 단조 증가하는 서버 발급 ID다. 0과 폐기 번호를 재사용하지 않는다. 세션 간 ID는 SessionId/연결 handshake로 구분한다. SessionId 교환 payload 및 인증은 N4 범위다.
- `net-contracts.lock.json`은 contract/protocol version, 레코드 필드 순서·타입·이름, enum/class 목록, 정본 ID 상수, 6종 헤더와 ID 원장의 SHA-256을 고정한다. 해시는 LF로 정규화하여 CRLF checkout에서 같은 판정을 낸다.

## 계약 변경 절차

1. 담당자는 변경 이유·영향 소비자·이전/새 wire byte 예·실패/이행 방안을 제시한다. 사내 계약 소유자가 변경을 검토한다. 이 최초 v1 구현은 사용자가 요청한 ND-a 확정 범위다.
2. 6종 헤더 또는 ID 정본 변경은 `contract_version`과 lock의 `contractVersion`을 증가시키고 changes에 새 기록을 추가한다. 이전 changes/history는 수정하지 않는다. 기존 wire 필드의 폭/순서/의미 또는 기존 ID의 폐기/의미 변경은 `protocol_version`도 증가시킨다. schema/allowlist 추가는 배포 CompatibilityId를 갱신하고 mixed-version 접속을 거절한다.
3. 새 ID/폐기는 registry history에 추가하고 active/tombstones를 일치시킨다. replicated quantizer·snapshot 배치 크기·네트워크 backpressure 임계는 N0 계측 전 확정하지 않는다. N3 로컬 큐의 이미 수용한 상수는 네트워크 전송 임계로 복사하지 않는다.
4. 실제 헤더 변경과 새 버전에 맞춰 lock의 필드/상수/해시를 갱신한다. 해시만 바꾸어 같은 버전으로 통과시키지 않는다. wire에 새 선언 구문이 필요하면 whitelist parser와 canary를 함께 검토한다. 일반 C++ 구문을 자동으로 안전하다고 추정하지 않는다.
5. `verify-net-wire-boundary.ps1 -Configuration All -BaseRef <검토 기준 커밋>`와 `run-all.ps1 -Suite NetworkHandoff -Configuration All`을 통과시킨다. 실제 adapter 변경이면 N4/N2 제품/loopback gate도 추가한다. 로컬 BaseRef 기본값은 HEAD, CI는 PR base 또는 push 직전 커밋이다. 기준 커밋을 읽을 수 없으면 실패한다.
6. 인계 문서·대시보드·증거의 코드 해시를 갱신한 변경을 함께 제출한다. 원격 CI/실제 서버 수용을 실행하지 않았다면 별도로 미실행이라고 기록한다.

## 경계 검사와 전체 회귀 배선

`verify-net-wire-boundary.ps1`은 6종 파일 목록·include·선언·wire 필드·ID 원장·동결 해시·기준 커밋 대비 변경 절차를 검사하고, Debug/Release 독립 include 및 실제 계약 소비자를 컴파일/실행한다. 금지 타입, alias, 숨은 header/include, ID 재사용, 버전/원장 수정, 필수 회귀 등록 제거 canary를 격리 복사본에서 검증한다. endian을 뒤집은 native 소비자도 golden byte 단정에서 실제 실패해야 한다.

기존 무범위 `run-all.ps1`은 폐지된 상태였으므로 이번 진입점은 필수 `-Suite NetworkHandoff`로 범위를 고정한다. `network-handoff.suite.json`의 모든 필수 gate(ND-a, N3, physics ABI/identity, lifecycle enum)를 실행하고, ND-a 누락/우회/실패를 성공으로 표시하지 않는다. `-Product -EditorExe <명시 경로>`는 실제 Editor N3 소비자 gate를 추가한다. 이 등록 세트 전체 통과가 모든 렌더/GPU/장치/soak 검사의 통과를 뜻하지 않는다.

`.github/workflows/network-contracts.yml`은 관련 변경의 PR/push에서 같은 Debug/Release 등록 세트를 호출하고 결과를 보관한다. 원격 실행은 별도의 CI 결과로 확인한다.

[현재 네트워크 계획](../plans/NetworkFrameworkPlan.md) · [N3 로컬 틱 계약](SimulationTickExchangeContract.md)
