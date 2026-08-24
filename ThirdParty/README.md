# ThirdParty

저장소가 **직접 들고 다니는** 외부 의존이다. vcpkg가 관리하는 것(`vcpkg.json`)은
여기 오지 않는다 — 그쪽은 매니페스트와 `builtin-baseline` 두 줄이 판본을 고정한다.

여기 있는 것은 vcpkg로 못 가져오거나, 가져오면 안 되는 것들이다.

| 폴더 | 무엇 | 왜 vcpkg가 아닌가 |
|---|---|---|
| `Fmod/` | FMOD Studio API 헤더·라이브러리 | 독점 SDK다. 재배포 조건이 있어 공개 레지스트리에 없다. `lib/x64/`는 `.gitignore`의 `x64/` 규칙에 걸려 추적되지 않는다 — 헤더만 저장소에 있고 라이브러리는 각자 받아 넣는다 |
| `Vulkan-Headers/` | Vulkan C API 헤더 | 아래 ★ |
| `Slang/` | Slang 셰이더 컴파일러 API 헤더·런타임 | PHASE 3.5 M1B의 DXIL/SPIR-V 단일 컴파일 경계다. 개발자 Vulkan SDK가 산출물을 바꾸지 못하도록 공식 릴리스의 최소 런타임을 저장소에 고정한다 |

## ★ Vulkan 헤더를 왜 벤더링하는가

`RenderEngine/RHI/Vulkan/`이 `<vulkan/vulkan.h>`를 연다. 이것을 SDK 설치 경로
(`$(VULKAN_SDK)\Include`)로 잡으면 **SDK가 없는 기계에서 빌드 자체가 깨진다** —
다른 개발자 PC든 CI 러너(`windows-2022`)든 마찬가지다. 실제로 한 번 겪었다.

런타임은 이미 SDK와 무관하다:

- `vulkan-1.lib`를 링크하지 않는다. `vulkan-1.dll`을 `LoadLibrary`로 열고
  진입점을 손으로 받는다(`VulkanLoader.h`). 로더가 없는 기계에서는 **실행
  파일이 뜨고 Vulkan만 꺼진다** — 링크했다면 프로세스가 아예 시작하지 못한다.
- 셰이더는 SPIR-V로 미리 뽑아 헤더에 박는다(`Shaders/VkTriangleSpv.h`).
  DX12는 `D3DCompiler_47.dll`이 Windows에 있어 HLSL을 런타임에 컴파일하지만
  Vulkan에는 그런 것이 없다.

그래서 **빌드에도 SDK가 필요 없게** 헤더만 들고 온다. SDK는 이제 두 가지에만
쓴다 — SPIR-V 재생성(`scripts/build_vk_shaders.ps1`)과 `vk.selftest`의 검증
레이어. 둘 다 개발자가 검증할 때의 일이고, 빌드하는 사람 전부가 질 부담이 아니다.

### 판본과 출처

```
Vulkan-Headers  VK_HEADER_VERSION 357  (Vulkan 1.4.357)
출처: LunarG Vulkan SDK 1.4.357.0 의 Include/ (= Khronos Vulkan-Headers)
라이선스: Apache-2.0 OR MIT
```

**C++ 바인딩을 걷어 냈다.** `vulkan.hpp`·`vulkan_raii.hpp`·`vulkan_structs.hpp`
따위가 24MB인데 `vulkan.h`는 그것들을 include하지 않고 이 저장소도 C API만 쓴다.
`vulkan/utility/`도 같은 이유로 걷었다. 남은 것은 2.5MB다 — 쓰는 것만 들고 온다.

갱신하려면 새 SDK의 `Include/vulkan`과 `Include/vk_video`를 그대로 덮어쓰고
`.hpp`·`.cppm`·`utility/`를 다시 걷어 낸 뒤, 위 판본 줄을 고친다.
