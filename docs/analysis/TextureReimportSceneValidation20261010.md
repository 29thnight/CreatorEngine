# PHASE 12 — 재임포트부터 씬 재로드까지 종단 검증

기준일: 2026-10-10 KST. 정본: [TexturePipelinePlan](../plans/TexturePipelinePlan.md).
앞선 [임포트 수명](TextureImportLifecycleValidation20261010.md) 및
[씬 재로드](TextureSceneReloadValidation20261010.md) 검사를 같은 프로세스에서 연결했다.

## 발견한 제품 결함과 수정

`ImageComponent::Load`가 cooked description의 빈 `m_assetPath`, 이름, 확장자를
합쳐 빈 문자열을 `texturePaths`에 저장했다. 실제 저장 fixture에 `texturePaths: [""]`가
기록돼 씬 재로드 후 텍스처 참조가 복원되지 않는 실패를 재현했다.

기존 경로/이름이 없는 cooked description은 `TextureAssetOrigin`의 자산 GUID로
등록된 참조 경로를 찾아 저장한다. cooked 참조를 구하지 못하면 빈 경로를 기록하지 않고
명시적으로 거부한다. description과 immutable artifact를 변경하지 않는다.
기존 loose 경로와 원본 없는 임시 생성 텍스처의 처리는 보존한다.

## 실행 경계와 수용 계약

격리 표시 파일이 있는 새 프로젝트에서만 동작하는 Editor commandlet probe
`assets.texture.reimportprobe`를 추가했다. 실제 typed import API, worker cook,
runtime publication, `ImageComponent` 저장/역직렬화, `QueueSceneLoad` 및 일반 프레임
활성화 경계를 사용한다. 이 씬 admission은 Inspector의 `scene.open_async` 핸들러가
호출하는 제품 API다. owner thread에서 cook/job을 기다리지 않고 진행 상태를 폴링한다.
UI role의 description도 별도 비동기 준비가 필요하므로 임포트 Ready와 구분해 기다린다.

각 구성의 한 Editor 프로세스에서 다음을 확인했다.

1. 초기 4×4 cooked UI description/image를 실제 ImageComponent에 연결하고 씬 저장
2. 최대 크기 2 설정 저장·재cook 후 새 2×2 description 게시, 열린 컴포넌트는 기존 4×4 owner 유지
3. 저장 씬 재로드 Ready 및 실제 프레임 교체 후 역직렬화된 컴포넌트가 새 2×2 owner 참조
4. 이전 씬 퇴역 후에도 보유한 4×4 image view 유효
5. 손상 PNG로 cook 실패 시 현재 씬과 2×2 컴포넌트 owner 보존
6. 없는 씬 재로드 Failed 시 현재 씬과 컴포넌트 owner 보존
7. 원본 바이트 복구·최대 크기 0 재임포트로 4×4 재게시; 열린 컴포넌트의 2×2 참조는 유지

## 실행 결과

Debug/Release 모두 `TEXTURE_REIMPORT_SCENE_OK`와 종료 코드 0을 확인했다.
실행별 단정 수는 최종 receipt에 기록한다. 이 수에는 폴링 시 deadline/state 확인이 포함되므로
고정된 기능 사례 수로 해석하지 않는다. 검증 대상 소스 8개 `sourceDrift=0`,
fixture PNG의 실행 전후 SHA-256 일치와 runtime DLL SHA-256 불변을 확인했다.

| 구성 | 프로젝트 | CreatorEditor.runtime.dll SHA-256 |
|---|---|---|
| Debug | `Build/TRS-Debug-b56317fa` | `48AD410851F8E2EEE93F210BA46449B810D8865D649EF04C39A118E6B25C1E4C` |
| Release | `Build/TRS-Release-2d2f0598` | `61A36FEB9E3F0CC4CEA98A0A346CE5F83047A668735B78698A79D25708AC4E64` |

실제 Editor와 프로젝트 참조는 두 구성 모두 빌드했다. Release에는 기존
Utility_Framework PDB의 LNK4020 경고가 남아 있어 디버그 심볼의 완전성을 주장하지 않는다.
texture import 정적 검사는 5/5 통과했다.
기존 별도 씬 재로드 회귀도 Debug/Release에서 정상 2건/실패 3건 및 미저장
엔티티/씬 identity 보존을 통과했다(`Build/phase12-e2e-scene-regression.log`).
대시보드 전체 JavaScript 파싱·렌더·유한 진행률과 변경 파일 whitespace 검사는 통과했다.
기존 PHASE 4.85 quoted-key RTP 구조 문제(문자열 140/항목 모양 14)는 같은 수량으로
남아 있으며 전체 구조 검사 통과로 표시하지 않는다.

```powershell
Tools/regression/verify-texture-reimport-scene.ps1 -Configuration All
Tools/regression/verify-texture-scene-reload.ps1 -Configuration All
```

최종 receipt는 `Build/TextureReimportScene/result.json`, 구성별 빌드 로그는
`Build/phase12-e2e-{debug,release}-build.log`다. 각 프로젝트의 시나리오,
구조화 결과, stdout/stderr와 저장 씬을 보존한다. 손상 PNG/없는 씬의 진단은
의도한 음성 사례이며 probe가 보존 조건을 별도로 단정한다.

## 잔여 범위

Inspector Save and Reimport·확인창·취소·파일 선택의 실제 입력 조작은 실행하지 않았다.
이미지 치수와 실제 owner identity를 검사했으며 GPU 화면 픽셀/모든 texel의 독립 기대값
대조나 압축 품질 판정은 아니다. 모델 전체 소비, Shipping GPU, 새 포맷 전체 GPU,
품질·시간·메모리 및 장시간 수용은 유지한다.

정적 검색에서 SpriteRenderer도 이름/확장자 fallback을 사용하는 별도 소비자임을 확인했다.
해당 cooked texture 저작·저장·재로드 수용은 T2의 다음 소비자 검사로 남긴다.
이번 ImageComponent 통과를 모든 소비자 완료로 확대하지 않는다.

T0/T1a/T2는 진행 중, `earnedDays: 0`; T1b 대기, T3 중단. unrelated 변경은 보존하고
commit/push는 하지 않았다.
