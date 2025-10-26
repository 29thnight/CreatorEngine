<img width="508" height="252" alt="title_logo" src="https://github.com/user-attachments/assets/6629d62f-83b2-4e3e-866e-65dedafb3400" />

# CreatorEngine — for <a>Kori The Spritail</a> Project

<h1 align="center">
<img src="https://github.com/user-attachments/assets/052ee7f2-f02f-4c9f-9eb3-43673b9c4fe2" alt="Creator Engine" height="150">
</h1>

플랫폼 : Windows

주요 API : WinAPI, DX11

개발 언어 : C++ 20

외부 종속 라이브러리 : DX11, Imgui, Assimp, DirextXTK, nlohmann-json, PhysX, imguizmo, spdlog, pugixml, magic-enum, yaml-cpp, efsw, meshoptimizer, boost-uuid, mimalloc, LZ4

## CreatorEngine 기술 스택 및 라이브러리 개요

CreatorEngine은 Windows 기반 DX11 렌더링 파이프라인과 C++20 모듈형 런타임을 중심으로 구축된 인하우스 게임/툴 엔진입니다. 엔진은 실시간 에디터, 런타임 빌드 파이프라인, 스크립트 핫리로드 등 제작 파이프라인 전반을 아우르는 기능을 제공합니다.

<img width="1917" height="1108" alt="스크린샷 2025-10-26 213538" src="https://github.com/user-attachments/assets/8eb2e961-ec65-457b-b90b-1f17bb39c18f" />

### 플랫폼 & 언어 타깃
- **운영 체제**: Win32/Win64 환경을 대상으로 하며, 엔진 엔트리 포인트에서 WinAPI 창 관리·메시지 루프와 DX11 초기화를 수행합니다.
- **언어 표준**: 솔루션 전역이 C++20 표준(`stdcpp20`)으로 설정되어 현대적 STL과 템플릿 기능을 활용합니다.
- **빌드 도구**: Visual Studio/MSBuild를 통해 여러 모듈을 동시에 구성하며, 런타임에서는 MSBuild를 호출해 스크립트 DLL을 재빌드할 수 있습니다.

### 렌더링 & 에디터 파이프라인
#### DirectX 11 기반 그래픽스 계층
- **디바이스 관리**: `DeviceResourceManager` 싱글턴이 DX11 디바이스, 컨텍스트, 렌더타겟 및 뷰포트 상태를 중앙집중식으로 관리합니다.
- **셰이더/버퍼 유틸리티**: 상수 버퍼 생성, 샘플러 생성, 드로우 콜 카운트 추적 등의 래퍼가 제공되어 낮은 수준의 DX11 API 호출을 단순화합니다.

#### 에디터 UI & 씬 편집
- **ImGui 기반 에디터**: 엔진은 Dear ImGui 컨텍스트를 구성하고 Win32/DX11 백엔드를 초기화하여 도킹 레이아웃, 스타일링, 멀티 뷰포트 옵션을 활성화합니다.
- **기즈모 조작**: SceneView 윈도우는 ImGuizmo를 사용해 이동·회전·스케일 조작, 스냅, 기즈모 모드 전환을 제공하며, ImGui 도킹 공간에 통합됩니다.
- **로깅 오버레이**: spdlog 기반 로깅 시스템이 파일/메모리 싱크를 묶어 에디터 HUD에 로그를 노출하고, 필터링·색상화를 지원합니다.

#### 콘텐츠 파이프라인
- **모델 임포트**: Assimp를 통해 FBX 등 3D 자산을 읽어 들이고, 스켈레톤/애니메이션/콜라이더 생성 여부를 설정 값으로 제어합니다.
- **지오메트리 최적화**: meshoptimizer를 활용해 LOD 리덕션, 캐시 최적화, 오버드로우 감소 등을 수행하여 렌더 효율을 높입니다.
- **DirectXTK 유틸리티**: SpriteBatch, SaveToDDS/HDR 등 DirectXTK 구성요소를 이용해 UI, 텍스처 캡처, 포스트 프로세싱 파이프라인을 구현합니다.

### 런타임 시스템
#### 물리 시뮬레이션
- NVIDIA PhysX를 통합해 필터 셰이더, 쿼리 필터 콜백, CPU 디스패처 생성, CUDA 컨텍스트 초기화 등 고급 물리 설정을 구성합니다.

#### 오디오 엔진
- FMOD 스튜디오 런타임을 초기화하고, 채널 그룹·볼륨·스트리밍 버퍼·3D 리스너 구성 등을 포함한 사운드 매니저를 제공합니다.

#### 스크립팅 & 핫리로드
~~- **Mono/C# 연동**: 게임 오브젝트에 Mono 기반 스크립트 컴포넌트를 부착하고, 수명 주기를 C++에서 관리합니다.~~
- **핫리로드 파이프라인**: 런타임에서 MSBuild를 호출해 스크립트 솔루션(`Dynamic_CPP`)을 빌드하고, DLL을 재로딩하여 새로운 스크립트를 자동 반영합니다.

### 데이터, 메타 & 직렬화
- **JSON 직렬화**: nlohmann::json을 사용해 파티클 이펙트, 렌더 모듈, 벡터 타입 등을 직렬화/역직렬화합니다.
- **YAML 설정**: 엔진 환경설정과 프로젝트 메타데이터는 yaml-cpp 기반 싱글턴에서 관리하며, MSBuild 경로 등 개발자 옵션을 노출합니다.
- **XML 처리**: 스크립트 핫로드 시스템은 pugixml을 포함해 외부 메타 데이터를 파싱하고 빌드 파이프라인과 연계합니다.
- **자산 메타 감시**: efsw 파일 감시기를 통해 메타 파일 생성/삭제를 감지하고, 누락된 `.meta` 파일을 자동 생성합니다.
- **자산 GUID**: boost::uuids 기반 `FileGuid` 타입이 자산 경로에 대한 안정적인 식별자를 생성·역직렬화합니다.

### 메모리 & 인프라 유틸리티
- **커스텀 메모리 관리**: mimalloc 오버라이드를 포함한 DLL 래퍼를 통해 커스텀 할당/해제를 노출합니다.
- **팩 파일 시스템**: `Paklib` 헤더는 LZ4 압축 훅, AES-256-CTR 암호화, SHA-256 무결성 검사를 갖춘 패키징 런타임을 제공합니다.
- **열거형 리플렉션**: magic_enum을 이용해 런타임 열거형 이름/값 테이블을 생성하고 리플렉션 레지스트리에 등록합니다.

### 프로젝트 모듈 구성
CreatorEngine은 복수의 Visual Studio 프로젝트로 분리되어 있으며, 렌더링(`RenderEngine`), 물리(`Physics`), 스크립트 바인더(`ScriptBinder`), 공용 유틸리티(`Utility_Framework`) 등이 각각 DLL/정적 라이브러리로 빌드됩니다. 엔진 부트스트랩(`EngineEntry`)은 이러한 모듈을 초기화하고, 렌더 루프·사운드 업데이트·스크립트 재빌드 트리거 등 주요 시스템을 조율합니다.

---
이 문서는 CreatorEngine의 핵심 기술 스택과 외부 라이브러리 사용 현황을 요약하여, 신규 기여자와 협업자가 엔진 구조를 빠르게 이해할 수 있도록 돕습니다.
