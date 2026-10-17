# PHASE 12 — Editor 텍스처 임포트 수명 검증

기준일: 2026-10-10 KST. 정본: [TexturePipelinePlan](../plans/TexturePipelinePlan.md).
앞선 빌드·cook·패키지 결과는 [기존 검증 기록](TexturePipelineValidation20261009.md)에 보존한다.

## 실행 경계

새 `TextureImportLifecycleProbe`는 실제 Editor 정적 라이브러리의
`EditorAssetDatabase`와 `DataSystem`을 연결한다. 실제 worker scheduler, 파일 watcher,
sidecar 저장, DirectXTex producer, immutable artifact, runtime publication과 image cache를
사용한다. 격리된 프로젝트에서 `DrainQueuedAssetChanges()`를 호출해 게시 경계를 진행한다.
ImGui에 진입하면 즉시 실패하는 검사기이며 UI 클릭·실제 렌더 frame·씬 reload는 실행하지 않는다.

입력은 기존 `Tiny4.png` 4×4 fixture다. 프로젝트 및 원본/sidecar 변경은 ignored `Build`의
새 디렉터리에 한정한다. Debug/Release 각각 짧은 경로와 긴 경로를 검사한다.

## 검사 계약

- typed 설정 저장 후 GUID, 별도 사용자 필드, 확장자와 sampler wrap 보존
- 최대 크기 2 적용 후 새 description/image 게시 및 기존 4×4 참조 보존
- 잘못된 compression enum 거부 시 sidecar 바이트와 정상 세대 불변
- 손상 PNG cook 실패 시 정상 2×2 description과 image view 치수 보존; 유효한 원본 복구 후 4×4 재게시
- 연속 요청에서 후속 recipe 적용; 이전 세대 strong owner 보존
- image cache 예산 0 및 외부 image owner 해제 후 이전 이미지가 실제로 사라졌는지 확인;
  이전 description의 정확한 immutable artifact를 재로딩해 2×2 복원
- cook 요청 직후 이름 변경; source/sidecar 쌍 이동과 GUID 보존, 새 경로의 recipe 게시,
  기존 세대 owner의 image view 치수 보존

각 실행은 36개 단정을 수행한다. 손상 PNG의 decode 오류는 의도한 음성 검사다.
watcher가 이동한 이전 경로의 작업을 다시 요청하면 missing source 진단이 발생할 수 있다.
이를 성공으로 세지 않으며 새 경로의 최종 ready/이미지/GUID를 별도로 단정한다.

## 발견한 문제와 수정

1. 긴 프로젝트 경로의 파일 기록 실패: generation 경로에 자산 GUID와 임의 GUID를 두고,
   산출물 경로에도 자산 GUID를 넣어 Windows 경로 제한을 넘었다. generation root의
   중복 자산 GUID 디렉터리를 제거했다. 임의 GUID와 산출물의 자산 GUID는 유지한다.
   기존 세대는 저장된 exact byte source 경로를 계속 사용한다. 일반적인 Windows
   무제한 경로 지원이나 longPathAware 전환을 검증한 것은 아니다.
2. Release에서 cook과 rename이 겹칠 때 sidecar 공유 위반을 실제로 재현했다.
   producer의 공통 입력 읽기에 `FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE`
   핸들을 사용한다. 최초 capture와 cook 뒤 독립적인 디스크 재읽기를 모두 수정했다.
   capture만 수정한 중간 버전은 반복 검사에서 원본 rename 공유 위반으로 실패했다.
   백그라운드 읽기가 Editor의 rename/save를 막지 않으며 파일 핸들을
   RAII로 닫는다. 512 MiB 원본/16 MiB metadata 제한과 읽기 전후 크기 검사를 유지한다. producer의 재읽기 hash와
   publication의 stamp/revision 검사를 유지하므로 변경된 입력이나 stale 결과를 게시하지 않는다.
   cook 전체에 authoring lock을 걸거나 game thread에서 worker를 기다리지 않는다.
