# UI 회귀 세트

에디터로 손수 확인하면 놓치는 것들을 기계로 잡기 위한 검사 묶음이다.
전부 종료 코드로 판정하므로 CI에 그대로 걸 수 있다.

## 전체 실행

```powershell
pwsh Tools/regression/run-all.ps1
```

빌드된 `x64\Debug\Academy_4Q.exe`가 필요하다. 다른 위치를 쓰려면 `-Exe`로 넘긴다.

## 개별 검사

| 검사 | 무엇을 지키는가 |
|------|-----------------|
| `ui_regression.txt` | 비정상 순서로 UI를 만들고 재생/정지를 반복한다. 캔버스 없이 UI를 먼저 만들거나 캔버스를 나중에 붙이는 경로 — 에디터에서 정상 순서로 만들면 절대 드러나지 않는 크래시가 여기서 나온다. |
| `verify-authored-rects.ps1` | 런타임이 계산한 배치가 에디터가 저장해 둔 배치와 같은지. worldRect를 더 이상 직렬화하지 않으므로 런타임 값은 앵커·크기·피벗에서 처음부터 유도된 것이고, 프리팹에 남은 옛 `m_worldRect`가 정답지 역할을 한다. |
| `verify-resolution-sweep.ps1` | 해상도를 바꿔 가며 캔버스가 화면을 따라오는지, 배율이 uGUI와 같은 로그 보간 값인지, 자식 크기가 배율을 따르는지, 버튼의 클릭 판정 상자가 보이는 사각형과 같은지. 16:9 축소·4:3·21:9·세로형·복귀까지 7단계. |
| `verify-shutdown-order.ps1` | 첫 프레임이 만들어지기 전에 종료를 걸어, `Dx11Main::Finalize`가 렌더 스레드(CB/CE)를 완전히 세운 뒤에야 렌더 씬을 해체하는지. 순서가 뒤집히면 커맨드를 만드는 중에 발밑에서 자료구조가 사라진다. 확률적이라 6회 반복한다. |
| `verify-crash-dump.ps1` | `crash.test`로 일부러 죽여 크래시 경로(AV·abort·미처리 예외)가 실제로 `.dmp`와 심볼 붙은 스택을 남기는지. 덤프 코드는 크래시가 나야만 실행돼서 평소엔 아무도 확인하지 않고, 그래서 조용히 망가져 있었다 — 로그에 CRASH 줄만 남고 덤프가 통째로 없는 크래시가 실제로 있었다. |
| `verify-lifecycle-baseline.ps1` | 생명주기가 누구를 어떤 순서로 부르는지(PHASE 9-0). 지금 순서는 델리게이트의 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없고, PHASE 9가 그 기구를 통째로 바꾼다. 교체 전에 기준선을 떠 두어야 교체 후 "동작이 같다"를 주장할 수 있다. 기준선 파일이 없으면 `run-all`이 이 항목을 건너뛴다. |

## 생명주기 기준선 뜨기 (PHASE 9-0)

PHASE 9 교체 **전에** 한 번 떠서 커밋해 둔다. 교체 후에 뜨면 비교 대상이 사라진다.

```powershell
pwsh Tools/regression/verify-lifecycle-baseline.ps1 -Baseline
```

이후 9-1~9-3 각 단계 뒤에 인자 없이 실행하면 기준선과 대조한다.
인스턴스 ID와 프레임 번호는 실행마다 달라지므로 비교에서 뺀다 — 남는 것은
(단계, 타입, 오브젝트 이름)의 **순서**이고, 그것이 생명주기의 계약이다.

## 두 디스패치 경로 (PHASE 9-1)

전환기 동안 생명주기 디스패치 경로가 둘이다. 기준선도 경로마다 따로 둔다.

```powershell
pwsh Tools/regression/verify-lifecycle-baseline.ps1            # 델리게이트(기존)
pwsh Tools/regression/verify-lifecycle-baseline.ps1 -Registry  # 레지스트리(신규)
```

