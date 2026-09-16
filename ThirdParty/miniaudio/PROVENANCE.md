# miniaudio — 출처와 고정 판본

- upstream: https://github.com/mackron/miniaudio
- 고정 태그: **0.11.25** (2026-03-03 발행, 정찰 시점의 최신 안정 릴리스)
- 받은 날: 2026-09-16
- 받은 경로: `https://raw.githubusercontent.com/mackron/miniaudio/0.11.25/<파일>`

| 파일 | 크기 | SHA-256 |
|---|---|---|
| `miniaudio.h` | 4,108,168 | `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89` |
| `miniaudio.c` | 56 | `ab1984bb9804ffd7b0303813595d0b345a8a86c34da1daffc353a14b34102a65` |
| `LICENSE` | 2,597 | `457f1b500e0adf6bc059edddfa78a2f62012e7c3bb43476c20e0bd23b25ba0eb` |

소스는 **수정하지 않았다**. 버전은 헤더의 `MA_VERSION_MAJOR/MINOR/REVISION`
(`miniaudio.h:3748-3750`)으로도 확인된다 — 파일 이름이 아니라 그 값이 정본이다.

## 라이선스 선택

miniaudio 는 **Public Domain(Unlicense)** 과 **MIT No Attribution** 중 하나를 고르게 한다.
CreatorEngine 은 **MIT No Attribution(MIT-0)** 을 택한다 — public domain 개념이 관할권마다
다르게 해석되는 반면 MIT-0 은 어디서나 명시적 허가로 성립하고, 배포본 라이선스 수집
(`BuildTool/EnginePublisher.cs` 의 `LICENSE|COPYING|NOTICE|README` 규칙)에 `LICENSE` 파일
그대로 실려 증빙이 남는다.

원문 두 벌이 모두 `LICENSE` 에 들어 있으므로 파일은 손대지 않고 선택만 여기 적는다.

## 왜 vcpkg 가 아닌가

vcpkg 에 포트가 있지만 벤더링한다. 이유 둘.

1. **DLL 로 배포하지 않기로 했다**(AudioBackendModernizationPlan §0-5). 공식 ABI 안정성
   보장이 없고, MSI 런타임 의존을 늘리지 않으려는 결정이다. 단일 구현 TU 하나를 직접
   컴파일하면 그 결정이 빌드 구조로 강제된다.
2. **정확한 tag/hash 를 이 저장소가 쥔다.** 오디오는 device·decoder·resampler 가 얽혀 있어
   판본이 조용히 바뀌면 재현이 깨진다. `builtin-baseline` 이 움직여도 여기는 움직이지 않는다.

## 통합 규약

- `miniaudio.h` 를 **아무 헤더에서도 include 하지 않는다.** 구현 TU 하나
  (`Engine/SceneRuntime/Audio/MiniaudioBackend.cpp`)만 연다. `wave::` 공개 헤더에 `ma_*`
  토큰이 0인 것이 계약이다.
- `miniaudio.c`(56바이트, `MINIAUDIO_IMPLEMENTATION` 정의 + include)는 **쓰지 않는다.**
  C 로 따로 컴파일하면 링크 단위가 하나 늘고, 우리 구현 TU 가 이미 그 역할을 한다.
  upstream 과의 대조를 위해 받은 그대로 둔다.

## 갱신 절차

태그를 올릴 때는 다음을 함께 한다. 하나라도 건너뛰면 "받아서 넣었다" 는 되지만
"판본을 고정했다" 는 성립하지 않는다.

1. 릴리스 노트(CHANGES.md)에서 device/decoder/API 변경을 읽는다.
2. 새 SHA-256 세 개를 이 표에 적는다.
3. `LICENSE` 가 바뀌지 않았는지 확인한다(이중 라이선스 구성이 유지되는가).
4. `Tools/regression/verify-audio-voice-contract.ps1` 를 다시 돌린다.
