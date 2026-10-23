# F# 선택적 생명주기·VS Code 작성 도우미 — 2026-10-23

## 적용한 저작 방식

생명주기 11종은 지원 목록이며 작성 의무가 아니다. 필요한 메서드만 일반 멤버로 선언한다. 컴포넌트 생성 템플릿은 아래 두 줄로 끝나며 빈 고정틱을 넣지 않는다.

```fsharp
type MyComponent() =
    inherit Component()
```

GameScripts 57개·기본 프로젝트 2개·도구 샘플 7개, 총 F# 66파일을 점검했다. 비어 있던 FailCleanupProbe.OnTickSimulate를 제거했다. 로그·상태 변경·구독·취소·정리·초기화 성공 짝을 검사하는 회귀 훅은 동작이 있으므로 유지했다. 모든 훅을 선언하는 LifecycleAxisProbe는 순서 관측을 위한 fixture다.

공통 실행기·네이티브 핸들·핫리로드 계약과 Script API45는 변경하지 않았다. 없는 훅의 콜백은 null이며, 기존 정적 생성기가 선언된 메서드만 등록한다. 생성 검사에는 데이터 전용 컴포넌트의 콜백 11개가 모두 null인 경우와 고정틱만 등록된 경우를 추가했다.

## VS Code 사용

로컬 VS Code에 `29thnight.creatorengine-fsharp-authoring@0.1.0`을 설치했다. [확장 설명](../../../FSharp_Tool_Pakage_for_CreatorEngine/vscode/README.md)을 따른다.

1. F# 컴포넌트 안의 멤버 위치에서 `On`을 입력하고 `Ctrl+Space`를 누른다. `member this.On`으로 시작할 수도 있다.
2. 여러 메서드는 명령 팔레트나 우클릭의 **CreatorEngine F#: 생명주기 추가 (Add Lifecycle Methods)**에서 선택한다.
3. 선택한 메서드의 `()` 본문만 작성한다. 틱 인자는 single, 반환형은 unit으로 명시된다.

현재 타입과 확인 가능한 같은 파일의 기반 타입에 이미 있는 메서드는 제외한다. 주석·문자열·메서드 본문에서는 제안하지 않는다. 여러 메서드 삽입은 단일 Undo로 되돌릴 수 있으며, 취소 시 문서를 변경하지 않는다. 선택 중 문서나 커서가 바뀌면 이전 위치에 삽입하지 않는다. 파일 저장·빌드·compile.order 변경은 자동 실행하지 않는다.

일반 들여쓰기 형태의 직접 Component 상속과 같은 파일의 파생 타입이 편집 도우미의 범위다. 다른 파일의 상속·타입 별칭·전처리·명시적 class/end 등은 추측하지 않는다. 이 제약은 생성기의 FCS 상속 판정과 별개다. 전체 F# IDE·디버깅 지원을 완료한 것으로 해석하지 않는다.

## 검증

| 검사 | 결과와 범위 |
|---|---|
| FCS·생성기 | 분석63건·생성43건 통과. 훅 없는 타입과 고정틱만 있는 타입의 실제 콜백 표 포함 |
| 관리 런타임·로더 | 실행 Debug/Release 각12건, collectible 로더50건, 기존 workflow9건 통과 |
| VS Code 편집 문맥 | Node 검사26건 통과. 다른 타입·상속·주석/문자열·본문·부분 선언·CRLF·불확실한 구문 포함 |
| 실제 VS Code 1.142.0 | VSIX payload를 개발 호스트에 올려8개 동작 그룹 통과. .fs 자동 언어 인식·자동완성·선택 삽입·중복 제외·Undo/Redo·메뉴 취소·들여쓰기 포함 |
| 삽입한 F# | 실제 ScriptCore/FCS 생성기로 컴파일 통과. DataOnly=0개, Completed=고정틱1개, Empty=초기화/고정틱2개 등록 |
| 로컬 설치 | CLI 설치 성공·확장 ID/버전 확인·설치된 실행 소스2파일의 SHA256 일치 |
| Release Editor | 새 네이티브 템플릿 빌드 통과. 훅 없는 타입 생성/부착/Play/Stop, 명시적 compile.order, 이후 고정틱50회 실행, 필드37 보존·실패 reload 보존·수정 복구·exit0 통과 |
| 수명·접촉 회귀 | Debug/Release 각각 수명26건·접촉76건. 접촉 추가 할당0 bytes. 네이티브 고정 클록도 통과 |
| 기존 제품 fixture | 실제 Editor52명령·Play2회에서 실패0. cooked Player2,000프레임·표시 승격8회 이상·패키지 불변·exit0. F#53종·직렬화 필드34개의 기존 계약 일치 |

