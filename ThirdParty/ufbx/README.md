# ufbx

FBX 임포터. `fastgltf` 의 짝이며, 이 둘이 `ImportedScene` IR 로 수렴한다.

```
판본  : UFBX_HEADER_VERSION 0.23.0 (ufbx/ufbx, master)
출처  : https://github.com/ufbx/ufbx
파일  : ufbx.h (223,898 B) · ufbx.c (1,181,162 B) · LICENSE
저자  : Samuli Raivio
라이선스: MIT 또는 Public Domain(Unlicense) 이중 — 고르면 된다
```

**원본 그대로다 — 한 줄도 고치지 않았다.** MIT 를 택하면 라이선스 사본 첨부가
조건이므로 `LICENSE` 를 함께 들여왔다(소스 파일 안에는 라이선스 문구가 없다).
갱신은 세 파일을 덮어쓰고 위 판본 줄만 고친다.

## 왜 vcpkg 가 아닌가

**vcpkg 에 없다.** fastgltf 를 굳이 벤더링한 이유가 이것이었다 — 짝이 되는
쪽을 패키지로 못 가져오니 임포트 계층 의존을 한곳에 모으기로 했다
([../README.md](../README.md) 표 참조).

## 왜 커브를 직접 읽지 않고 베이크를 쓰는가

`FbxImporter` 는 `ufbx_bake_anim()` 이 돌려주는 초 단위 TRS 키를 쓴다.
FBX 애니메이션은 회전 순서(Euler order)·프리/포스트 회전·상속 모드가 얽혀
있어 커브를 직접 합성하려면 그 규칙을 전부 재현해야 한다. ufbx 가 이미 접어
주므로 그것을 쓰는 편이 안전하고, 결과 형태도 IR 이 원하는 모양 그대로다.

**대신 계단(step) 보간이 촘촘한 Linear 키 쌍으로 펴진다.** 값은 보존되지만
`InterpolationMode::Step` 으로 접으려면 구간별 판정이 필요한데 현 채널 모델은
트랙 단위라 표현하지 못한다 — 임포터가 계수만 하고 넘긴다.

## 좌표계와 단위 — 실수하기 쉬운 자리

| 항목 | 설정 | 왜 |
|---|---|---|
| `target_axes` | `ufbx_axes_left_handed_y_up` | legacy 의 `aiProcess_ConvertToLeftHanded` 재현 |
| UV | 코드에서 `v = 1-v` | 같은 플래그에 `FlipUVs` 가 들어 있다 |
| `target_unit_meters` | **0 (환산 안 함)** | ★ 아래 |

★ **단위를 건드리면 안 된다.** FBX 는 보통 cm 로 저장되므로 `target_unit_meters`
를 1.0 으로 두면 ufbx 가 100 으로 나눈다. 그런데 legacy(Assimp) 는 단위 환산을
하지 않는다 — 여기서 환산하면 같은 자산이 기준선의 1/100 크기로 들어온다.

## 빌드 편입

33,000 줄짜리 단일 C 소스다. `.vcxproj` 의 per-file 설정이 필수다:

| 설정 | 왜 |
|---|---|
| `CompileAs=CompileAsC` | C 소스다 |
| `PrecompiledHeader=NotUsing` | 프로젝트가 PCH 를 강제한다 |
| `IncludeInUnityFile=false` | 유니티 빌드가 매크로·정적 심볼을 충돌시킨다 |
| `WarningLevel=TurnOffAllWarnings` | 남의 코드를 고치지 않기로 했다 |

`ufbx.h` 는 `Experiment/Import/FbxImporter.cpp` 한 TU 에서만 연다. include 경로도
전역이 아니라 그 파일에만 준다 — 아무 데서나 열리면 의존이 번진다.
