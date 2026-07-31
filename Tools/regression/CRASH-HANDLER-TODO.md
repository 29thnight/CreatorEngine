# 종료 시 크래시 · 덤프 누락 (다음 작업)

회귀 세트를 흔들고 있는 문제. 두 층으로 나뉜다.

## 층 1 — 덤프가 안 남는다 (이쪽이 먼저)

크래시 핸들러가 로그는 남기는데 `.dmp` 파일이 생기지 않는다. 덤프가 없으면
층 2(크래시 자체)는 진단이 불가능하므로 이것이 선행 조건이다.

**증거**

`x64\Debug\Log\Editor_20260731_102351.html`
```
10:24:21.508 CRASH - 미처리 예외로 프로세스 종료
             - ACCESS_VIOLATION (잘못된 메모리 접근 - 널/댕글링 포인터 의심)
             @ 0x00007FF774C7C327
```
같은 시각 `x64\Debug\Dump\`에는 2026-07-30자 파일만 있었다. 심볼 스택도
로그에 없다 — `CoreWindow::WriteCrashDump`가 덤프와 스택을 함께 남기게
돼 있는데 둘 다 없다.

**조사 지점**

- `Utility_Framework/CoreWindow.h:126` — `Log::SetCrashDumpWriter(&CoreWindow::WriteCrashDump)`가
  `CoreWindow::SetDumpType` 안에서 호출된다. 즉 `SetDumpType`이 불리지 않으면
  기록자가 등록되지 않는다.
- `Utility_Framework/LogSystem.cpp` `WriteCrashDumpOnce`(≈48행) — `g_crashDumpWriter`가
  널이면 조용히 early return. 여기가 실패 지점일 가능성이 가장 높다.
- EngineEntry(Academy_4Q) 시작 경로에서 `SetDumpType`이 실제로 불리는지 확인할 것.

**요구**

기록자 등록 여부를 시작 시 로그로 남길 것 — 다시는 조용히 실패하지 않게.

## 층 2 — 종료 시 간헐 크래시

`ui_regression` 실행이 프로브를 전부 통과하고도 종료 코드
`0xC0000374`(힙 손상)로 죽는 경우가 있다. 간헐이며 단독 4회 재실행,
이어진 run-all 재실행에서는 재현되지 않았다.

- DX12 코드(PHASE 3-3/3-4)는 이 경로에서 실행되지 않으므로 그 변경과 무관하다.
- `Tools/regression/run-all.ps1`이 이제 종료 코드를 판정에 넣기 때문에
  이 간헐 실패가 회귀 세트를 붉게 만든다. 숨기지 말 것 — 고칠 것.

**재현 시도**

```
x64\Debug\Academy_4Q.exe --script Tools\regression\ui_regression.txt
```
종료 코드 확인. 시나리오는 모델 배치 + 캔버스/텍스트/이미지 생성 +
재생·정지 4회. 크래시는 마지막 quit 이후 종료 구간에서 난다.

## 하지 말 것

RectTransform/Canvas 레이아웃 코드와 RHI/DX12 코드는 건드리지 않는다 —
별개의 진행 중 작업이다.