3. 기존 정적 검사에서 앞선 컴파일 수정 전 `scene.string()` 문자열을 기대하던 항목을
   실제 `file::path(scene).string()` 호출로 최신화했다. 이는 UI 실행 검증이 아니다.

## 실행 결과와 재현

최종 반복 실행 결과는 `Build/TextureImportLifecycle/result.json`과 구성별 로그에 기록한다.
이 결과에는 실행 파일 SHA-256, 입력 소스 9개 SHA-256, 실행 경로와 검사 수를 보존한다.
최종 실행: Debug/Release × short/long × 각각 5회, 총 20회 모두 성공했다.
각 실행 36개 단정(총 720개), 검증 대상 소스 9개 `sourceDrift=0`이다.
RenderEngine/Editor 라이브러리와 실제 CreatorEditor host를 Debug/Release 모두 다시 빌드했다.
최종 producer를 반영한 실제 Inspector/씬 실행 결과로 확대하지 않는다.

| 검사기 | 최종 실행 파일 SHA-256 |
|---|---|
| Debug | `DACF3885AF995360E4B37C0621B8E8A4B197660A52CB3759721D8D7B7833D98A` |
| Release | `F51A3F64E592BC626BDC9EC1B7666A1A80BA188BD77CFEA8E412F79238F5C42F` |

texture import 정적 검사는 5/5 통과했다. 변경 파일의 whitespace 검사도 통과했다.
producer 변경 후 texcook 회귀는 Debug/Release 각각 기존 138/138 및 PHASE 12
53/53 통과했다. 이 합성 계약 검사 결과를 실제 압축 품질 측정으로 해석하지 않는다.

```powershell
Tools/regression/verify-texture-import-lifecycle.ps1 -Configuration All -Repetitions 5
python Tools/regression/verify-texture-import-editor-source.py
```

`-SkipProjectReferences`는 의존 프로젝트를 현재 소스로 별도 빌드한 경우에만 사용한다.
Editor 라이브러리 및 실제 host의 구성별 빌드 로그는
`Build/phase12-import-editor-{debug,release}-build.log`와
`Build/phase12-import-host-{debug,release}-build.log`에 보존한다.
producer 변경을 반영한 RenderEngine 구성별 빌드와 texcook 회귀 로그는
`Build/phase12-import-render-{debug,release}-build.log` 및
`Build/phase12-import-texcook.log`에 보존한다.

대시보드 전체 JavaScript 파싱·렌더와 유한 진행률 계산은 통과했다.
구조 검사기는 이전 기록과 동일하게 PHASE 4.85의 quoted-key RTP 14개에 대해
문자열 위반 140건/항목 모양 14건을 보고한다. 이번 PHASE 12 변경의 오류로
표시하거나 전체 구조 검사 통과로 기록하지 않는다.

## 남은 수용 조건

Inspector Save and Reimport의 실제 조작과 미저장 씬 확인 UI는 남아 있다.
후속 실제 Editor 명령 경로의 Reload Saved Scene과 같은 재로드 성공/실패 및
기존 씬·entity identity 보존은 [씬 재로드 검증 기록](TextureSceneReloadValidation20261010.md)에서 구분한다.
손상 runtime publication의 모든
실패 주입, 장시간·대규모·외부 프로세스 파일 변경 race도 이번 작은 fixture의 범위 밖이다.
Shipping GPU 패키지, 새 BC5/BC7 등의 전체 GPU 소비, 모델 전체 sampling과 현재 코퍼스
품질·시간·메모리 수용은 기존 잔여 조건으로 유지한다.
이 검사기는 description identity와 image view 치수를 확인하며 최종 화면 픽셀이나
임포트 전후 모든 texel의 독립 기대값을 대조하지 않는다.

T0/T1a/T2는 진행 중이며 `earnedDays: 0`. T1b는 대기, T3는 제품 타깃 부재로 중단한다.
기존 작업 트리 변경은 보존했고 commit/push는 하지 않았다.
