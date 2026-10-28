# 파티클 프레임 전달·렌더 기여 계약

> 2026-10-24 · Phase 10-1c · 값 snapshot과 입력 경계 수용, 파티클 실행·그리기는 후속

[Phase 10 계획](../plans/ParticleEffectRedesignPlan.md) · [저작 정의](ParticleEffectAuthoringContract.md) · [수용 기록](../analysis/Phase10TransportAcceptance20261024.json)

## 연결 경로

`SceneManager의 성공한 고정 tick → IEffectSystem → EffectState → 불변 EffectFrame → EnhancedLiveFramePacket → 뷰별 EnhancedFrameContext → Effect.HDRBoundary`

소스는 현재 작업 트리의 `Engine/RenderEngine/Effects/EffectFrame.h`, `IEffectSystem.h`, `Engine/Effect/EffectState.{h,cpp}`, `EffectRenderContributor.{h,cpp}`다. 아직 게시된 커밋의 소스 링크로 제공하지 않으며 수용 기록의 파일 해시로 신원을 구분한다.

- 고정 tick의 소유자는 기존 SceneManager/SimulationTick이다. 물리·관리 tick transaction이 성공해 committed가 증가한 뒤 Effect에 `(SceneContextEpoch, committed tick, fixed seconds)`를 한 번 전달한다. 렌더 발행·뷰 순회에서는 tick을 호출하지 않는다.
- 0 delta·중복·역행·누락 tick과 다른 scene epoch는 거절한다. 일시정지 시에는 기존 엔진 경로가 고정 tick을 실행하지 않는다. 이 경계는 엔진의 실제 committed tick을 전달하며 저작 EffectClock의 배율·서브스텝 실행과 입자 적분을 구현한 것은 아니다.
- SceneContextEpoch가 증가하면 instance·tick·버스트 journal을 비운다. 같은 epoch는 상태를 초기화하지 않고, 0이나 이전 epoch로 돌아가지 않는다.
- RenderEngine의 sceneEpoch와 SceneManager의 SceneContextEpoch는 서로 다른 신원이다. parent 렌더 packet은 기존 렌더 epoch 검사 대상이며 EffectFrame은 자신의 context epoch를 보존한다. 두 숫자의 우연한 일치를 가정하지 않는다.

## 핸들과 값 소유권

핸들은 `(context epoch, slot index, generation)`이다. index 0은 무효다. 슬롯을 재사용할 때 generation을 증가시키며, 세대가 소진된 슬롯은 재사용하지 않는다. 등록·transform 갱신·버스트·해제는 게임 스레드의 EffectState만 수행한다. 정의 ID 형식은 10-1b의 검사 함수를 재사용한다. 에셋 해석과 실제 정의 로드는 아직 하지 않는다.

프레임은 instance ID·핸들·행렬·누적 방출 요청 개수와 버스트 레코드를 값으로 복사한다. `own::shared_owner<const EffectFrame>`만 렌더 packet에 실린다. 이전 프레임을 새 값으로 덮어쓰지 않으며 컴포넌트 주소나 EffectState 포인터를 렌더 스레드로 넘기지 않는다. 오래된 핸들은 새 명령 대상으로 거절하되, 이전 프레임이 소유한 과거 값은 읽을 수 있다.

Scene epoch의 읽기 전용 atomic owner도 기여자가 따로 보유한다. 새 scene epoch와 shutdown은 이 신원을 갱신한다. 이전 scope의 새 선언·기록은 건너뛰며, 렌더 기여자는 소멸한 EffectState를 조회하지 않는다. 이미 제출된 GPU 작업의 자원 퇴역은 별도 완료 조건의 책임이다. 현재 Effect는 GPU 버퍼를 할당하지 않으므로 particle buffer fence 수용을 주장하지 않는다.

## 버스트를 latest-wins로 잃지 않는 규칙

단순 누적 개수만으로는 서로 다른 위치·tick에서 들어온 버스트를 재현할 수 없다. 각 수용 요청은 sequence·핸들·당시 행렬·당시 committed tick·개수를 가진 값 레코드가 된다. tick 필드는 요청 수용 시점의 cursor이며 실제 GPU 방출 실행을 의미하지 않는다.