경로 선택은 **기동 인자** `--lifecycle-registry`다. CLI(`lifecycle.registry on`)로도 바꿀 수 있지만,
CLI는 엔진이 다 선 뒤에 열려서 기본 씬의 `Main Camera`·`Directional Light`가 이미 옛 경로에
등록된 뒤다. 그 상태로 바꾸면 그 둘만 다른 규약을 따른다 — A/B 대조가 실제로 그 어긋남을
사건 2건의 차이로 잡아냈다. 그래서 검증 스크립트는 기동 인자를 쓴다.

**두 경로는 같은 사건을 내지만 단계 안의 순서가 다르다.** 델리게이트 쪽은 우선순위 정렬
삽입(`lower_bound` + `>` 비교자)이 같은 우선순위에서 항상 맨 앞에 꽂아 **등록 역순**으로
돌았다 — 설계된 계약이 아니라 자료구조에서 나온 부수 효과다. 레지스트리는 등록 순서로 돈다.
기준선을 하나로 두면 어느 쪽을 고쳐도 나머지가 깨져 결국 무시되므로 파일을 나눴다.

검증 실패 시 출력은 **"사건이 빠졌다"와 "순서만 다르다"를 갈라서** 보고한다. 전자는 결함이고
후자는 설계 판단이라 성격이 다르다.

## AddressSanitizer 빌드 (PHASE 9-0 / 0-5)

```powershell
msbuild CreatorEngine.sln /p:Configuration=Debug /p:Platform=x64 /p:EngineAsan=true
```

솔루션 구성을 늘리지 않고 스위치로 켠다(`Directory.Build.targets` 참조).

**주의 — 이것을 모르면 ASan이 아무것도 잡지 못한다.** 엔진의 `GameObject`·`Component`는
전부 `shared_alloc` → `MyAlloc` → `mi_malloc`을 지나는데, mimalloc은 ASan의 가로채기
바깥이라 그 메모리는 ASan에게 존재하지 않는다. `EngineAsan=true`가 `ENGINE_ASAN`을
정의해 `MemoryManager.cpp`가 CRT `malloc/free`로 돌아가게 하는 이유가 그것이다.
이 우회 없이 ASan을 켜면 컴포넌트 UAF를 한 건도 못 잡은 채 "무사고"로 보고된다.

실행 전에 런타임 DLL과 옵션을 챙긴다.

```
copy "%VCToolsInstallDir%bin\Hostx64\x64\clang_rt.asan_dynamic-x86_64.dll" x64\Debug\
set ASAN_OPTIONS=detect_leaks=0:halt_on_error=1:abort_on_error=1
```

## 검사가 조용히 건너뛰지 않게 하기

`verify-resolution-sweep.ps1`은 히트박스 단정이 **한 건도 실행되지 않으면 실패**로 끝난다.
버튼이 없는 프리팹을 띄우는 바람에 그 단정이 통째로 건너뛰어지고도 "전체 통과"가
나온 적이 있어서 넣은 장치다. 검사를 추가할 때도 같은 원칙을 지킬 것 —
"확인하지 못했다"와 "확인했고 문제없다"는 다르다.

## 검사가 실제로 실패하는지 확인하기

기대값을 일부러 틀리게 주면 실패해야 한다.

```powershell
pwsh Tools/regression/verify-resolution-sweep.ps1 -RefWidth 1280 -RefHeight 720
```

기준 해상도를 거짓으로 주었으므로 배율 단정이 전부 실패하고 종료 코드 1이 나온다.
이게 나오지 않으면 검사가 아무것도 보고 있지 않다는 뜻이다.

## 아직 없는 것

렌더된 픽셀을 직접 대조하는 시각 회귀는 없다. 백버퍼 캡처를 붙이려 했으나
게임 스레드에서 `DirectX::CaptureTexture`가 죽어(0x0000087A) 보류했다 —
렌더 루프의 안전 지점에서 실행해야 한다. 그때까지 UI 렌더 좌표는 숫자
대조와 수식 동등성으로만 검증된다.
