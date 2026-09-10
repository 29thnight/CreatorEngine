# DotNetHost (nethost)

CreatorEngine이 CoreCLR을 올릴 때 쓰는 .NET 네이티브 호스팅 진입점이다.
`ClrHost::LoadHostfxr`가 `get_hostfxr_path`로 설치된 hostfxr를 찾고, 그 뒤로는
`hostfxr_initialize_for_runtime_config` · `hostfxr_get_runtime_delegate` ·
`hostfxr_close` 셋만 쓴다. 이 네 진입점과 세 헤더는 .NET Core 3.0 이후 바뀐 적이
없다. `nethost.dll`은 런타임 코드가 아니라 **설치된 hostfxr를 찾는 탐색기**라,
어느 10.x 사본을 써도 더 새 런타임을 정상적으로 찾는다.

## 왜 SDK 팩을 쓰지 않는가

SDK가 설치하는 `packs\Microsoft.NETCore.App.Host.win-x64\<버전>\`은 SDK가 갱신될
때마다 옛 패치 폴더가 **삭제**된다. `EngineOutput.props`가 그 버전을 고정하고
있어 VS·Windows 업데이트로 SDK가 오를 때마다 헤더 해석부터 깨졌고, 이력상
10.0.8 → 10.0.10 → 10.0.11 → 10.0.12 네 번 손으로 올렸다. 실제 의존은 API이지
SDK 판본이 아니므로 최소 파일만 저장소에 고정한다.

## 판본과 출처

```
Microsoft.NETCore.App.Host.win-x64 10.0.12
출처: .NET SDK 10.0.401 동봉 팩
      C:\Program Files\dotnet\packs\Microsoft.NETCore.App.Host.win-x64\10.0.12\runtimes\win-x64\native\
동일 산출물: https://www.nuget.org/packages/Microsoft.NETCore.App.Host.win-x64/10.0.12
라이선스: MIT (dotnet/runtime, LICENSE.TXT)
```

| 파일 | SHA-256 |
|---|---|
| `include/nethost.h` | `2049acb304b6f329f0a68ae7550a5142df604065ad08663c03ff19d03c0f456c` |
| `include/hostfxr.h` | `f365479a3b54703e288054df05943a215b8a005d145885e8b0508d1ef596afc7` |
| `include/coreclr_delegates.h` | `f1be3dd52842835b1f824e28a070b9a9626ecd73b1572a1ef772ac07088e8775` |
| `lib/nethost.lib` | `310357376f40cb56521027ce1bd9f1f9c4d57db99cae84be63f7f207d4f22633` |
| `bin/nethost.dll` | `0a6772c622650c09d64d98b5ce1cf101929163d5bb46c55d6f462a18637296ba` |

`apphost.exe` · `comhost.dll` · `ijwhost.*` · `libnethost.lib`(정적판) ·
`singlefilehost.exe`는 엔진이 쓰지 않으므로 넣지 않는다.

## 소비자

- `EngineOutput.props` — `DotNetHostIncludeDir` / `DotNetHostLibDir` / `DotNetHostBinDir`
- `Engine/SceneRuntime/SceneRuntime.vcxproj` — 헤더 include 경로
- `Editor/CreatorEditor.vcxproj` · `Player/Player.vcxproj` — `nethost.lib` 링크 경로
- `Directory.Build.targets` `DeployEngineHostRuntime` — `nethost.dll`을 실행 번들에 배치
- `Tools/dx12-validation/Invoke-DX12Validation.ps1` — preflight가 세 파일의 존재를 검사

## 런타임 요구와의 관계

이 사본은 **빌드·호스팅** 의존만 닫는다. 실행 머신에 어떤 .NET 런타임이 있어야
하는지는 `ScriptCore.runtimeconfig.json`이 정한다(현재 `Microsoft.NETCore.App`
10.0.0, 기본 roll-forward가 설치된 최신 패치를 고른다). 런타임이 아예 없는
머신에 배포하는 문제는 `docs/plans/EngineDistributionAndLauncherPlan.md`의 몫이며,
그때는 `get_hostfxr_path`에 `dotnet_root`를 넘겨 실행 폴더 안의 사설 런타임을
가리키는 자체 포함 배포가 후보다. `ClrHost::LoadHostfxr`는 지금 `nullptr`을
넘겨 전역 설치를 쓴다.

## 갱신 규약

갱신은 의무가 아니다. 메이저를 옮길 때(net11 등)도 hostfxr API가 유지되는 한
그대로 써도 된다. 굳이 올릴 때는:

1. 같은 메이저의 `Microsoft.NETCore.App.Host.win-x64` 공식 팩 또는 nuget 패키지에서
   위 다섯 파일만 교체한다.
2. 이 문서의 판본·출처·hash를 갱신한다.
3. Editor·Player를 빌드해 `DeployEngineHostRuntime`이 새 DLL을 배치하는지,
   `[CLR]` 초기화 로그가 정상인지 확인한다.