관리 도구의 검증 보고서는 [optional-lifecycle-20261023](../../../FSharp_Tool_Pakage_for_CreatorEngine/artifacts/optional-lifecycle-20261023/verification.json), 확장 패키지·개발 호스트·생성기 컴파일 보고서는 [vscode-authoring-acceptance](../../../FSharp_Tool_Pakage_for_CreatorEngine/artifacts/vscode-authoring-acceptance/verification.json)다. 패키지는 동 디렉터리의 creatorengine-fsharp-authoring-0.1.0.vsix이며 공개 게시하지 않았다.

Editor 생성/핫리로드 근거: [Editor-400f675a56ca4d41bd27800c4efcd446](../../Build/Verification/FSharp/Editor-400f675a56ca4d41bd27800c4efcd446/result.json). 설치 근거: [로컬 확장 payload](../../Build/Verification/FSharpOptionalLifecycle/vscode-install.json).

수명·정리 제품 근거: [Editor](../../Build/Verification/CSharpFixedUpdate/Editor-edbaa2bf3b344d77ac94e710797bb234/result.json), [cooked Player](../../Build/Verification/CSharpFixedUpdate/Player-93e7085c744e4af1a6dcbf784b8a28ba/result.json), [카탈로그/직렬화 비교](../../Build/Verification/FSharp/Compatibility-be90ba56797b4efeb0690a94feed4abb/result.json), [Release 계약](../../Build/Verification/CSharpFixedUpdate/Contracts-af5bcbfa36be4874a34a28db7806acdf/result.json), [Debug 계약](../../Build/Verification/CSharpFixedUpdate/Contracts-a4bcfa07d603489082a51e1447562f22/result.json). CSharpFixedUpdate는 기존 호출자가 사용하는 회귀 이름이며, 이 실행의 저작 코드는 F#이다.

## 실행 환경과 수용 경계

당시 제품 수용 gate는 PowerShell7.6.5에서 실행했다. Windows PowerShell5의 앞선 두 저작 검사는 종료 대기에서 실패하여 미수용으로 기록했고, 원인을 확정하지 않았다. 후속 조사에서는 명령 서비스의 연결 회수 순서·WSAPoll 취소와 PowerShell5의 종료 코드 누락을 확인·수정했다. 현재는 PS5/7 전체 저작·핫리로드·정상 종료를 수용했다. [후속 종료 기록](PowerShell5EditorShutdownAcceptance20261023.md)을 따르며 이 문서의 이전 실행·소스 해시는 유지한다.

이 기록은 선택적 저작·생성 템플릿·편집 도우미의 후속 검증이다. [공통 실행기 이주 당시 수용](FSharpManagedRuntimeAcceptance20261023.md)의 원본 소스 신원은 그대로 유지한다. 이번 변경으로 실제 30/60/144fps·GPU 품질·DDOL·서버·다른 플랫폼·IDE 디버깅 전체를 새 바이너리에서 재수용했다고 주장하지 않는다. 해당 실행기 소스는 변경하지 않았다. 원격 CI 실행·Marketplace 게시·커밋·푸시는 수행하지 않았다.
