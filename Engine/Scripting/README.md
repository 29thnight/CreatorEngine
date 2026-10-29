# Engine scripting

F# 스크립팅 소스와 빌드 프로젝트를 이 디렉터리에서 관리한다.

| 디렉터리 | 역할 |
|---|---|
| `Bindings` | 네이티브 바인딩과 프로세스 상주 F# 런타임 |
| `AudioValues` | 바인딩과 게임 스크립트가 공유하는 오디오 값 타입 |
| `Authoring` | F# 컴포넌트 작성 보조 함수 |
| `GameScripts` | 프로젝트의 `Assets/Script/compile.order`를 읽는 게임 스크립트 빌드 |

`Bindings` 내부는 다음과 같이 나뉜다. 컴파일 순서는 `Bindings/ScriptNative.fsproj`에서 지정한다.

- `Components`: 엔티티·Transform과 컴포넌트 래퍼
- `Services`: 입력·물리·오디오·자산 등 스크립트 API
- `Interop`: 네이티브 API 표, 부트스트랩, 리플렉션·콜백 전달
- `Runtime`: 생명주기, 등록부, 접촉 전달, 호출·핫리로드·GC 관리
- `Values`: 공통 값 타입과 키 코드
- `GeneratedNative`: 네이티브 타입별로 생성한 F# 바인딩. `Tools/build/generate-fsharp-native-facades.py`로 갱신한다.

빌드 후 어셈블리 이름과 `Bin/<플랫폼>-<구성>/Managed` 배치는 기존 계약을 따른다. 저장소의 `GameScripts`는 회귀 검사용 F# 샘플 소스다.
