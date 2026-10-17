# PHASE 12 — SpriteRenderer 재임포트·씬 재로드 검증

기준일: 2026-10-10 KST. 정본: [TexturePipelinePlan](../plans/TexturePipelinePlan.md).

## 재현과 수정

수정 전 실제 Debug Editor 실행에서 저장 씬의 `m_SpritePath: ""`를 확인했다.
재로드 후 SpriteRenderer가 새 generation을 복원하지 못해
`Sprite component generation was not retained/restored`로 실패했다.
격리 프로젝트는 `Build/TRS-Debug-360796b3`, 실행 기록은
`Build/phase12-sprite-baseline-run.log` 및 해당 프로젝트 `results.jsonl`이다.

`SpriteRenderer::SetSprite`도 ImageComponent와 같이 기존 경로/이름이 없는 cooked
자산은 TextureAssetOrigin의 GUID로 등록 경로를 찾아 저장하도록 수정했다.
등록 경로를 찾지 못하면 기존 owner를 교체하기 전에 거부한다.
loose 경로, origin 없는 임시 생성 텍스처 및 null로 참조를 해제하는 처리를 유지한다.
immutable description/artifact를 수정하지 않는다.

## 실제 실행 수용

기존 격리 `assets.texture.reimportprobe`에 SpriteRenderer를 추가했다.
UITexture와 일반 Texture role을 각각 비동기로 준비하고 실제 두 컴포넌트를 같은 씬에 저장한다.
각 구성의 단일 Editor 프로세스에서 다음을 확인했다.

1. 초기 4px texture 연결과 씬 저장
2. 최대 크기 2 재임포트·게시 후 새 2px owner 생성, 열린 두 컴포넌트는 기존 owner 유지
3. 저장 씬 재로드와 프레임 교체 후 두 컴포넌트가 각 role의 새 2px owner 참조
4. 이전 씬 퇴역 후 보유한 이전 Sprite texture/image view가 4px로 유효
5. 손상 PNG cook 실패와 없는 씬 reload 실패 시 현재 씬/두 컴포넌트 owner 보존
6. 원본 복구 후 4px 재게시, 열린 두 컴포넌트는 2px owner 유지

Debug/Release 실제 host와 참조 프로젝트 빌드 및 실행 종료 코드 0을 확인했다.
검사 수는 deadline/state 폴링 단정을 포함하므로 고정 기능 사례 수가 아니다.

| 구성 | 격리 프로젝트 | 단정 수 | runtime DLL SHA-256 |
|---|---|---:|---|
| Debug | `TRS-Debug-edc50552` | 50 | `818EDADF973D695FF62EBF0CF32D82584876CFF6389514996AD15D49CE592776` |
| Release | `TRS-Release-0a85bc1c` | 51 | `2D94FA2B59280572FBBBE308A63DF1A01170D2097B4FCEAEE16C5AFDE9E3090D` |

소스 9개 drift 0, fixture PNG 원본 복구 및 runtime DLL 실행 전후 hash 일치를 확인했다.
유지된 검사기는 `Tools/regression/verify-texture-reimport-scene.ps1 -Configuration All`이다.
receipt: `Build/TextureReimportScene/result.json`.
빌드 로그: `Build/phase12-sprite-{debug,release}-build.log`.
실행 로그: `Build/phase12-sprite-run.log`.
기존 texture import 정적 검사 5/5 및 변경된 tracked 파일 whitespace 검사는 통과했다.
Release의 기존 Utility_Framework PDB LNK4020 경고는 남아 있다.
대시보드 전체 JavaScript 파싱·렌더·유한 진행률은 통과했다. 기존 PHASE 4.85
quoted-key 구조 문제는 문자열 140건/항목 모양 14건 그대로 남아 전체 구조 검사는
실패한다(`Build/phase12-sprite-dashboard.log`).

## 잔여 수용

이는 작은 fixture의 실제 저장/역직렬화·CPU image view·owner identity 검증이다.
Inspector 버튼/경고/취소/파일 선택 직접 입력, GPU 화면 픽셀, 전체 모델/포맷 소비,
Shipping GPU 패키지, 품질/시간/메모리 및 대량·장시간 검증은 남긴다.
자동 live component rebind와 역할별 다중 산출물 선택은 별도 확장 범위다.
다음 활성 검증은 Inspector 실제 조작이다.
T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.