- 최신 snapshot도 아직 인수 확인되지 않은 모든 레코드를 포함한다. 렌더 queue가 중간 packet을 병합해도 요청의 개수·위치·시점을 지우지 않는다.
- scene당 미처리 레코드 한도는 4096이다. 한도에 도달하면 새 Burst 요청을 false로 거절하고 기존 요청을 버리지 않는다. 0개 요청은 이벤트를 만들지 않는다.
- 실제 simulation submission을 인수한 소비자만 해당 프레임의 `AcknowledgeSubmittedBursts`를 호출할 수 있다. 오래된 view의 확인은 더 새로운 레코드를 지우지 않으며, 이전 scene의 확인은 새 scene의 journal에 영향을 주지 않는다.
- 확인은 GPU 완료 fence를 대신하지 않는다. 소비자는 sequence high-water를 사용해 이미 인수한 레코드를 다시 실행하지 않아야 한다. 현재 HDR 관측 노드는 simulation을 제출하지 않으므로 확인 함수를 호출하지 않는다.
- 명시적 instance 해제는 그 instance의 미제출 요청을 취소한다. Scene 전환·shutdown도 해당 scope의 요청을 취소한다. 이전 snapshot의 복사본 자체는 변하지 않는다.

## 렌더 기여와 수명

Session 초기화가 독립 소유 기여자를 설치한다. 파이프라인 조립은 mutex 아래에서 복사한 기여자 owner를 보유한다. Editor의 기존 overlay 목록 교체가 Effect 기여자를 지우지 않는다. Session 해제는 새 조립에 사용할 기여자를 끊고, 설치된 파이프라인·노드·프레임은 각 owner로 안전하게 정리한다.

현재 기여자는 `BeforePostProcess`에서 `Scene.LitColor`와 GBuffer 깊이를 typed read 입력으로 선언한다. `Effect.HDRBoundary`는 정확한 snapshot을 기록 callback까지 보유하는 읽기 전용 경계다. HDR 값을 바꾸거나 입자를 그리지 않는다. material preview에는 Effect snapshot을 넘기지 않는다. snapshot 선언·기록 카운터는 CPU graph recording 관측이며 GPU simulation 성공이나 성능 수치가 아니다.

이 지점은 최종 파티클 합성 순서를 확정한 것이 아니다. 실제 draw는 10-3에서 반투명 정렬·pre-temporal 입력·reactive/motion 계약을 함께 닫아야 한다. GPU simulation의 ordered step 소비·진행 신원·submission 인수와 particle buffer 퇴역은 10-2b가 소유한다. 현재 snapshot의 tick/time만 보고 렌더 delta로 입자를 적분하지 않는다.

## 실행한 검증과 재현

- 실제 Effect 라이브러리 소비자의 Debug·Release에서 전송·수명 검사 56개씩, 기존 저작 스키마 검사 51개씩 통과했다. 다중 view 캡처의 tick 불변, duplicate/gap 거절, ABA·epoch, 독립 epoch token, immutable 과거 프레임, 다른 스레드의 쓰기 거절, per-event 위치/tick, 한도·제출 확인·해제·shutdown을 확인한다.
- Debug Editor·Player의 패키지 포함·제외 4조합을 솔루션 빌드·실행했다. 참조·link map·종료 코드와 포함 시 HDR 기록을 검사했다. Player는 실제 scene reload 후 새 composition 제출까지 통과했다.
- Editor에서 네이티브 window resize 전후 실제 렌더 캡처 크기가 바뀌고 HDR 경계 기록이 이어지는 것을 확인했다. Scene/Game view 캡처는 모드를 명시해 저장된 workspace 설정과 분리한다.
- private camera/light fixture를 재사용했다. 새 파티클 콘텐츠, 수치 시뮬레이션, particle GPU validation, Release/Shipping 제품 실행, 성능 측정은 아직 수용하지 않았다.

```powershell
pwsh -NoProfile -File Tools/regression/verify-effect-transport.ps1 `
  -FixtureProject <private-project> -CookedPak <private-GameAssets.pak> `
  -OutputDirectory artifacts/phase10-transport-new
```

출력은 새 폴더를 사용한다. 기본 실행은 네이티브 검사·제품 4조합·resize를 수행한다. 이미 실행한 제품 행렬을 재사용할 때에는 `-ProductReceiptPath`를 지정할 수 있으며 현재 설치된 포함 host DLL의 해시와 일치해야 한다. 이는 새 빌드라고 표시하지 않고 검증한 바이너리를 식별해 재사용하는 경로다.

10-1a의 최적화 standalone 등록 검사는 당시의 독립 archive 범위에 대한 과거 증거다. 이제 패키지가 RHI 노드를 기여하므로 그 작은 링크 프로브를 전체 패키지 검증으로 재사용하지 않는다. 현재 제품의 참조·map·실행과 실제 라이브러리 소비자의 Debug/Release 검사를 구분한다.
