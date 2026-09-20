# profiling-workers — 워커 계측 게이트의 fixture 씬

`Invoke-ProfilingValidation.ps1 -Action Workers` 가 소비한다.

## 왜 있나

PHASE 14 P2 로 애니메이션 워커에 계측이 붙었는데, **그 계측이 통째로 죽어도
붉어지는 게이트가 없었다.** 기본 씬에는 애니메이터가 없어 잡이 0 개이고, 그러면
등록된 워커 여덟의 칸이 빈 채로 초록이기 때문이다. 자극이 없으면 단정도 없다.

애니메이션이 도는 씬은 전부 `.gitignore` 의 전면 `*.creator` 안에 있었다 — 즉 이
기계에만 있었다. 그래서 저장소가 소유하는 fixture 를 하나 세웠다.

## 무엇이 들었나

빈 `Animator` 컴포넌트 여덟 개. 그뿐이다 — 모델도, 텍스처도, 애니메이션 클립도
참조하지 않는다(10 KB).

그래도 워커가 자극되는 이유는 계측이 **잡 람다의 첫 줄**에 있기 때문이다.
`AnimationJob::Update` 는 활성 애니메이터마다 잡을 하나씩 제출하고, 그 잡은
스켈레톤이 없으면 바로 돌아 나온다 — 스코프는 그 전에 이미 찍혔다. 그래서
"워커가 돌았는가" 를 재는 데에는 자산이 한 톨도 필요 없다.

★ 이 씬으로는 애니메이션 **결과**를 검증할 수 없다. 포즈·블렌드·스키닝을 재려면
모델이 딸린 씬이 따로 필요하다. 이 fixture 는 **계측의 생사**만 판정한다.

## 어떻게 만들었나

제품 저작 경로로 만들었다. 손으로 쓰지 않았다 — 손으로 지은 fixture 는 제품이
실제로 내는 것과 어긋나도 아무도 모른다.

```
scene.new ProfilingWorkerFixture
scene.populate 8
component.add W7_0 Animator      (W7_0 .. W7_7)
scene.save <이 디렉터리>/ProfilingWorkerFixture.creator
```

다시 만들어야 하면 같은 순서를 `--commandlet-script` 로 돌리면 된다.

## 추적

`.gitignore` 의 전면 `*.creator` 에 대해 이 디렉터리만 예외로 열려 있다
(`!Tools/regression/fixtures/profiling-workers/*.creator`). `git add -f` 로
규약을 우회해 들어온 파일이 아니다.
