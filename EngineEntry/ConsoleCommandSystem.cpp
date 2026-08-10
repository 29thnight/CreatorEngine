#ifndef DYNAMICCPP_EXPORTS
#include "ConsoleCommandSystem.h"
#include "GameBuilderSystem.h"

#include "SceneManager.h"
// SceneManager.h는 Scene을 전방 선언만 한다. 여기서는 씬의 멤버를 훑으므로
// 완전한 형이 필요하다 — 유니티 빌드에서는 앞선 파일이 공급했다.
#include "Scene.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "PrefabUtility.h"
#include "ComponentFactory.h"
#include "Model.h"
#include "LifecycleTrace.h"
#include "LifecycleRegistry.h"
#include "Animator.h"
#include "ConditionParameter.h"
#include "UIManager.h"
#include "Canvas.h"
#include "ImageComponent.h"
#include "RectTransformComponent.h"
#include "UIButton.h"
#include "TextComponent.h"
#include "SpriteSheetComponent.h"
#include "DataSystem.h"
#include "GpuDiagnostics.h"
#include "LogSystem.h"
#include "PathFinder.h"
#include "CoreWindow.h"
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "RenderPassData.h"
#include "RHI/ScreenSizedResource.h"

#include <Windows.h>
#include <DXProgrammableCapture.h>
#include <dxgidebug.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include "../ScriptBinder/MeshRenderer.h"
#include "../RenderEngine/Material.h"
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
    // PIX가 주입된 실행에서만 DXGIGetDebugInterface1로 얻어진다. Begin/End는
    // DX12 커맨드 스트림에 원시 이벤트 payload를 쓰지 않는 정식 캡처 경계다.
    Microsoft::WRL::ComPtr<IDXGraphicsAnalysis> g_pixGraphicsAnalysis;

    // camera.editor follow 의 상태. 게임 스레드(App 프레임 경계와 CLI Pump)
    // 에서만 만지므로 원자성이 필요 없다 — 둘 다 같은 스레드다.
    bool g_editorCameraFollowsGame = false;

    // 앞뒤 공백 제거
    std::string TrimLine(const std::string& s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    /// 공백으로 쪼개되 큰따옴표로 묶은 구간은 한 토큰으로 본다.
    ///
    /// 이름에 공백이 들어가는 경우가 실제로 있다 — 기본 씬의 카메라가
    /// "Main Camera"라서 object.transform이 이 오브젝트를 영영 못 찾았다
    /// (parts[1]이 `"Main`이 된다). 따옴표를 안 쓰면 동작이 예전과 같으므로
    /// 기존 스크립트는 그대로 돈다.
    std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> parts;
        std::string token;
        bool inQuotes = false;
        // 빈 따옴표("")도 '값을 비웠다'는 뜻이라 토큰으로 남긴다 —
        // 길이만 보면 그것을 버리게 된다.
        bool hasToken = false;

        const auto flush = [&parts, &token, &hasToken]
        {
            if (!hasToken) return;
            parts.push_back(token);
            token.clear();
            hasToken = false;
        };

        for (const char c : line)
        {
            if ('"' == c) { inQuotes = !inQuotes; hasToken = true; continue; }
            if (!inQuotes && (' ' == c || '\t' == c || '\r' == c || '\n' == c))
            {
                flush();
                continue;
            }
            token.push_back(c);
            hasToken = true;
        }
        flush();
        return parts;
    }

    // "1,2,3" 또는 "1 2 3"을 성분으로 쪼갠다. 두 형태를 다 받는 이유는
    // 벡터를 한 토큰으로 쓰는 편이 스크립트에서 읽기 쉽지만, 손으로 칠 때는
    // 공백이 더 자연스러워서다.
    std::vector<float> ParseNumbers(const std::string& raw)
    {
        std::vector<float> numbers;
        std::string buffer = raw;
        for (char& c : buffer) { if (',' == c) c = ' '; }

        std::istringstream iss(buffer);
        float value = 0.f;
        while (iss >> value) numbers.push_back(value);
        return numbers;
    }

    float NumberAt(const std::vector<float>& numbers, size_t index, float fallback)
    {
        return (index < numbers.size()) ? numbers[index] : fallback;
    }

    // 리플렉션으로 프로퍼티 하나를 설정한다.
    //
    // 인스펙터(ReflectionImGuiHelper)가 하는 일과 같은 목록을 훑는다. 컴포넌트마다
    // 전용 CLI를 만들지 않는 이유가 그것이다 — 종류가 늘 때마다 두 곳을 고치게 된다.
    //
    // 부모 타입까지 올라간다. 컴포넌트 프로퍼티는 상속 계층에 흩어져 있고
    // (m_isEnabled는 Component에, m_lightType은 LightComponent에) 인스펙터도
    // 재귀로 훑는다.
    bool ApplyReflectedProperty(void* instance, const Meta::Type* type,
        const std::string& field, const std::string& raw)
    {
        if (nullptr == type) return false;

        for (const auto& prop : type->properties)
        {
            if (nullptr == prop.name || field != prop.name) continue;
            if (!prop.setter) return false;

            const HashedGuid hash = prop.typeID;
            const auto numbers = ParseNumbers(raw);

            if (hash == GUIDCreator::GetTypeID<float>())
            {
                prop.setter(instance, NumberAt(numbers, 0, 0.f));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<int>())
            {
                prop.setter(instance, static_cast<int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<unsigned int>() || prop.typeName == "UINT")
            {
                prop.setter(instance, static_cast<unsigned int>(NumberAt(numbers, 0, 0.f)));
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<bool>() || prop.typeName == "bool32")
            {
                prop.setter(instance, raw == "true" || raw == "1");
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<std::string>())
            {
                prop.setter(instance, raw);
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<Mathf::Vector2>())
            {
                prop.setter(instance, Mathf::Vector2{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<Mathf::Vector3>())
            {
                prop.setter(instance, Mathf::Vector3{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<Mathf::Vector4>())
            {
                prop.setter(instance, Mathf::Vector4{
                    NumberAt(numbers, 0, 0.f), NumberAt(numbers, 1, 0.f),
                    NumberAt(numbers, 2, 0.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }
            if (hash == GUIDCreator::GetTypeID<Mathf::Color4>())
            {
                prop.setter(instance, Mathf::Color4{
                    NumberAt(numbers, 0, 1.f), NumberAt(numbers, 1, 1.f),
                    NumberAt(numbers, 2, 1.f), NumberAt(numbers, 3, 1.f) });
                return true;
            }

            // 열거형은 이름으로도 숫자로도 받는다. 이름 쪽이 스크립트를 읽을 때
            // 무슨 뜻인지 바로 보인다(Directional vs 0).
            if (const Meta::EnumType* enumType = Meta::MetaEnumRegistry->Find(prop.typeName))
            {
                for (const auto& entry : enumType->values)
                {
                    if (nullptr != entry.name && raw == entry.name)
                    {
                        prop.setter(instance, entry.value);
                        return true;
                    }
                }

                if (!numbers.empty())
                {
                    prop.setter(instance, static_cast<int>(numbers[0]));
                    return true;
                }
            }

            std::printf("[CLI] 지원하지 않는 프로퍼티 타입: %s (%s)\n",
                prop.name, prop.typeName.c_str());
            return false;
        }

        return ApplyReflectedProperty(instance, type->parent, field, raw);
    }

    bool ApplyReflectedProperty(Component* component, const std::string& field,
        const std::string& raw)
    {
        if (nullptr == component) return false;
        return ApplyReflectedProperty(component, Meta::Find(component->ToString()), field, raw);
    }

    // 콘솔을 확보한다(GUI 앱이라 기본적으로 없다).
    //
    // 터미널에서 실행한 경우에는 그 터미널에 그대로 붙는다. Windows Terminal에서
    // 띄우면 별도 conhost 창이 뜨지 않고 그 탭에 출력된다.
    // 부모 콘솔이 없을 때만(탐색기에서 더블클릭 등) 새 콘솔을 만드는데, 이때
    // 어떤 터미널이 열리는지는 Windows 11의 "기본 터미널 앱" 설정을 따른다.
    void EnsureConsole()
    {
        if (::GetConsoleWindow() != nullptr) return;

        if (!::AttachConsole(ATTACH_PARENT_PROCESS))
        {
            if (!::AllocConsole()) return;
        }

        // 이미 파일이나 파이프로 리다이렉트된 스트림은 건드리지 않는다.
        //
        // CONOUT$로 무조건 다시 여는 코드가 `Academy_4Q.exe --exec ... > out.txt`를
        // 조용히 무력화하고 있었다 — 명령은 돌고 출력은 콘솔 창으로만 가서
        // 파일에는 아무것도 남지 않았다. 자동 검증에서는 그 출력이 결과 전부라,
        // '통과했는지 알 수 없음'과 '실패'가 구분되지 않는 상태였다.
        const auto redirected = [](DWORD stdHandle)
        {
            const HANDLE handle = ::GetStdHandle(stdHandle);
            if (nullptr == handle || INVALID_HANDLE_VALUE == handle) return false;
            const DWORD type = ::GetFileType(handle);
            return FILE_TYPE_DISK == type || FILE_TYPE_PIPE == type;
        };

        FILE* dummy = nullptr;
        if (!redirected(STD_INPUT_HANDLE))  freopen_s(&dummy, "CONIN$", "r", stdin);
        if (!redirected(STD_OUTPUT_HANDLE)) freopen_s(&dummy, "CONOUT$", "w", stdout);
        if (!redirected(STD_ERROR_HANDLE))  freopen_s(&dummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);

        // ★ 버퍼링을 끈다.
        //
        // 씬 로드가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        // 프로세스를 죽여도 아무것도 안 남아서, 어디까지 갔는지조차 알 수
        // 없었다 — 멈춘 자리를 찾는 일에 로그가 없는 것이 가장 나쁘다.
        //
        // 버퍼링을 끄면 느려지지만, 이 경로는 진단용 CLI라 그 대가가 싸다.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);

        // 로그와 명령 출력이 한글을 쓰므로 UTF-8로 맞춘다.
        // (기본 코드페이지 949에서는 소스의 UTF-8 문자열이 깨진다)
        ::SetConsoleOutputCP(CP_UTF8);
        ::SetConsoleCP(CP_UTF8);
    }
}

ConsoleCommandSystem& ConsoleCommandSystem::Get()
{
    static ConsoleCommandSystem instance;
    return instance;
}

void ConsoleCommandSystem::InitializeFromCommandLine()
{
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    if (!argv) return;

    bool wantConsole = false;

    // 표준 입력을 읽는 스레드는 --console에서만 띄운다.
    //
    // --script / --exec는 명령이 파일과 인자에서 오므로 타이핑할 사람이 없다.
    // 그런데도 예전에는 세 경우 모두 리더를 띄웠고, 그 스레드는 종료 시점에
    // getline에 갇혀 있다가 detach됐다 — 회귀 세트(전부 --script)에서만 나타난
    // 종료 구간 힙 손상(0xC0000374)의 구조적 원인이다.
    // 콘솔 자체(출력)는 세 경우 모두 필요하므로 wantConsole과는 분리한다.
    bool wantStdinReader = false;

    auto toUtf8 = [](const wchar_t* w) -> std::string
    {
        if (!w) return {};
        const int need = ::WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
        std::string s(need > 0 ? need - 1 : 0, '\0');
        if (need > 1) ::WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), need, nullptr, nullptr);
        return s;
    };

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = toUtf8(argv[i]);

        if (arg == "--exec" && i + 1 < argc)
        {
            Enqueue(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--script" && i + 1 < argc)
        {
            LoadScriptFile(toUtf8(argv[++i]));
            wantConsole = true;
        }
        else if (arg == "--console")
        {
            wantConsole = true;
            wantStdinReader = true;
        }
        else if (arg == "--heapcheck")
        {
            EnableHeapValidation();
        }
    }
    ::LocalFree(argv);

    if (wantConsole)
    {
        // 스크립트로 돌리는 실행에서는 크래시 때 대화상자를 띄우지 않는다 —
        // 답할 사람이 없어 그대로 멈춰 있다가 덤프도 없이 죽는다.
        CoreWindow::SetUnattended(true);
        SuppressCrtDialogs();

        EnsureConsole();

        if (wantStdinReader)
        {
            std::printf("[CLI] 콘솔 명령 사용 가능. 'help' 입력.\n");
            StartStdinReader();
        }
    }

    // 스크립트를 못 열었는데 타이핑할 사람도 없다면, 이 실행은 아무것도 하지
    // 못한다. 계속 돌게 두면 하네스가 타임아웃으로 죽여야 하고 원인도 안 보인다.
    if (m_scriptLoadFailed && !wantStdinReader)
    {
        std::fputs("[CLI] 실행할 명령이 없어 종료한다.\n", stderr);
        std::fflush(stderr);
        m_quitRequested.store(true, std::memory_order_release);
    }
}

void ConsoleCommandSystem::StartStdinReader()
{
    if (m_running.exchange(true)) return;

    // 표준 입력은 블로킹이므로 별도 스레드에서 읽고 큐에만 넣는다.
    // 실제 실행은 Pump()가 게임 스레드에서 수행한다.
    m_stdinDone = std::promise<void>{};
    m_stdinDoneFuture = m_stdinDone.get_future();

    m_stdinThread = std::thread([this]
    {
        // 어떤 경로로 빠져나가든 종료 사실을 알린다. Shutdown이 이걸 기다린다.
        struct DoneSignal
        {
            std::promise<void>& promise;
            ~DoneSignal() { promise.set_value(); }
        } signal{ m_stdinDone };

        std::string line;
        while (m_running.load(std::memory_order_acquire) && std::getline(std::cin, line))
        {
            Enqueue(line);
        }
    });
}

void ConsoleCommandSystem::LoadScriptFile(const std::string& path)
{
    std::ifstream file(path);
    if (!file)
    {
        // 로그에만 남기면 아무도 못 본다. 실행 인자를 준 쪽은 대개 자동화라
        // 콘솔을 보고 있지 않고, 명령이 하나도 없는 에디터는 quit도 받지 못한 채
        // 그냥 계속 돈다 — 하네스가 타임아웃으로 죽을 때까지. 실제로 겪었다.
        std::fprintf(stderr, "[CLI] 스크립트를 열 수 없습니다: %s\n", path.c_str());
        std::fflush(stderr);
        Debug->LogError("[CLI] 스크립트를 열 수 없습니다: " + path);

        m_scriptLoadFailed = true;
        return;
    }

    std::string line;
    while (std::getline(file, line))
    {
        const std::string trimmed = TrimLine(line);
        if (trimmed.empty() || trimmed[0] == '#') continue;   // 주석/빈 줄 무시
        Enqueue(trimmed);
    }
}

void ConsoleCommandSystem::Enqueue(std::string command)
{
    std::lock_guard<std::mutex> guard(m_mutex);
    m_pending.push_back(std::move(command));
}

void ConsoleCommandSystem::Pump()
{
    // 생명주기 기록기의 프레임 경계(PHASE 9-0).
    //
    // 여기에 두는 이유는 이 함수가 이미 "게임 스레드에서 프레임마다 정확히 한 번"이고,
    // 그 성질을 가진 자리를 새로 만들면 엔진 루프에 진단용 호출이 하나 더 늘기 때문이다.
    // 아래 조기 반환들보다 앞이어야 한다 — wait 중이거나 씬 로딩 중인 프레임도
    // 프레임이고, 그 사이에 일어난 Awake/OnDestroy가 어느 프레임 것인지 알아야 한다.
    Lifecycle::Trace::BeginFrame();

    // wait 명령으로 보류 중이면 프레임만 소모한다.
    if (m_waitFrames > 0)
    {
        --m_waitFrames;
        return;
    }

    // 씬 로딩이 끝나기 전에는 다음 명령을 실행하지 않는다.
    // (전환 중 측정하면 중간값이 섞인다)
    if (SceneManagers->IsSceneLoading()) return;

    std::string command;
    {
        std::lock_guard<std::mutex> guard(m_mutex);
        if (m_pending.empty()) return;
        command = std::move(m_pending.front());
        m_pending.pop_front();
    }

    Execute(TrimLine(command));
}

void ConsoleCommandSystem::Execute(const std::string& line)
{
    if (line.empty()) return;

    const auto parts = Split(line);
    if (parts.empty()) return;

    const std::string& cmd = parts[0];

    if (cmd == "help")
    {
        PrintHelp();
    }
    else if (cmd == "quit" || cmd == "exit")
    {
        std::printf("[CLI] 종료 요청\n");
        m_quitRequested.store(true, std::memory_order_release);
    }
    else if (cmd == "game.pak")
    {
        // 게임 에셋 pak 생성 (PHASE 12 B0). 메뉴의 "Build Game"과 같은
        // 경로를 헤드리스로 연다 — B2의 build.ps1 Pak 단계가 이 명령을 부른다.
        const bool packOk = GameBuilderSystem::GetInstance()->PackageGameAssets();
        std::printf("[CLI] game.pak %s\n", packOk ? "완료" : "실패");
    }
    else if (cmd == "wait")
    {
        m_waitFrames = (parts.size() > 1) ? std::max(0, std::atoi(parts[1].c_str())) : 1;
        std::printf("[CLI] %d 프레임 대기\n", m_waitFrames);
    }
    else if (cmd == "scene.load" || cmd == "scene.switch")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: %s <씬 경로>\n", cmd.c_str());
            return;
        }

        // scene.load  : 씬을 열기만 한다(기존 씬 유지)
        // scene.switch: 씬을 열고 활성 씬으로 교체한다(기존 씬 파괴 → 언로드 유발)
        //
        // ★ 단계마다 즉시 찍는다.
        //
        //   씬 교체가 멈추는 것을 쫓다가 출력이 0바이트인 실행을 만났다.
        //   프로세스를 죽여도 아무것도 안 남아 어디까지 갔는지조차 알 수
        //   없었다. 함수가 끝나야 찍히는 로그는 멈춘 자리를 못 알려 준다 —
        //   dx12.compare 크래시 때와 같은 자리다(그때도 outLog가 함수 끝에
        //   가서야 쓰여서 세 번을 헛짚었다).
        std::printf("[CLI] %s 시작: %s\n", cmd.c_str(), parts[1].c_str());

        Scene* scene = SceneManagers->LoadScene(parts[1]);
        std::printf("[CLI] LoadScene 반환: %s\n",
            (nullptr != scene) ? "성공" : "널");

        if (!scene)
        {
            Debug->LogError("[CLI] 씬 로드 실패: " + parts[1]);
            std::printf("[CLI] 씬 로드 실패: %s\n", parts[1].c_str());
            return;
        }

        if (cmd == "scene.switch")
        {
            std::printf("[CLI] ActivateScene 진입\n");
            SceneManagers->ActivateScene(scene, true);
            std::printf("[CLI] ActivateScene 반환\n");
        }
        std::printf("[CLI] %s 완료: %s\n", cmd.c_str(), parts[1].c_str());
    }
    else if (cmd == "scene.new")
    {
        // 빈 씬에서 시작한다. 기능별 테스트 씬은 '무엇이 들어 있는지'를 전부
        // 알아야 결과를 판정할 수 있는데, 열려 있던 씬 위에 쌓으면 그게 깨진다.
        const std::string name = (parts.size() > 1)
            ? TrimLine(line.substr(cmd.size())) : std::string("FeatureTest");

        // SceneManager::CreateScene을 쓰지 않는다. 그쪽은 옛 씬을 그 자리에서
        // 해체하고 새 씬을 활성으로 바꾸는데, 그 '그 자리'가 프레임 중간이라
        // 커맨드를 만들고 있던 렌더 워커의 발밑에서 자료구조가 사라진다.
        // 실측으로 ShadowMapPass::CreateCommandListCascadeShadow →
        // DX11CommandContext::UpdateBuffer에서 죽었다.
        //
        // scene.switch가 쓰는 경로를 그대로 쓴다: 씬만 만들어 두고 교체는
        // ActivateScene에 맡긴다 — 그쪽은 BeforeAwakeSceneLoad(프레임의 안전
        // 지점)까지 미룬다.
        Scene* scene = Scene::CreateNewScene(name);
        if (!scene)
        {
            Debug->LogError("[CLI] 씬 생성 실패: " + name);
            std::printf("[CLI] 씬 생성 실패: %s\n", name.c_str());
            return;
        }

        SceneManagers->ActivateScene(scene, true);

        Debug->LogWarning("[CLI] 새 씬: " + name);
        std::printf("[CLI] 새 씬: %s\n", name.c_str());
    }
    else if (cmd == "scene.save")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.save <저장 경로>\n");
            return;
        }

        // 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
        const std::string path = TrimLine(line.substr(cmd.size()));

        if (!SceneManagers->GetActiveScene())
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return;
        }

        // 상위 디렉터리를 만들어 준다. 없으면 ofstream이 조용히 실패하고
        // '저장했다'는 메시지만 남는다.
        std::error_code directoryError{};
        file::create_directories(file::path(path).parent_path(), directoryError);

        SceneManagers->SaveScene(path);

        // 실제로 파일이 생겼는지 확인한다 — SaveScene은 실패를 돌려주지 않는다.
        if (!file::exists(path))
        {
            Debug->LogError("[CLI] 씬 저장 실패: " + path);
            std::printf("[CLI] 씬 저장 실패: %s\n", path.c_str());
            return;
        }

        const auto bytes = file::file_size(path);
        Debug->LogWarning("[CLI] 씬 저장: " + path);
        std::printf("[CLI] 씬 저장: %s (%llu 바이트)\n", path.c_str(),
            static_cast<unsigned long long>(bytes));
    }
    else if (cmd == "object.create")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: object.create <이름> [Empty|Light|Camera|Mesh]\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        GameObjectType type = GameObjectType::Empty;
        std::string name = parts[1];
        if (parts.size() > 2)
        {
            const std::string& typeName = parts[2];
            if (typeName == "Light")       type = GameObjectType::Light;
            else if (typeName == "Camera") type = GameObjectType::Camera;
            else if (typeName == "Mesh")   type = GameObjectType::Mesh;
            else if (typeName != "Empty")
            {
                std::printf("[CLI] 알 수 없는 오브젝트 타입: %s\n", typeName.c_str());
                return;
            }
        }

        auto object = scene->CreateGameObject(name, type);
        if (!object)
        {
            std::printf("[CLI] 오브젝트 생성 실패: %s\n", name.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 오브젝트 생성: " + name);
        std::printf("[CLI] 오브젝트 생성: %s\n", name.c_str());
    }
    else if (cmd == "object.rename")
    {
        // 같은 모델을 여러 번 배치하면 이름이 겹쳐 이후 명령이 첫 번째만 잡는다.
        // 하나 놓고 바로 이름을 바꾸면 그 문제가 없다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: object.rename <이전 이름> <새 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 이전 이름에 공백이 흔하다. 같은 모델을 두 번 놓으면 엔진이
        // "Prim_Cube (1)"처럼 번호를 붙이기 때문이다. 그래서 새 이름을 마지막
        // 토큰으로 보고 그 앞 전체를 이전 이름으로 본다(prefab.create와 같은 규칙).
        const std::string newName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string oldName = TrimLine(rest.substr(0, rest.rfind(newName)));

        auto object = scene->GetGameObject(oldName);
        if (!object)
        {
            Debug->LogError("[CLI] 오브젝트를 찾을 수 없음: " + oldName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", oldName.c_str());
            return;
        }

        object->m_name = newName;
        Debug->LogWarning("[CLI] 이름 변경: " + oldName + " -> " + newName);
        std::printf("[CLI] 이름 변경: %s -> %s\n", oldName.c_str(), newName.c_str());
    }
    else if (cmd == "object.transform")
    {
        // object.transform <이름> <px> <py> <pz> [rx ry rz] [sx sy sz]
        // 회전은 오일러 각(도)이다. 라디안을 쓰면 스크립트를 읽을 때 값이
        // 무슨 뜻인지 바로 안 보인다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.transform <이름> <px> <py> <pz>"
                " [rx ry rz] [sx sy sz]\n"
                "       이름에 공백이 있으면 따옴표로 묶는다: \"Main Camera\"\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetGameObject(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const auto number = [&](size_t index, float fallback) -> float
        {
            return (parts.size() > index)
                ? static_cast<float>(std::atof(parts[index].c_str())) : fallback;
        };

        const Mathf::Vector3 position{ number(2, 0.f), number(3, 0.f), number(4, 0.f) };
        const Mathf::Vector3 euler{ number(5, 0.f), number(6, 0.f), number(7, 0.f) };
        const Mathf::Vector3 scale{ number(8, 1.f), number(9, 1.f), number(10, 1.f) };

        object->m_transform.SetPosition(position);
        object->m_transform.SetRotation(Mathf::Quaternion::CreateFromYawPitchRoll(
            XMConvertToRadians(euler.y), XMConvertToRadians(euler.x),
            XMConvertToRadians(euler.z)));
        object->m_transform.SetScale(scale);
        object->m_transform.UpdateWorldMatrix();

        char message[192]{};
        std::snprintf(message, sizeof(message),
            "[CLI] 변환 설정: %s pos(%.2f %.2f %.2f) rot(%.1f %.1f %.1f) scale(%.2f %.2f %.2f)",
            parts[1].c_str(), position.x, position.y, position.z,
            euler.x, euler.y, euler.z, scale.x, scale.y, scale.z);
        Debug->LogWarning(message);
        std::printf("%s\n", message);
    }
    else if (cmd == "render.matmode")
    {
        // render.matmode <오브젝트> <opaque|transparent>
        //
        // 재질의 렌더링 모드를 바꾼다. 이것이 프록시가 deferred 큐로 가느냐
        // forward 큐로 가느냐를 정한다(RenderPassData가 이 값 하나로 나눈다).
        //
        // 전용 명령을 만든 이유: object.property는 컴포넌트의 반사 필드를
        // 설정하는데, m_renderingMode는 컴포넌트가 아니라 그 아래 Material의
        // 필드라 경로가 닿지 않는다. 그리고 이 값 없이는 Forward+ 경로를
        // 실제 씬에서 한 번도 실행해 볼 수 없다 — 씬에 투명 재질이 없으면
        // forward 큐가 늘 비고, 그러면 '되는지 안 되는지 모르는' 상태가 된다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: render.matmode <오브젝트> <opaque|transparent>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetGameObject(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const bool transparent = ("transparent" == parts[2]);
        if (!transparent && "opaque" != parts[2])
        {
            std::printf("[CLI] 모드는 opaque 또는 transparent여야 한다: %s\n",
                parts[2].c_str());
            return;
        }

        // 자식까지 훑는다. 모델 하나가 메시 여러 개로 들어오는 것이 보통이라
        // 루트만 바꾸면 큐가 그대로 비어 있고, 그건 '명령이 안 먹었다'와
        // 구분되지 않는다.
        //
        // ★ 재질은 모델 단위로 공유된다. 같은 모델을 두 번 배치한 뒤 하나만
        //   바꾸려 해도 둘 다 바뀐다 — Material 객체가 하나이기 때문이다.
        //   '불투명 하나 + 투명 하나' 배치를 만들려다 이것으로 한 번 헛돌았다.
        //   그렇게 하려면 재질 복제가 먼저 필요하고, 그건 이 명령의 몫이 아니다.
        uint32_t changed = 0;
        std::function<void(GameObject*)> apply = [&](GameObject* node)
        {
            if (nullptr == node) return;
            for (const auto& component : node->m_components)
            {
                auto* renderer = dynamic_cast<MeshRenderer*>(component.get());
                if (nullptr == renderer || nullptr == renderer->m_Material) continue;

                renderer->m_Material->m_renderingMode = transparent
                    ? MaterialRenderingMode::Transparent
                    : MaterialRenderingMode::Opaque;
                ++changed;
            }
            for (auto& child : node->m_childrenIndices)
            {
                apply(GameObject::FindIndex(child));
            }
        };
        apply(object.get());

        Debug->LogWarning("[CLI] 렌더링 모드 " + parts[2] + " — 재질 "
            + std::to_string(changed) + "개");
        std::printf("[CLI] 렌더링 모드 %s — 재질 %u개\n", parts[2].c_str(), changed);
    }
    else if (cmd == "object.property")
    {
        // object.property <오브젝트> <컴포넌트> <필드> <값...>
        //
        // 리플렉션으로 설정한다. 컴포넌트마다 전용 명령을 만들면 종류가 늘 때마다
        // CLI가 같이 늘고, 그건 인스펙터가 이미 하는 일을 두 번 하는 것이다 —
        // 인스펙터도 같은 프로퍼티 목록을 훑는다.
        if (parts.size() < 5)
        {
            std::printf("[CLI] 사용법: object.property <오브젝트> <컴포넌트> <필드> <값...>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        auto object = scene->GetGameObject(parts[1]);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        Component* target = nullptr;
        for (const auto& component : object->m_components)
        {
            if (component && component->ToString() == parts[2])
            {
                target = component.get();
                break;
            }
        }
        if (nullptr == target)
        {
            Debug->LogError("[CLI] 컴포넌트를 찾을 수 없음: " + parts[1] + "." + parts[2]);
            std::printf("[CLI] 컴포넌트를 찾을 수 없음: %s\n", parts[2].c_str());
            return;
        }

        // 값에 쉼표로 구분한 성분이 들어올 수 있다(벡터·색). 필드 이름 뒤 전체.
        std::string rest = TrimLine(line.substr(cmd.size()));
        for (size_t i = 1; i <= 3; ++i) rest = TrimLine(rest.substr(rest.find(parts[i]) + parts[i].size()));
        const std::string rawValue = rest;

        if (!ApplyReflectedProperty(target, parts[3], rawValue))
        {
            // 실패를 로그에도 남긴다. 콘솔 출력은 스크립트 실행에서 리다이렉트되지
            // 않아 보이지 않고, 그러면 '설정한 줄 알았는데 안 된' 씬이 저장된다.
            Debug->LogError("[CLI] 프로퍼티 설정 실패: " + parts[2] + "." + parts[3]
                + " = " + rawValue);
            std::printf("[CLI] 프로퍼티 설정 실패: %s.%s\n", parts[2].c_str(), parts[3].c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프로퍼티 설정: " + parts[1] + "." + parts[2] + "."
            + parts[3] + " = " + rawValue);
        std::printf("[CLI] 프로퍼티 설정: %s.%s = %s\n", parts[2].c_str(), parts[3].c_str(),
            rawValue.c_str());
    }
    else if (cmd == "model.load")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: model.load <모델 경로>\n");
            return;
        }

        // 경로에 공백이 들어갈 수 있으므로 명령어 뒤 전체를 경로로 본다.
        const std::string path = TrimLine(line.substr(cmd.size()));
        DataSystems->LoadModel(path);
        std::printf("[CLI] 모델 로드 요청: %s\n", path.c_str());
    }
    else if (cmd == "model.place")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: model.place <모델 이름>\n");
            return;
        }

        // 콘텐츠 브라우저에서 씬으로 끌어다 놓는 것과 같은 경로.
        Scene* scene = SceneManagers->GetActiveScene();
        auto found = DataSystems->Models.find(parts[1]);
        if (!scene || found == DataSystems->Models.end() || !found->second)
        {
            std::printf("[CLI] 씬 또는 모델을 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        Model::LoadModelToScene(found->second.get(), *scene);
        std::printf("[CLI] 씬에 배치: %s\n", parts[1].c_str());
    }
    else if (cmd == "script.add")
    {
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: script.add <오브젝트 이름> <스크립트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하다("Main Camera"). 타입은 마지막 토큰으로 보고,
        // 그 앞 전체를 이름으로 취급한다.
        const std::string typeName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(typeName)));

        auto object = scene->GetGameObject(objectName);
        if (!object)
        {
            Debug->LogError("[스크립트] 오브젝트를 찾을 수 없음: " + objectName);
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 한 오브젝트에 스크립트를 여럿 붙일 수 있어야 하므로 중복 허용 경로를 쓴다.
        // ScriptComponent가 Awake에서 관리 인스턴스를 만든다.
        const Meta::Type* scriptType = Meta::Find("ScriptComponent");
        if (nullptr == scriptType)
        {
            std::printf("[CLI] ScriptComponent 타입을 찾을 수 없음\n");
            return;
        }

        auto script = std::dynamic_pointer_cast<ScriptComponent>(
            object->AddComponentAllowMultiple(*scriptType));
        if (!script)
        {
            std::printf("[CLI] ScriptComponent 추가 실패\n");
            return;
        }

        script->m_scriptType = typeName;
        script->Awake();   // 씬 Awake는 이미 지나갔을 수 있으므로 여기서 직접 깨운다

        if (!script->HasInstance())
        {
            Debug->LogError("[스크립트] 부착 실패 — 타입=" + typeName);
            std::printf("[CLI] 스크립트 부착 실패 (타입=%s)\n", typeName.c_str());
            return;
        }

        const int id = script->GetInstanceId();
        Debug->LogWarning("[스크립트] " + objectName + " 에 " + typeName + " 부착 (id=" + std::to_string(id) + ")");
        std::printf("[CLI] 부착 완료: %s <- %s (id=%d)\n", objectName.c_str(), typeName.c_str(), id);
    }
    else if (cmd == "scene.select")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: scene.select <오브젝트 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = TrimLine(line.substr(cmd.size()));
        auto object = scene->GetGameObject(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 인스펙터가 보는 선택 상태를 그대로 바꾼다(에디터에서 클릭한 것과 같은 효과).
        scene->m_selectedSceneObject = object.get();
        Debug->LogWarning("[CLI] 선택: " + objectName);
        std::printf("[CLI] 선택: %s\n", objectName.c_str());
    }
    else if (cmd == "script.fields")
    {
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: script.fields <인스턴스 id>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int count = clr.GetFieldCount(id);

        Debug->LogWarning("[스크립트] 인스턴스 " + parts[1] + " 노출 필드 " + std::to_string(count) + "개");

        for (int i = 0; i < count; ++i)
        {
            const std::string name = clr.GetFieldName(id, i);
            const auto type = clr.GetFieldType(id, i);

            std::string value;
            switch (type)
            {
            case ClrHost::ScriptFieldType::Float:
                value = "float " + std::to_string(clr.GetFieldFloat(id, i));
                break;
            case ClrHost::ScriptFieldType::Int32:
                value = "int " + std::to_string(clr.GetFieldInt32(id, i));
                break;
            case ClrHost::ScriptFieldType::Bool:
                value = std::string("bool ") + (clr.GetFieldBool(id, i) ? "true" : "false");
                break;
            case ClrHost::ScriptFieldType::String:
                value = "string \"" + clr.GetFieldString(id, i) + "\"";
                break;
            case ClrHost::ScriptFieldType::Float2:
            {
                const auto v = clr.GetFieldFloat2(id, i);
                value = "float2 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Float3:
            {
                const auto v = clr.GetFieldFloat3(id, i);
                value = "float3 (" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
                break;
            }
            case ClrHost::ScriptFieldType::Object:
            {
                GameObject* target = clr.GetFieldObject(id, i);
                value = std::string("object ") + (nullptr != target ? target->m_name.ToString() : "(없음)");
                break;
            }
            default:
                value = "(미지원 타입)";
                break;
            }

            Debug->LogWarning("[스크립트]   [" + std::to_string(i) + "] " + name + " = " + value);
        }
        std::printf("[CLI] 필드 %d개 기록\n", count);
    }
    else if (cmd == "script.set")
    {
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: script.set <인스턴스 id> <필드 인덱스> <값>\n");
            return;
        }

        auto& clr = ClrHost::Get();
        const int id = std::atoi(parts[1].c_str());
        const int index = std::atoi(parts[2].c_str());

        // 값에 공백이 들어갈 수 있다(문자열·오브젝트 이름). 인덱스 뒤 전체를 값으로 본다.
        std::string rest = TrimLine(line.substr(cmd.size()));
        rest = TrimLine(rest.substr(parts[1].size()));
        const std::string rawValue = TrimLine(rest.substr(parts[2].size()));

        switch (clr.GetFieldType(id, index))
        {
        case ClrHost::ScriptFieldType::Float:
            clr.SetFieldFloat(id, index, static_cast<float>(std::atof(rawValue.c_str())));
            break;
        case ClrHost::ScriptFieldType::Int32:
            clr.SetFieldInt32(id, index, std::atoi(rawValue.c_str()));
            break;
        case ClrHost::ScriptFieldType::Bool:
            clr.SetFieldBool(id, index, rawValue == "true" || rawValue == "1");
            break;
        case ClrHost::ScriptFieldType::String:
            clr.SetFieldString(id, index, rawValue);
            break;
        case ClrHost::ScriptFieldType::Float2:
        {
            ClrHost::ScriptFloat2 v{};
            sscanf_s(rawValue.c_str(), "%f,%f", &v.x, &v.y);
            clr.SetFieldFloat2(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Float3:
        {
            ClrHost::ScriptFloat3 v{};
            sscanf_s(rawValue.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
            clr.SetFieldFloat3(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Object:
        {
            // 오브젝트 참조는 이름으로 지정한다("none"이면 비운다).
            GameObject* target = nullptr;
            if (rawValue != "none" && !rawValue.empty())
            {
                if (Scene* activeScene = SceneManagers->GetActiveScene())
                {
                    auto found = activeScene->GetGameObject(rawValue);
                    target = found ? found.get() : nullptr;
                }

                if (nullptr == target)
                {
                    std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", rawValue.c_str());
                    return;
                }
            }
            clr.SetFieldObject(id, index, target);
            break;
        }
        default:
            std::printf("[CLI] 설정할 수 없는 필드입니다\n");
            return;
        }

        // 인스펙터 편집과 같은 취급 — 바뀐 값을 컴포넌트에 담아 직렬화 대상으로 만든다.
        if (Scene* scene = SceneManagers->GetActiveScene())
        {
            for (const auto& object : scene->m_SceneObjects)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script && script->GetInstanceId() == id)
                {
                    script->CaptureFields();
                    break;
                }
            }
        }

        Debug->LogWarning("[스크립트] 필드 설정 — id=" + parts[1] + " [" + parts[2] + "] = " + parts[3]);
        std::printf("[CLI] 필드 설정 완료\n");
    }
    else if (cmd == "component.add")
    {
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: component.add <오브젝트 이름> <컴포넌트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 컴포넌트 타입을 마지막 토큰으로 본다.
        const std::string typeName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(typeName)));

        auto object = scene->GetGameObject(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        // 에디터의 Add Component 메뉴와 같은 경로.
        auto found = ComponentFactorys->m_componentTypes.find(typeName);
        if (found == ComponentFactorys->m_componentTypes.end() || nullptr == found->second)
        {
            std::printf("[CLI] 알 수 없는 컴포넌트 타입: %s\n", typeName.c_str());
            return;
        }

        auto component = object->AddComponent(*found->second);
        if (!component)
        {
            std::printf("[CLI] 컴포넌트 추가 실패\n");
            return;
        }

        if (auto initializable = std::dynamic_pointer_cast<System::IInitializable>(component))
        {
            initializable->Initialize();
        }

        Debug->LogWarning("[CLI] 컴포넌트 추가: " + objectName + " <- " + typeName);
        std::printf("[CLI] 컴포넌트 추가: %s <- %s\n", objectName.c_str(), typeName.c_str());
    }
    else if (cmd == "prefab.instantiate")
    {
        // 실제 게임 콘텐츠(프리팹)를 씬에 소환한다. UI 프리팹의 지연 연결 검증에
        // 필요해 추가했지만, 콘텐츠가 걸린 회귀라면 어디든 쓸 수 있다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: prefab.instantiate <프리팹 이름> [인스턴스 이름]\n");
            return;
        }

        Prefab* prefab = PrefabUtilitys->LoadPrefab(parts[1]);
        if (nullptr == prefab)
        {
            Debug->LogError("[CLI] 프리팹을 찾을 수 없음: " + parts[1]);
            std::printf("[CLI] 프리팹을 찾을 수 없음: %s\n", parts[1].c_str());
            return;
        }

        const std::string instanceName = (parts.size() > 2) ? parts[2] : parts[1];
        GameObject* instance = PrefabUtilitys->InstantiatePrefab(prefab, instanceName);
        if (nullptr == instance)
        {
            std::printf("[CLI] 인스턴스 생성 실패: %s\n", parts[1].c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 소환: " + parts[1] + " -> " + instanceName);
        std::printf("[CLI] 프리팹 소환: %s -> %s (index=%d)\n",
            parts[1].c_str(), instanceName.c_str(), static_cast<int>(instance->m_index));
    }
    else if (cmd == "window.resize")
    {
        // 해상도 독립 검증용(PHASE 7). 창을 실제로 리사이즈해 엔진의 리사이즈 경로를
        // 그대로 태운다 — g_ClientRect 갱신부터 UI 리플로우까지 실제 흐름을 검증해야
        // 의미가 있다.
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: window.resize <너비> <높이>\n");
            return;
        }

        const int width = std::atoi(parts[1].c_str());
        const int height = std::atoi(parts[2].c_str());
        if (width < 320 || height < 240)
        {
            std::printf("[CLI] 너무 작은 크기입니다: %dx%d\n", width, height);
            return;
        }

        // GetActiveWindow는 창이 포그라운드가 아니면 null을 준다. 스크립트 실행은
        // 대개 백그라운드라 이 경로가 실제로 걸리므로, 프로세스의 보이는 최상위 창을
        // 직접 찾아 대체한다.
        HWND hwnd = ::GetActiveWindow();
        if (nullptr == hwnd)
        {
            ::EnumWindows([](HWND candidate, LPARAM out) -> BOOL
            {
                DWORD pid = 0;
                ::GetWindowThreadProcessId(candidate, &pid);
                if (pid != ::GetCurrentProcessId()) return TRUE;
                if (!::IsWindowVisible(candidate)) return TRUE;
                if (::GetWindow(candidate, GW_OWNER) != nullptr) return TRUE;

                *reinterpret_cast<HWND*>(out) = candidate;
                return FALSE;
            }, reinterpret_cast<LPARAM>(&hwnd));
        }
        if (nullptr == hwnd) { std::printf("[CLI] 창 핸들 없음\n"); return; }

        // 클라이언트 영역이 요청 크기가 되도록 창 전체 크기를 역산한다.
        RECT desired{ 0, 0, width, height };
        const LONG style = ::GetWindowLong(hwnd, GWL_STYLE);
        const LONG exStyle = ::GetWindowLong(hwnd, GWL_EXSTYLE);
        ::AdjustWindowRectEx(&desired, style, FALSE, exStyle);

        ::SetWindowPos(hwnd, nullptr, 0, 0,
            desired.right - desired.left, desired.bottom - desired.top,
            SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        // 요청한 크기가 그대로 적용되지 않을 수 있다(모니터보다 큰 창은 잘린다).
        // 요청값만 찍으면 검증에서 엉뚱한 기준을 잡게 되므로 실제 결과를 읽어 보고한다.
        RECT actual{};
        ::GetClientRect(hwnd, &actual);
        const int actualWidth = actual.right - actual.left;
        const int actualHeight = actual.bottom - actual.top;

        const std::string message = (actualWidth == width && actualHeight == height)
            ? "[CLI] 창 크기 변경: " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight)
            : "[CLI] 창 크기 변경(클램프됨): 요청 " + std::to_string(width) + "x" + std::to_string(height) +
              " -> 실제 " + std::to_string(actualWidth) + "x" + std::to_string(actualHeight);

        Debug->LogWarning(message);
        std::printf("%s\n", message.c_str());
    }
    else if (cmd == "ui.rect")
    {
        // 오브젝트 이하 전체의 worldRect를 재귀로 찍는다. 이름 대신 *를 주면 씬의
        // 모든 RectTransform을 훑는다 — 해상도를 바꿔 가며, 또는 코드를 고치기
        // 전후로 같은 명령을 돌려 레이아웃 결과를 통째로 대조하기 위한 것이다(PHASE 7).
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: ui.rect <오브젝트 이름 | *>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 재귀 깊이 상한 — 계층이 순환하더라도 CLI가 스택을 태우지 않게 한다.
        constexpr int kMaxDepth = 32;

        std::function<void(GameObject*, int)> dump = [&](GameObject* obj, int depth)
        {
            if (nullptr == obj || depth > kMaxDepth) return;

            if (auto* rect = obj->GetComponent<RectTransformComponent>())
            {
                const auto& world = rect->GetWorldRect();
                const auto& size = rect->GetSizeDelta();
                const std::string line =
                    std::string(static_cast<size_t>(depth) * 2, ' ') + obj->m_name.ToString() +
                    " world(" + std::to_string(static_cast<int>(world.x)) + ", " +
                    std::to_string(static_cast<int>(world.y)) + ", " +
                    std::to_string(static_cast<int>(world.width)) + ", " +
                    std::to_string(static_cast<int>(world.height)) + ")" +
                    " sizeDelta(" + std::to_string(static_cast<int>(size.x)) + ", " +
                    std::to_string(static_cast<int>(size.y)) + ")" +
                    " anchor(" + std::to_string(rect->GetAnchorMin().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMin().y).substr(0, 4) + "-" +
                    std::to_string(rect->GetAnchorMax().x).substr(0, 4) + "," +
                    std::to_string(rect->GetAnchorMax().y).substr(0, 4) + ")" +
                    " scale(" + std::to_string(rect->GetLayoutScale()).substr(0, 5) + ")";

                std::printf("[CLI] %s\n", line.c_str());
                Debug->LogWarning("[ui.rect] " + line);
            }

            for (auto childIndex : obj->m_childrenIndices)
            {
                dump(GameObject::FindIndex(childIndex), depth + 1);
            }
        };

        if (parts[1] == "*")
        {
            // 최상위는 "아무의 자식도 아닌 오브젝트"로 가린다. m_parentIndex만 보고
            // 판별했더니 프리팹 루트 밑의 캔버스가 최상위로도 잡혀 같은 서브트리가
            // 두 번 찍혔다 — 대조에서 개수가 정확히 두 배가 되어 드러났다.
            std::unordered_set<GameObject::Index> childIndices;
            for (const auto& obj : scene->m_SceneObjects)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                for (auto childIndex : obj->m_childrenIndices) childIndices.insert(childIndex);
            }

            for (const auto& obj : scene->m_SceneObjects)
            {
                if (!obj || obj->IsDestroyMark()) continue;
                if (childIndices.count(obj->m_index)) continue;
                dump(obj.get(), 0);
            }
        }
        else
        {
            GameObject* target = scene->GetGameObject(parts[1]).get();
            if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }
            dump(target, 0);
        }
    }
    else if (cmd == "ui.anchor" || cmd == "ui.size")
    {
        // 스트레치 앵커처럼 저작 데이터에 없는 배치를 검증하려면 값을 직접 넣어 봐야 한다.
        // ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY> / ui.size <오브젝트> <x> <y>
        const size_t needed = (cmd == "ui.anchor") ? 6 : 4;
        if (parts.size() < needed)
        {
            std::printf("[CLI] 사용법: ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY>"
                " · ui.size <오브젝트> <x> <y>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        GameObject* target = scene->GetGameObject(parts[1]).get();
        if (!target) { std::printf("[CLI] 오브젝트 없음: %s\n", parts[1].c_str()); return; }

        auto* rect = target->GetComponent<RectTransformComponent>();
        if (!rect) { std::printf("[CLI] RectTransform 없음: %s\n", parts[1].c_str()); return; }

        if (cmd == "ui.anchor")
        {
            const Mathf::Vector2 min{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            const Mathf::Vector2 max{ std::strtof(parts[4].c_str(), nullptr), std::strtof(parts[5].c_str(), nullptr) };
            rect->SetAnchorMin(min);
            rect->SetAnchorMax(max);
            std::printf("[CLI] %s 앵커 = (%.2f,%.2f)-(%.2f,%.2f)\n",
                parts[1].c_str(), min.x, min.y, max.x, max.y);
        }
        else
        {
            const Mathf::Vector2 size{ std::strtof(parts[2].c_str(), nullptr), std::strtof(parts[3].c_str(), nullptr) };
            rect->SetSizeDelta(size);
            std::printf("[CLI] %s sizeDelta = (%.2f,%.2f)\n", parts[1].c_str(), size.x, size.y);
        }
    }
    else if (cmd == "ui.hitbox")
    {
        // 버튼의 클릭 판정 상자를 rect와 나란히 찍는다. 두 값이 같아야 보이는 곳과
        // 눌리는 곳이 일치한다 — 해상도가 바뀌어도 유지되는지가 검증 대상이다(PHASE 7-7).
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int reported = 0;
        for (const auto& owned : scene->m_SceneObjects)
        {
            GameObject* owner = owned.get();
            if (nullptr == owner || owner->IsDestroyMark()) continue;

            auto* button = owner->GetComponent<UIButton>();
            if (nullptr == button) continue;

            auto* rect = owner->GetComponent<RectTransformComponent>();
            if (nullptr == rect) continue;

            const auto& box = button->GetCollider();
            const auto& world = rect->GetWorldRect();
            const std::string line = owner->m_name.ToString() +
                " rect(" + std::to_string(static_cast<int>(world.x)) + ", " +
                std::to_string(static_cast<int>(world.y)) + ", " +
                std::to_string(static_cast<int>(world.width)) + ", " +
                std::to_string(static_cast<int>(world.height)) + ")" +
                " hitbox(" + std::to_string(static_cast<int>(box.Center.x - box.Extents.x)) + ", " +
                std::to_string(static_cast<int>(box.Center.y - box.Extents.y)) + ", " +
                std::to_string(static_cast<int>(box.Extents.x * 2.f)) + ", " +
                std::to_string(static_cast<int>(box.Extents.y * 2.f)) + ")";

            std::printf("[CLI] %s\n", line.c_str());
            Debug->LogWarning("[ui.hitbox] " + line);
            ++reported;
        }

        if (0 == reported) std::printf("[CLI] 버튼 없음\n");
    }
    else if (cmd == "pix.capture")
    {
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";
        if (mode == "begin")
        {
            if (g_pixGraphicsAnalysis)
            {
                std::printf("[CLI] pix.capture — 이미 캡처 중\n");
            }
            else
            {
                Microsoft::WRL::ComPtr<IDXGraphicsAnalysis> analysis;
                const HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&analysis));
                if (FAILED(hr))
                {
                    std::printf("[CLI] pix.capture begin 실패 0x%08X — PIX/pixtool로 실행했는지 확인\n",
                        static_cast<unsigned>(hr));
                }
                else
                {
                    analysis->BeginCapture();
                    g_pixGraphicsAnalysis = std::move(analysis);
                    std::printf("[CLI] pix.capture begin\n");
                }
            }
        }
        else if (mode == "end")
        {
            if (!g_pixGraphicsAnalysis)
            {
                std::printf("[CLI] pix.capture end — 진행 중인 캡처 없음\n");
            }
            else
            {
                // PIX는 EndCapture 전에 캡처 범위의 GPU 작업 완료를 권장한다.
                EnhancedSceneRenderer::WaitForLiveGpu();
                g_pixGraphicsAnalysis->EndCapture();
                g_pixGraphicsAnalysis.Reset();
                std::printf("[CLI] pix.capture end\n");
            }
        }
        else
        {
            std::printf("[CLI] pix.capture — %s\n",
                g_pixGraphicsAnalysis ? "캡처 중" : "대기");
        }
    }
    else if (cmd == "dx12.selftest")
    {
        // EnhancedSceneRenderer 브링업 자가 검증(PHASE 3-3). 자체 디바이스·큐·펜스로
        // 돌므로 DX11 렌더 스레드와 충돌하지 않는다 — 게임 스레드에서 즉시 실행.
        const std::string outputPath = (parts.size() > 1) ? parts[1] : std::string("dx12_selftest.png");

        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSelfTest(outputPath, 6, log);

        for (const auto& line : { log })
        {
            std::printf("%s", line.c_str());
        }
        Debug->LogWarning(std::string("[dx12.selftest] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.selftest %s → %s\n", passed ? "통과" : "실패", outputPath.c_str());
    }
    else if (cmd == "dx12.psocache")
    {
        // PSO 캐시 자가 검증(PHASE 3-4) — 매니저를 두 번 세워 캐시가 컴파일을
        // 실제로 없애는지 확인한다.
        const std::string cachePath = (parts.size() > 1) ? parts[1] : std::string("dx12_pso.cache");

        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunPsoCacheTest(cachePath, log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.psocache] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.psocache %s\n", passed ? "통과" : "실패");
    }
    else if (cmd == "dx12.uploadring")
    {
        // 업로드 링 자가 검증(PHASE 3-3). 자체 디바이스로 돌므로 DX11 렌더
        // 스레드와 충돌하지 않는다.
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunUploadRingTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.uploadring] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.uploadring %s\n", passed ? "통과" : "실패");
    }
    else if (cmd == "dx12.forward")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunForwardPlusTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forward] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forward %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.forwardshade")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunForwardPlusShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forwardshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forwardshade %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.forwardscale")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunForwardPlusScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.forwardscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.forwardscale %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ssao")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSSAOTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssao] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssao %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ssaoscale")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSSAOScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssaoscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssaoscale %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.post")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunPostChainTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.post] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.post %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.postscale")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunPostChainScaleTest(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.postscale] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.postscale %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ui")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunUITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ui] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ui %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.grid")
    {
        // 그리드 패스 검증(PHASE 3-6, Gizmo 계열 첫 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunGridTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.grid] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.grid %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.gizmoline")
    {
        // 기즈모 라인 패스 검증(PHASE 3-6, Gizmo 계열 2차 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunGizmoLineTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoline] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoline %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.gizmoicon")
    {
        // 기즈모 아이콘 패스 검증(PHASE 3-6, Gizmo 계열 3차 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunGizmoIconTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoicon] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoicon %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.wireframe")
    {
        // 와이어프레임 패스 검증(PHASE 3-6, Gizmo 계열 4차 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunWireFrameTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.wireframe] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.wireframe %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.gizmoscene")
    {
        // Gizmo 계열 씬 연결 검증(PHASE 3-6, Gizmo 계열 5차 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunGizmoSceneTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gizmoscene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gizmoscene %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.shadowquality")
    {
        // 그림자 품질 검증(PHASE 3-6 — 경사 비례 편향·캐스케이드 경계 블렌딩).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunShadowQualityTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.shadowquality] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.shadowquality %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.skybox")
    {
        // 스카이박스 패스 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSkyBoxTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.skybox] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skybox %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ibl")
    {
        // IBL 생성 체인 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunIBLTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ibl] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ibl %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.sss")
    {
        // SSS 패스 검증(PHASE 3-6, 미구현 패스 이식 1차).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSSSTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.sss] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.sss %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.decal")
    {
        // 데칼 패스 검증(PHASE 3-6, 미구현 패스 이식 2차).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunDecalTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.decal] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.decal %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ssr")
    {
        // SSR 패스 검증(PHASE 3-6, 미구현 패스 이식 3차).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSSRTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssr] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssr %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.fog")
    {
        // 볼류메트릭 포그 패스 검증(PHASE 3-6, 미구현 패스 이식 4차).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunVolumetricFogTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.fog] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.fog %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.skinning")
    {
        // GBuffer 스키닝 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSkinningTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.skinning] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.skinning %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.iblshade")
    {
        // IBL 앰비언트 소비 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunIBLShadeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.iblshade] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.iblshade %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.ssgi")
    {
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSSGITest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.ssgi] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.ssgi %s\n", verdict.c_str());
    }
    else if (cmd == "camera.editor")
    {
        // camera.editor match|follow on|follow off|status
        //
        // 씬 뷰와 게임 뷰가 서로 다른 시점이면 두 그림의 차이가 시점 탓인지
        // 렌더 탓인지 갈리지 않는다. 시점을 통일해 두면 남는 차이가 곧
        // 렌더 경로의 차이다.
        const std::string action = (parts.size() >= 2) ? parts[1] : "status";

        if (action == "match")
        {
            if (MatchEditorCameraToGameCamera())
            {
                std::printf("[CLI] camera.editor match — 게임 카메라 자세로 맞춤\n");
            }
            else
            {
                std::printf("[CLI] camera.editor match 실패: 게임 카메라가 없다\n");
            }
        }
        else if (action == "follow")
        {
            const std::string mode = (parts.size() >= 3) ? parts[2] : "on";
            g_editorCameraFollowsGame = (mode != "off" && mode != "0");
            // 켤 때 한 번 맞춰 둔다 — 다음 프레임을 기다리지 않고 바로 보인다.
            if (g_editorCameraFollowsGame) MatchEditorCameraToGameCamera();
            std::printf("[CLI] camera.editor follow %s\n",
                g_editorCameraFollowsGame ? "on" : "off");
        }
        else
        {
            const Camera* editorCamera = EnhancedSceneRenderer::GetEditorCamera();
            const auto gameCamera = CameraManagement->GetLastCamera();

            const auto describe = [](const char* label, const Camera* camera)
            {
                if (nullptr == camera) { std::printf("  %s: 없음\n", label); return; }
                Mathf::Vector3 eye{}, forward{};
                XMStoreFloat3(&eye, camera->m_eyePosition);
                XMStoreFloat3(&forward, camera->m_forward);
                std::printf("  %s: index %d · pos(%.3f %.3f %.3f)"
                    " · forward(%.3f %.3f %.3f) · fov %.1f\n",
                    label, camera->m_cameraIndex, eye.x, eye.y, eye.z,
                    forward.x, forward.y, forward.z, camera->m_fov);
            };

            std::printf("[CLI] camera.editor status (follow %s)\n",
                g_editorCameraFollowsGame ? "on" : "off");
            describe("에디터", editorCamera);
            describe("게임  ", gameCamera.get());
        }
    }
    else if (cmd == "render.backend")
    {
        const std::string backend = (parts.size() >= 2) ? parts[1] : "status";
        if (backend == "dx12" || backend == "enhanced")
        {
            EnhancedSceneRenderer::EnableLive();
            EngineSettingInstance->SetDx12BackendPreferred(true);
            std::printf("[CLI] render.backend — EnhancedRenderer/DX12 단독 운용\n");
        }
        else if (backend == "dx11")
        {
            std::printf("[CLI] render.backend dx11 — 지원하지 않음: SceneRenderer는 dead code다\n");
        }
        else
        {
            std::printf("[CLI] render.backend — 활성: enhanced-dx12 (단독)\n");
            std::printf("%s\n", EnhancedSceneRenderer::GetLiveStatus().c_str());
        }
    }
    else if (cmd == "dx12.live")
    {
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if (mode == "on")
        {
            EnhancedSceneRenderer::EnableLive();
            std::printf("[CLI] dx12.live 켜짐 — EnhancedRenderer가 메인 렌더러다\n");
        }
        else if (mode == "off")
        {
            std::printf("[CLI] dx12.live off — 지원하지 않음: 단독 메인 렌더러는 끌 수 없다\n");
        }
        else
        {
            const std::string status = EnhancedSceneRenderer::GetLiveStatus();
            std::printf("%s\n", status.c_str());
            Debug->LogWarning("[dx12.live] " + status);
        }
    }
    else if (cmd == "dx12.bench11")
    {
        // DX11 vs DX12 API 오버헤드 실측 — 마이그레이션 전제 검증.
        // 전용 디바이스 둘을 새로 세우므로 에디터 씬과 무관하게 언제든 돈다.
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunApiOverheadBench(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.bench11] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.bench11 %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.encoderbench")
    {
        // 인코더 오버헤드 실측 — R3 착수 조건(RhiBoundaryPlan §5).
        // 자체 디바이스를 세우므로 에디터 씬과 무관하게 언제든 돈다.
        // Release로 재야 의미가 있다(Debug는 검증 레이어가 vtable 비용을 덮는다).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunEncoderOverheadBench(log);
        const std::string verdict = passed ? "완료" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.encoderbench] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.encoderbench %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.scene")
    {
        // 씬 연결 검증(PHASE 3-6). 활성 씬의 카메라와 프록시를 DX12로 그린다.
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSceneBindingTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.scene] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.scene %s\n", verdict.c_str());
    }
    else if (cmd == "render.rtinfo")
    {
        // 화면 크기와 그것을 따라가는 텍스처들을 나란히 찍는다.
        //
        // ★ 예전에는 '클라이언트 · 뷰포트 · 버스' 셋을 비교했다. 셋이 어긋나는
        //   것이 '화면이 구석에 몰린다'의 정체였기 때문인데, 앞의 둘은 DX11
        //   전역이었고 D4에서 사라졌다. 이제 출처가 버스 하나라 어긋날 자리가
        //   없다 - 비교 대신 아래 명부(따라가야 하는데 안 따라간 텍스처)가
        //   같은 질문에 답한다.
        std::string report;
        {
            char line[224]{};
            std::snprintf(line, sizeof(line), "화면 %ux%u\n",
                ScreenResizeBus::Get().GetWidth(), ScreenResizeBus::Get().GetHeight());
            report += line;
        }

        // ★ 카메라별 DX11 렌더타깃·깊이 크기 리포트를 걷었다 (T6, 2026-08-08).
        //   RenderPassData가 들던 그 둘의 마지막 소비자가 이 진단이었고,
        //   실제로 그리는 쪽은 이미 0이었다(EffectSystem이 마지막이었는데
        //   PHASE 10-0에서 사라졌다). 아래 명부가 화면 추종 텍스처 전부를
        //   훑으므로 관측이 줄지도 않는다.

        // 화면 추종을 선언한 텍스처 전부. 카메라 렌더 타깃만 보면 GBuffer나
        // 포스트 체인이 어긋난 것을 놓친다 — 그것들은 중간 결과라 화면에
        // 직접 보이지 않는다.
        const uint32_t screenWidth = ScreenResizeBus::Get().GetWidth();
        const uint32_t screenHeight = ScreenResizeBus::Get().GetHeight();

        const auto entries = ScreenSizedRegistry::Get().Snapshot();
        uint32_t mismatched = 0;
        std::string mismatchReport;

        for (const auto& entry : entries)
        {
            if (!entry.querySize) continue;

            const auto [width, height] = entry.querySize();

            // 1/N 버퍼도 있으므로 '화면 크기와 다르다'만으로는 못 잡는다.
            // 화면을 정수로 나눈 값 중 하나면 정상으로 본다.
            bool plausible = false;
            // SSGI가 1/16까지 쓴다(ssratio 4의 4배). 상한을 그보다 낮게 잡으면
            // 정상인 것을 어긋난 것으로 센다.
            for (uint32_t divisor = 1; divisor <= 16; ++divisor)
            {
                if (width == (screenWidth / divisor) && height == (screenHeight / divisor))
                {
                    plausible = true;
                    break;
                }
            }

            if (!plausible)
            {
                ++mismatched;
                char line[224]{};
                std::snprintf(line, sizeof(line), "    %-34s %ux%u\n",
                    entry.name.c_str(), width, height);
                mismatchReport += line;
            }
        }

        {
            char line[160]{};
            std::snprintf(line, sizeof(line),
                "  추종 선언 텍스처 %zu개 · 화면과 어긋난 것 %u개\n",
                entries.size(), mismatched);
            report += line;
        }
        report += mismatchReport;

        Debug->LogWarning("[렌더 타깃]\n" + report);
        std::printf("[CLI] 렌더 타깃\n%s", report.c_str());
    }
    else if (cmd == "render.post")
    {
        // 기존 명령은 DX11 SceneRenderer가 소비하는 EngineSetting만 바꿨다.
        // EnhancedRenderer에는 전달되지 않아 성공처럼 출력되는 무효 명령이므로
        // 새 DX12 런타임 튜닝 API가 생기기 전까지 명시적으로 차단한다.
        std::printf("[CLI] render.post — DX11 레거시 제어는 비활성화됨; Enhanced PostChain 튜닝 API가 필요하다\n");
    }
    else if (cmd == "render.exposure")
    {
        // 기존 구현은 SceneRenderer가 갱신하는 DX11 ToneMapPass의 정적 값을
        // 읽었다. 단독 모드에서 그 값을 출력하면 정상처럼 보이는 오래된 0값을
        // 진단값으로 오인하게 되므로 새 DX12 계측이 붙기 전까지 차단한다.
        std::printf("[CLI] render.exposure — DX11 레거시 진단은 비활성화됨; PIX의 Enhanced PostChain을 확인한다\n");
    }
    else if (cmd == "dx12.resize")
    {
        // 크기 추종 검증(해상도 슬라이스).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunScreenResizeTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.resize] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.resize %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.parallel")
    {
        // 커맨드 기록 병렬화 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunParallelRecordTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.parallel] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.parallel %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.gbuffer")
    {
        // GBuffer 패스 검증(PHASE 3-6).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunGBufferTest(log);
        const std::string verdict = passed ? "통과" : "실패";

        std::printf("%s", log.c_str());
        Debug->LogWarning("[dx12.gbuffer] " + verdict + "\n" + log);
        std::printf("[CLI] dx12.gbuffer %s\n", verdict.c_str());
    }
    else if (cmd == "dx12.rendergraph")
    {
        // 렌더 그래프 자가 검증(PHASE 3-5).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunRenderGraphTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.rendergraph] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.rendergraph %s\n", passed ? "통과" : "실패");
    }
    else if (cmd == "dx12.descriptorheap")
    {
        // 디스크립터 링·샘플러 힙 자가 검증(PHASE 3-4).
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunDescriptorHeapTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.descriptorheap] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.descriptorheap %s\n", passed ? "통과" : "실패");
    }
    else if (cmd == "window.info")
    {
        // 엔진이 실제로 인식하는 클라이언트 크기. window.resize가 리사이즈 경로까지
        // 도달했는지를 UI 계산과 같은 출처(화면 크기 버스)로 확인한다.
        const uint32_t clientW = ScreenResizeBus::Get().GetWidth();
        const uint32_t clientH = ScreenResizeBus::Get().GetHeight();
        std::printf("[CLI] 클라이언트 영역: %ux%u\n", clientW, clientH);
        Debug->LogWarning("[CLI] 클라이언트 영역: " +
            std::to_string(clientW) + "x" + std::to_string(clientH));
    }
    else if (cmd == "ui.status")
    {
        // 지연 연결 상태를 숫자로 본다. 레지스트리 등록 수와 그중 캔버스가 연결된 수,
        // 그리고 씬의 캔버스 목록 — 검증에서 눈으로 대조할 기준선이다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        int imageLinked = 0;
        for (auto* image : UIManagers->Images) { if (image && image->GetOwnerCanvas()) ++imageLinked; }
        int textLinked = 0;
        for (auto* text : UIManagers->Texts) { if (text && text->GetOwnerCanvas()) ++textLinked; }
        int spriteLinked = 0;
        for (auto* sprite : UIManagers->SpriteSheets) { if (sprite && sprite->GetOwnerCanvas()) ++spriteLinked; }

        // 캔버스별 소속 UI 수까지 보여 준다 — 오연결(엉뚱한 캔버스에 붙음)은
        // 총합만 봐서는 안 보이고, 캔버스별 분포가 어긋나야 드러난다.
        std::string canvasNames;
        for (auto& weakCanvas : scene->GetCanvases())
        {
            if (auto canvas = weakCanvas.lock())
            {
                if (!canvasNames.empty()) canvasNames += ", ";
                canvasNames += canvas->m_name.ToString();

                if (Canvas* canvasComponent = canvas->GetComponent<Canvas>())
                {
                    canvasNames += "(" + std::to_string(canvasComponent->UIObjs.size()) + ")";
                }
            }
        }

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[UI 상태] Image %d/%zu 연결 · Text %d/%zu 연결 · Sprite %d/%zu 연결 · 캔버스 %zu개 [%s]",
            imageLinked, UIManagers->Images.size(),
            textLinked, UIManagers->Texts.size(),
            spriteLinked, UIManagers->SpriteSheets.size(),
            scene->GetCanvases().size(), canvasNames.c_str());

        Debug->LogWarning(line);
        std::printf("%s\n", line);
    }
    else if (cmd == "dump.crash")
    {
        // 크래시 처리 자체를 검증하기 위한 명령. 종류별로 경로가 달라서
        // (SEH / abort / terminate) 각각 실제로 덤프가 남는지 확인해야 한다.
        const std::string kind = (parts.size() > 1) ? parts[1] : std::string("access");

        Debug->LogWarning("[CLI] 의도적 크래시: " + kind);
        Log::FlushNow();

        if ("abort" == kind)
        {
            std::abort();
        }
        else if ("terminate" == kind)
        {
            std::terminate();
        }
        else if ("throw" == kind)
        {
            // 처리되지 않은 C++ 예외 → std::terminate 경로
            throw std::runtime_error("dump.crash throw");
        }
        else if ("access" == kind)
        {
            volatile int* p = nullptr;
            *p = 1;
        }
        else
        {
            std::printf("[CLI] 사용법: dump.crash <access|abort|terminate|throw>\n");
        }
    }
    else if (cmd == "dump.list" || cmd == "dump.show")
    {
        // 크래시가 나면 덤프(.dmp)와 요약(.txt)이 같은 이름으로 함께 남는다.
        // dump.list는 목록만, dump.show는 가장 최근 요약의 내용까지 찍는다.
        const file::path dumpDir = PathFinder::DumpPath();
        std::printf("[CLI] 덤프 경로: %ls\n", dumpDir.c_str());

        std::error_code ec{};
        if (!file::exists(dumpDir, ec))
        {
            std::printf("[CLI] 덤프 폴더가 없습니다\n");
            return;
        }

        // 최신순으로 보여 준다 — 방금 난 크래시가 맨 위에 있어야 쓸모가 있다.
        std::vector<file::directory_entry> dumps;
        for (const auto& entry : file::directory_iterator(dumpDir, ec))
        {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".dmp") dumps.push_back(entry);
        }

        std::sort(dumps.begin(), dumps.end(), [](const auto& a, const auto& b)
        {
            std::error_code cmpEc{};
            return file::last_write_time(a, cmpEc) > file::last_write_time(b, cmpEc);
        });

        if (dumps.empty())
        {
            Debug->LogWarning("[CLI] 덤프 없음");
            std::printf("[CLI] 덤프 없음\n");
            return;
        }

        // 콘솔은 별도 창이라 스크립트로 돌린 실행에서는 눈에 띄지 않는다.
        // 로그에도 남겨야 CI나 사후 확인에서 쓸 수 있다.
        const size_t limit = (parts.size() > 1) ? static_cast<size_t>(std::max(1, std::atoi(parts[1].c_str()))) : 10;
        for (size_t i = 0; i < dumps.size() && i < limit; ++i)
        {
            std::error_code sizeEc{};
            const auto bytes = file::file_size(dumps[i], sizeEc);

            char entry[512]{};
            std::snprintf(entry, sizeof(entry), "[CLI] 덤프 [%zu] %s (%.1f MB)",
                i, dumps[i].path().filename().string().c_str(),
                static_cast<double>(bytes) / (1024.0 * 1024.0));

            Debug->LogWarning(entry);
            std::printf("%s\n", entry);
        }

        if (cmd == "dump.show")
        {
            file::path reportPath = dumps.front().path();
            reportPath.replace_extension(".txt");

            std::ifstream report(reportPath);
            if (!report)
            {
                Debug->LogWarning("[CLI] 요약 파일 없음: " + reportPath.string());
                std::printf("[CLI] 요약 파일 없음: %ls\n", reportPath.c_str());
                return;
            }

            Debug->LogWarning("[CLI] 크래시 요약 " + reportPath.filename().string());
            std::string line;
            while (std::getline(report, line))
            {
                Debug->LogWarning("  " + line);
                std::printf("%s\n", line.c_str());
            }
        }
    }
    else if (cmd == "animator.state")
    {
        // 애니메이션 상태와 거기 붙는 스크립트는 원래 컨트롤러 편집기에서 만든다.
        // 상태 스크립트 바인딩을 검증할 방법이 없으므로 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.state <오브젝트 이름> <상태 이름> <스크립트 타입>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string behaviourName = parts.back();
        const std::string stateName = parts[parts.size() - 2];
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(stateName)));

        auto object = scene->GetGameObject(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator) { std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str()); return; }

        if (animator->m_animationControllers.empty())
        {
            animator->CreateController("CliController");
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller) { std::printf("[CLI] 컨트롤러 생성 실패\n"); return; }

        AnimationState* state = controller->CreateState(stateName, -1);
        if (nullptr == state) { std::printf("[CLI] 상태 생성 실패: %s\n", stateName.c_str()); return; }

        state->SetBehaviour(behaviourName);
        if (nullptr == state->behaviour)
        {
            Debug->LogError("[CLI] 상태 스크립트를 찾을 수 없음: " + behaviourName);
            std::printf("[CLI] 상태 스크립트를 찾을 수 없음: %s\n", behaviourName.c_str());
            return;
        }

        // 현재 상태로 만들어 두면 다음 프레임부터 Update가 돈다.
        // (전이 조건을 CLI로 짜기는 과하므로, 진입은 여기서 직접 흉내 낸다)
        controller->m_curState = state;
        state->behaviour->Enter();

        Debug->LogWarning("[CLI] 애니메이션 상태 추가: " + objectName + " · " + stateName + " <- " + behaviourName);
        std::printf("[CLI] 애니메이션 상태 추가: %s · %s <- %s\n",
            objectName.c_str(), stateName.c_str(), behaviourName.c_str());
    }
    else if (cmd == "animator.exit")
    {
        // 상태에서 빠져나가는 것까지 확인하려면 전이 조건을 짜야 하는데 CLI로는 과하다.
        // 상태 머신이 전이 때 하는 일(Exit 호출 + 현재 상태 비우기)만 흉내 낸다.
        if (parts.size() < 2)
        {
            std::printf("[CLI] 사용법: animator.exit <오브젝트 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        const std::string objectName = TrimLine(line.substr(cmd.size()));
        auto object = scene->GetGameObject(objectName);
        if (!object) { std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str()); return; }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator || animator->m_animationControllers.empty())
        {
            std::printf("[CLI] Animator 또는 컨트롤러가 없음: %s\n", objectName.c_str());
            return;
        }

        auto controller = animator->m_animationControllers.front();
        if (!controller || nullptr == controller->m_curState)
        {
            std::printf("[CLI] 현재 상태 없음\n");
            return;
        }

        if (controller->m_curState->behaviour) controller->m_curState->behaviour->Exit();
        controller->m_curState = nullptr;

        Debug->LogWarning("[CLI] 애니메이션 상태 종료: " + objectName);
        std::printf("[CLI] 애니메이션 상태 종료: %s\n", objectName.c_str());
    }
    else if (cmd == "animator.param")
    {
        // 애니메이터 파라미터는 원래 컨트롤러 편집기에서 선언한다. 스크립트에서는 만들 수 없어
        // 바인딩을 검증할 방법이 없으므로, 편집기가 하는 일을 CLI로 대신한다.
        if (parts.size() < 4)
        {
            std::printf("[CLI] 사용법: animator.param <오브젝트 이름> <파라미터 이름> <bool|float|int|trigger>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 뒤의 두 토큰을 파라미터 이름·타입으로 본다.
        const std::string typeName = parts.back();
        const std::string paramName = parts[parts.size() - 2];
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(paramName)));

        auto object = scene->GetGameObject(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        Animator* animator = object->GetComponent<Animator>();
        if (nullptr == animator)
        {
            std::printf("[CLI] Animator가 없음: %s\n", objectName.c_str());
            return;
        }

        if ("bool" == typeName)         animator->AddParameter(paramName, false, ValueType::Bool);
        else if ("float" == typeName)   animator->AddParameter(paramName, 0.f,   ValueType::Float);
        else if ("int" == typeName)     animator->AddParameter(paramName, 0,     ValueType::Int);
        else if ("trigger" == typeName) animator->AddParameter(paramName, false, ValueType::Trigger);
        else
        {
            std::printf("[CLI] 알 수 없는 파라미터 타입: %s\n", typeName.c_str());
            return;
        }

        Debug->LogWarning("[CLI] 애니메이터 파라미터 추가: " + objectName + " <- " + paramName + " (" + typeName + ")");
        std::printf("[CLI] 애니메이터 파라미터 추가: %s <- %s (%s)\n", objectName.c_str(), paramName.c_str(), typeName.c_str());
    }
    else if (cmd == "component.list")
    {
        // 붙일 수 있는 컴포넌트 타입을 훑어본다(콜라이더 이름 확인용).
        const std::string filter = (parts.size() > 1) ? parts[1] : std::string{};

        int count = 0;
        for (const auto& [typeName, type] : ComponentFactorys->m_componentTypes)
        {
            if (!filter.empty() && typeName.find(filter) == std::string::npos) continue;

            Debug->LogWarning("[CLI]   " + typeName);
            ++count;
        }
        std::printf("[CLI] 컴포넌트 타입 %d개 기록\n", count);
    }
    else if (cmd == "prefab.create")
    {
        if (parts.size() < 3)
        {
            std::printf("[CLI] 사용법: prefab.create <오브젝트 이름> <프리팹 이름>\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        // 오브젝트 이름에 공백이 흔하므로 프리팹 이름을 마지막 토큰으로 본다.
        const std::string prefabName = parts.back();
        std::string rest = TrimLine(line.substr(cmd.size()));
        const std::string objectName = TrimLine(rest.substr(0, rest.rfind(prefabName)));

        auto object = scene->GetGameObject(objectName);
        if (!object)
        {
            std::printf("[CLI] 오브젝트를 찾을 수 없음: %s\n", objectName.c_str());
            return;
        }

        Prefab* prefab = PrefabUtilitys->CreatePrefab(object.get(), prefabName);
        if (!prefab)
        {
            std::printf("[CLI] 프리팹 생성 실패\n");
            return;
        }

        const file::path path = PathFinder::Relative("Prefabs\\") / (prefabName + ".prefab");
        if (!PrefabUtilitys->SavePrefab(prefab, path.string()))
        {
            std::printf("[CLI] 프리팹 저장 실패: %s\n", path.string().c_str());
            return;
        }

        Debug->LogWarning("[CLI] 프리팹 생성: " + prefabName + " <- " + objectName);
        std::printf("[CLI] 프리팹 생성: %s\n", path.string().c_str());
    }
    else if (cmd == "script.reload")
    {
        auto& clr = ClrHost::Get();
        if (!clr.IsReady())
        {
            std::printf("[CLI] CLR이 준비되지 않았습니다\n");
            return;
        }

        Scene* scene = SceneManagers->GetActiveScene();
        std::vector<ScriptComponent*> scripts;

        // 1) 값을 챙기고 인스턴스 참조를 끊는다. 하나라도 남으면 언로드가 실패한다.
        if (scene)
        {
            for (const auto& object : scene->m_SceneObjects)
            {
                if (!object) continue;

                auto script = object->GetComponent<ScriptComponent>();
                if (nullptr != script)
                {
                    script->PrepareForReload();
                    scripts.push_back(script);
                }
            }
        }

        // 2) 어셈블리 교체
        if (!clr.ReloadScripts())
        {
            Debug->LogError("[스크립트] 리로드 실패");
            std::printf("[CLI] 리로드 실패\n");
            return;
        }

        // 3) 인스턴스를 다시 만들고 챙겨 둔 값을 되돌린다
        int restored = 0;
        for (ScriptComponent* script : scripts)
        {
            script->Awake();
            if (script->HasInstance()) ++restored;
        }

        // 언로드 완료 여부는 여기서 묻지 않는다. 리로드 호출 스택이 아직 살아 있어
        // 항상 "잔존"으로 나온다. 몇 프레임 뒤 script.status로 확인할 것.
        Debug->LogWarning("[스크립트] 리로드 완료 — 복원 " + std::to_string(restored) + "/" +
            std::to_string(scripts.size()));
        std::printf("[CLI] 리로드 완료: %d/%zu 복원 (언로드 확인은 script.status)\n",
            restored, scripts.size());
    }
    else if (cmd == "script.status")
    {
        auto& clr = ClrHost::Get();
        const bool stale = clr.IsPreviousContextAlive();

        Debug->LogWarning(std::string("[스크립트] CLR ") + (clr.IsReady() ? "준비됨" : "비활성") +
            " · 활성 스크립트 " + std::to_string(clr.LastActiveCount()) + "개" +
            " · 이전 어셈블리 " + (stale ? "잔존(참조 누수)" : "정리됨"));
        std::printf("[CLI] CLR %s, 활성 스크립트 %d개, 이전 어셈블리 %s\n",
            clr.IsReady() ? "준비됨" : "비활성", clr.LastActiveCount(),
            stale ? "잔존(참조 누수)" : "정리됨");
    }
    else if (cmd == "play" || cmd == "stop")
    {
        // 에디터의 재생/정지 버튼과 같은 경로(SceneManager::Editor가 다음 프레임에 처리).
        SceneManagers->SetGameStart(cmd == "play");
        std::printf("[CLI] %s 요청\n", cmd.c_str());
    }
    else if (cmd == "lifecycle.trace")
    {
        // 생명주기 호출 순서를 받아 적는다(PHASE 9-0).
        //
        // PHASE 9는 이 순서를 만들어 내는 기구를 통째로 바꾼다. 지금 순서는 델리게이트의
        // 우선순위 정렬과 등록 시점이 만드는 창발적 결과라 코드로는 알 수 없다 —
        // 교체 전에 받아 적어 두어야 교체 후 "동작이 같다"를 주장할 수 있다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "status";

        if (mode == "on")
        {
            // 틱 단계(Update·LateUpdate·FixedUpdate)를 적을 프레임 수.
            // 한 프레임 안의 순서가 알고 싶은 것이지 반복 횟수가 아니라서 예산을 둔다.
            const int frames = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 3;
            Lifecycle::Trace::Enable(frames);
            std::printf("[CLI] lifecycle.trace on — 틱 %d프레임\n", frames);
        }
        else if (mode == "off")
        {
            Lifecycle::Trace::Disable();
            std::printf("[CLI] lifecycle.trace off — %zu건 보관\n", Lifecycle::Trace::Count());
        }
        else if (mode == "clear")
        {
            Lifecycle::Trace::Clear();
            std::printf("[CLI] lifecycle.trace clear\n");
        }
        else
        {
            std::printf("[CLI] lifecycle.trace — %s · %zu건 · 틱 잔여 %d프레임\n",
                Lifecycle::Trace::IsEnabled() ? "기록 중" : "정지",
                Lifecycle::Trace::Count(),
                Lifecycle::Trace::RemainingTickFrames());
        }
    }
    else if (cmd == "lifecycle.registry")
    {
        // 상태 조회만 남았다(PHASE 9-3에서 델리게이트를 철거해 경로가 하나다).
        // 진단 가치는 그대로다 — 각 단계 리스트의 크기가 곧 '무엇이 매 프레임 도는가'이고,
        // 마스크 표 크기는 등록 목록이 실제로 채워졌는지를 알려 준다.
        Scene* scene = SceneManagers->GetActiveScene();
        std::printf("[CLI] lifecycle — 마스크 표 %zu종\n", Lifecycle::Registry::Count());

        if (nullptr != scene)
        {
            const auto counts = scene->GetRegistryCounts();
            std::printf("[CLI]   pendingAwake %zu · pendingStart %zu · update %zu · lateUpdate %zu · fixedUpdate %zu\n",
                counts.pendingAwake, counts.pendingStart,
                counts.update, counts.lateUpdate, counts.fixedUpdate);
        }
    }
    else if (cmd == "lifecycle.dump")
    {
        const std::string path = (parts.size() >= 2) ? parts[1] : std::string("lifecycle_trace.tsv");
        const size_t count = Lifecycle::Trace::Count();

        if (0 == count)
        {
            // 빈 파일을 성공으로 흘려보내면 "기준선을 떴다"고 착각한 채 다음으로 넘어간다.
            // 3-6에서 겪은 조용한 통과와 같은 부류라 여기서 실패로 못 박는다.
            std::printf("[CLI] lifecycle.dump 실패 — 기록 0건 (lifecycle.trace on 을 먼저 부를 것)\n");
            return;
        }

        if (Lifecycle::Trace::Dump(path))
        {
            std::printf("[CLI] lifecycle.dump %s — %zu건\n", path.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.dump 실패 — 파일을 열 수 없다: %s\n", path.c_str());
        }
    }
    else if (cmd == "lifecycle.stress")
    {
        // 파괴·생성을 몰아쳐 수명 경로를 흔든다(PHASE 9-0의 ASan 재현용).
        //
        // 지금은 프레임 경계에서 파괴가 일어나는 경로만 흔든다. "순회 도중 파괴"와
        // "Update 안에서 AddComponent" 같은 재진입 재현은 9-1의 레지스트리가 선
        // 뒤에 붙인다 — 지금 구조에는 그 지점을 안전하게 잡을 자리가 없다.
        const std::string mode = (parts.size() >= 2) ? parts[1] : "";
        const int count = (parts.size() >= 3) ? std::atoi(parts[2].c_str()) : 8;

        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene) { std::printf("[CLI] 활성 씬 없음\n"); return; }

        if (mode == "destroy")
        {
            int marked = 0;
            // 루트(0번)는 건드리지 않는다. 씬 구조가 무너지면 이후 명령이 전부 의미를 잃는다.
            for (size_t i = 1; i < scene->m_SceneObjects.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_SceneObjects[i];
                if (!owned || owned->IsDestroyMark()) continue;
                scene->DestroyGameObject(owned);
                ++marked;
            }
            std::printf("[CLI] lifecycle.stress destroy — %d개 파괴 표시\n", marked);
        }
        else if (mode == "churn")
        {
            // 파괴와 생성을 같은 프레임에 섞는다. 인덱스 재사용 경로가 여기서 드러난다.
            int marked = 0;
            for (size_t i = 1; i < scene->m_SceneObjects.size() && marked < count; ++i)
            {
                const auto& owned = scene->m_SceneObjects[i];
                if (!owned || owned->IsDestroyMark()) continue;
                scene->DestroyGameObject(owned);
                ++marked;
            }
            for (int i = 0; i < count; ++i)
            {
                scene->CreateGameObject("StressChurn_" + std::to_string(i));
            }
            std::printf("[CLI] lifecycle.stress churn — 파괴 %d · 생성 %d\n", marked, count);
        }
        else if (mode == "reentrant" || mode == "reentrant-destroy" || mode == "reentrant-add")
        {
            // 순회 한복판에서 터뜨린다(PHASE 9-9).
            //
            // 위 destroy/churn은 프레임 경계에서 일어나므로 R1·R2를 시험하지 못한다 —
            // 그 둘은 "순회하는 도중에 대상이 죽으면?"이라는 질문이고, 답하려면
            // 실제로 순회 중이어야 한다.
            const auto kind =
                (mode == "reentrant-destroy") ? Scene::StressKind::Destroy :
                (mode == "reentrant-add")     ? Scene::StressKind::AddComponent :
                                                Scene::StressKind::Both;
            scene->ArmReentrancyStress(kind, count);
            std::printf("[CLI] lifecycle.stress %s — 다음 Update 순회 한복판에서 %d건 발화\n",
                mode.c_str(), count);
        }
        else
        {
            std::printf("[CLI] lifecycle.stress destroy|churn|reentrant|reentrant-destroy|reentrant-add [개수]\n");
        }
    }
    else if (cmd == "scene.dump")
    {
        // 활성 씬의 오브젝트 계층을 로그로 남긴다. 재생/정지 전후로 찍어 비교하면
        // 무엇이 사라졌는지가 그대로 드러난다.
        Scene* scene = SceneManagers->GetActiveScene();
        if (!scene)
        {
            std::printf("[CLI] 활성 씬 없음\n");
            return;
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("dump");
        Debug->LogWarning("[씬 덤프] " + label + " · 오브젝트 " +
            std::to_string(scene->m_SceneObjects.size()) + "개");

        for (const auto& object : scene->m_SceneObjects)
        {
            if (!object) continue;
            const auto& p = object->m_transform.position;
            char position[96]{};
            std::snprintf(position, sizeof(position), "(%.3f, %.3f, %.3f)", p.x, p.y, p.z);

            Debug->LogWarning("[씬 덤프]   " + object->m_name.ToString() +
                " (index=" + std::to_string(object->m_index) +
                ", parent=" + std::to_string(object->m_parentIndex) +
                ", 컴포넌트 " + std::to_string(object->m_components.size()) + "개"
                ", pos=" + position + ")");
        }
        std::printf("[CLI] 씬 덤프 기록: %s (%zu개)\n", label.c_str(), scene->m_SceneObjects.size());
    }
    else if (cmd == "gpu.baseline")
    {
        GpuDiagnostics::ResetBaseline();
        std::printf("[CLI] 기준선 초기화 (이후 gpu.delta는 이 시점과 비교)\n");
    }
    else if (cmd == "gpu.census" || cmd == "gpu.delta")
    {
        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        // 실행 중에는 VRAM만 남는다. 타입별 집계는 디버그 레이어를 망가뜨려
        // 이후 렌더에서 죽으므로 종료 시점 리포트로만 얻을 수 있다.
        if (cmd == "gpu.delta") GpuDiagnostics::LogDelta(label);
        else                    GpuDiagnostics::LogCensus(label);

        std::printf("[CLI] GPU %s 기록: %s (VRAM 기준, 타입별 집계는 종료 리포트 참조)\n",
            (cmd == "gpu.delta") ? "증감" : "집계", label.c_str());
    }
    else if (cmd == "assets.unload")
    {
        DataSystems->UnloadUnusedAssets();
        std::printf("[CLI] 사용하지 않는 에셋 정리 요청\n");
    }
    else if (cmd == "gc.stats" || cmd == "gc.delta")
    {
        // 관리 힙 지표(PHASE 9-7). 씬 churn 벤치의 판정 기준을 네이티브에서
        // "네이티브·관리 양쪽 모두 기준선 복귀"로 넓히기 위한 것이다 —
        // gpu.delta만 보면 평탄성의 절반만 본다.
        static ClrHost::ScriptGcStats s_baseline{};
        static bool s_hasBaseline = false;

        ClrHost::ScriptGcStats gc{};
        if (!ClrHost::Get().GetManagedGcStats(gc))
        {
            std::printf("[CLI] 관리 힙 지표 없음 — 스크립트 계층 비활성\n");
            return;
        }

        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        constexpr double kBytesPerMB = 1024.0 * 1024.0;

        char line[512]{};
        if (cmd == "gc.delta" && s_hasBaseline)
        {
            std::snprintf(line, sizeof(line),
                "[gc.delta] %s — gen0 %+d · gen1 %+d · gen2 %+d · 힙 %+.1f MB (현재 %.1f MB)",
                label.c_str(),
                gc.gen0Collections - s_baseline.gen0Collections,
                gc.gen1Collections - s_baseline.gen1Collections,
                gc.gen2Collections - s_baseline.gen2Collections,
                (gc.heapSizeBytes - s_baseline.heapSizeBytes) / kBytesPerMB,
                gc.heapSizeBytes / kBytesPerMB);
        }
        else
        {
            // 기준선이 없으면 delta 요청이어도 집계로 남기고 기준선을 세운다.
            // 조용히 0을 찍으면 "변화 없음"으로 오독된다.
            s_baseline = gc;
            s_hasBaseline = true;
            std::snprintf(line, sizeof(line),
                "[gc.stats] %s — gen0 %d · gen1 %d · gen2 %d · 힙 %.1f MB · 단편화 %.1f MB · GC 점유 %.2f%%",
                label.c_str(),
                gc.gen0Collections, gc.gen1Collections, gc.gen2Collections,
                gc.heapSizeBytes / kBytesPerMB,
                gc.fragmentedBytes / kBytesPerMB,
                gc.pauseTimePercentageX100 / 100.0);
        }

        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);
    }
    else if (cmd == "bt.status" || cmd == "bt.reset")
    {
        // 행동 트리 진단(PHASE 9-8 완료 기준 1·3).
        //
        // 왜 필요한가: 트리 생성·틱은 실패할 때만 로그를 남긴다. 그래서 "트리가 안
        // 서서 AI가 가만히 있다"와 "정상 동작"이 밖에서 구분되지 않는다 — 둘 다
        // 무음이다. 회귀 세트도 BT를 쓰는 씬을 열지 않으므로, 전부 통과해도 BT
        // 코드는 한 줄도 실행되지 않은 채 통과할 수 있다. 즉 BT에 대한 양성 증거가
        // 없었고, 이 명령이 그 자리를 메운다.
        if (cmd == "bt.reset")
        {
            ClrHost::Get().ResetBehaviorTreeStats();
            ClrHost::Get().ResetAICrossings();
            std::printf("[CLI] BT 누계 초기화 (트리는 그대로)\n");
            return;
        }

        ClrHost::ScriptBTStats bt{};
        if (!ClrHost::Get().GetBehaviorTreeStats(bt))
        {
            // 조용히 0을 찍지 않는다 — "트리 0개"와 "지표를 못 읽었다"는 전혀 다른
            // 상황인데 같은 숫자로 보이면 진단이 거꾸로 간다.
            std::printf("[CLI] BT 지표 없음 — 스크립트 계층 비활성이거나 구 어셈블리\n");
            return;
        }

        const ClrHost::AICrossingCounters& x = ClrHost::Get().AICrossings();

        char line[512]{};
        std::snprintf(line, sizeof(line),
            "[bt.status] 트리 %d개 · 노드 타입 %d종 · 틱 %llu회 · 건너뜀 %llu회",
            bt.treeCount, bt.nodeTypeCount,
            static_cast<unsigned long long>(bt.tickCount),
            static_cast<unsigned long long>(bt.skippedCount));
        std::printf("[CLI] %s\n", line);
        Debug->LogWarning(line);

        // 크로싱 비율이 이 재설계의 핵심 주장이다 — 트리가 몇 개든 프레임당 1회.
        // 분모는 FlushAITicks 호출 수(= 흘려보낸 프레임 수)이고, 큐가 빈 프레임도
        // 포함한다. 그래서 비율은 1을 넘을 수 없고, 넘으면 배선이 깨진 것이다.
        char cross[512]{};
        const double perFrame = (0 == x.flushCalls)
            ? 0.0 : static_cast<double>(x.crossings) / static_cast<double>(x.flushCalls);
        const double perCrossing = (0 == x.crossings)
            ? 0.0 : static_cast<double>(x.ticksDelivered) / static_cast<double>(x.crossings);
        std::snprintf(cross, sizeof(cross),
            "[bt.status] 경계 통과 %llu회 / 프레임 %llu (프레임당 %.2f) · 전달 틱 %llu건 "
            "(크로싱당 %.1f · 최대 배치 %llu)",
            static_cast<unsigned long long>(x.crossings),
            static_cast<unsigned long long>(x.flushCalls), perFrame,
            static_cast<unsigned long long>(x.ticksDelivered), perCrossing,
            static_cast<unsigned long long>(x.maxBatch));
        std::printf("[CLI] %s\n", cross);
        Debug->LogWarning(cross);
    }
    else if (cmd == "gc.collect")
    {
        // 씬 전환이 자동으로 부르는 것과 같은 경로를 손으로 부른다.
        // 벤치에서 "전환 없이도 회수되는가"를 가르는 데 쓴다.
        ClrHost::Get().CollectManagedHeap();
        std::printf("[CLI] 관리 힙 확정 수집 요청\n");
    }
    else if (cmd == "log.flush")
    {
        Log::FlushNow();
        std::printf("[CLI] 로그 flush\n");
    }
    else if (cmd == "render.syncstats")
    {
        // 게임/렌더 락스텝의 비용을 숫자로 본다 (PHASE 3-2).
        //
        // 락스텝 해체는 크고 위험한 변경이라 "얼마나 손해인가"를 모르고 시작하면
        // 이득 없는 위험만 떠안는다. 여기서 나온 값이 해체의 근거이자 해체 후
        // 비교할 기준선이다. 인자로 reset을 주면 그 시점부터 다시 잰다.
        if (parts.size() > 1 && parts[1] == "reset")
        {
            EngineSettingInstance->renderBarrier.ResetStats();
            std::printf("[CLI] 동기화 통계 초기화\n");
            Debug->LogWarning("[syncstats] 초기화");
            return;
        }

        const BarrierStats stats = EngineSettingInstance->renderBarrier.GetStats();

        // 스레드 3개가 랑데뷰마다 도달하므로 슬롯별 도달 수를 3으로 나누면 프레임 수다.
        constexpr double kThreadCount = 3.0;

        // 랑데뷰 1은 "가장 느린 스레드"를, 2는 "게임 스레드의 배타 구간"을 기다린다.
        // 어느 쪽이 비싼지가 곧 다음 작업의 방향이다.
        static const char* kSlotNames[kBarrierSlotCount] = {
            "랑데뷰1(가장 느린 스레드 대기)",
            "랑데뷰2(게임 배타 구간 대기)" };

        std::string report;
        char line[512]{};
        double totalPerFrame = 0.0;

        for (int slot = 0; slot < kBarrierSlotCount; ++slot)
        {
            const auto& s = stats.slots[slot];
            const double frames = s.arrivals / kThreadCount;
            const double perFrameMs = (frames > 0.0) ? s.waitMilliseconds / frames : 0.0;
            totalPerFrame += perFrameMs;

            std::snprintf(line, sizeof(line),
                "\n  %s: 프레임 %.0f · 대기 %llu/%llu · 총 %.1f ms · 프레임당 %.3f ms",
                kSlotNames[slot], frames,
                static_cast<unsigned long long>(s.spins),
                static_cast<unsigned long long>(s.arrivals),
                s.waitMilliseconds, perFrameMs);
            report += line;

            // 역할별로 갈라 본다. '마지막 도착'이 곧 그 랑데뷰의 긴 쪽이다.
            static const char* kRoleNames[kBarrierRoleCount] = { "게임", "커맨드빌드", "커맨드실행" };
            for (int role = 0; role < kBarrierRoleCount; ++role)
            {
                const double roleFrames = (frames > 0.0) ? frames : 1.0;
                std::snprintf(line, sizeof(line),
                    "\n      %-10s 대기 %.3f ms/프레임 · 마지막 도착 %llu회 (%.1f%%)",
                    kRoleNames[role],
                    s.roleWaitMilliseconds[role] / roleFrames,
                    static_cast<unsigned long long>(s.roleLastArrivals[role]),
                    (frames > 0.0) ? (100.0 * s.roleLastArrivals[role] / frames) : 0.0);
                report += line;
            }
        }

        std::snprintf(line, sizeof(line), "\n  합계 프레임당 %.3f ms", totalPerFrame);
        report += line;

        std::printf("[syncstats]%s\n", report.c_str());
        std::fflush(stdout);
        Debug->LogWarning("[syncstats]" + report);
    }
    else if (cmd == "render.shadowinfo")
    {
        // 그림자 캐스케이드 계산 결과를 그대로 찍는다(PHASE 3-2 검증용).
        //
        // 이 값들은 스크린샷 대조로는 검증할 수 없다. 에디터 창에는 FPS·프로파일러·
        // 로그 패널이 같이 잡혀서 같은 빌드로 두 번 찍어도 8.8%가 어긋난다 —
        // 노이즈가 신호보다 크다. 계산값을 직접 대조하는 편이 정확하고 싸다.
        //
        // 엔진이 자체 콘솔을 열어 stdout을 CONOUT$로 돌리므로 리디렉션으로는
        // 잡히지 않는다. 다른 검증 명령과 같이 로그에도 남겨 밖에서 읽게 한다.
        char line[512]{};
        std::string report;

        for (auto& camera : CameraManagement->GetCameras())
        {
            if (nullptr == camera) continue;
            if (!RenderPassData::VaildCheck(camera.get())) continue;

            auto* data = RenderPassData::GetData(camera.get());

            std::snprintf(line, sizeof(line), "camera %d\n", camera->m_cameraIndex);
            report += line;

            // 프레임 밀봉 카메라 스냅샷. 렌더 스레드가 읽는 값이 여기 전부 있어야
            // 하고, 이식 전후로 이 숫자들이 같아야 한다.
            std::snprintf(line, sizeof(line),
                "  snapshot: eye(%.6f %.6f %.6f) fwd(%.6f %.6f %.6f) right(%.6f %.6f %.6f)"
                " fov %.6f near %.6f far %.6f ortho %d\n",
                data->GetFrameSnapshot().eyePosition.m128_f32[0], data->GetFrameSnapshot().eyePosition.m128_f32[1], data->GetFrameSnapshot().eyePosition.m128_f32[2],
                data->GetFrameSnapshot().forward.m128_f32[0], data->GetFrameSnapshot().forward.m128_f32[1], data->GetFrameSnapshot().forward.m128_f32[2],
                data->GetFrameSnapshot().right.m128_f32[0], data->GetFrameSnapshot().right.m128_f32[1], data->GetFrameSnapshot().right.m128_f32[2],
                data->GetFrameSnapshot().fov, data->GetFrameSnapshot().nearPlane, data->GetFrameSnapshot().farPlane,
                static_cast<int>(data->GetFrameSnapshot().isOrthographic));
            report += line;

            // 뷰·투영과 그 역행렬. 패스들이 이제 이것만 읽는다.
            const char* matrixNames[4] = { "view", "proj", "invView", "invProj" };
            const Mathf::Matrix matrices[4] = {
                data->GetFrameSnapshot().view, data->GetFrameSnapshot().projection,
                data->GetFrameSnapshot().inverseView, data->GetFrameSnapshot().inverseProjection };

            for (int m = 0; m < 4; ++m)
            {
                std::snprintf(line, sizeof(line), "  %s", matrixNames[m]);
                report += line;
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        std::snprintf(line, sizeof(line), " %.6f", matrices[m].m[r][c]);
                        report += line;
                    }
                }
                report += "\n";
            }

            report += "  cascadeEnd:";
            for (float end : data->m_cascadeEnd)
            {
                std::snprintf(line, sizeof(line), " %.6f", end);
                report += line;
            }
            report += "\n";

            for (size_t i = 0; i < data->m_cascadeInfo.size(); ++i)
            {
                const auto& info = data->m_cascadeInfo[i];
                std::snprintf(line, sizeof(line),
                    "  [%zu] eye(%.6f %.6f %.6f) look(%.6f %.6f %.6f) near %.6f far %.6f w %.6f h %.6f\n",
                    i,
                    info.m_eyePosition.m128_f32[0], info.m_eyePosition.m128_f32[1], info.m_eyePosition.m128_f32[2],
                    info.m_lookAt.m128_f32[0], info.m_lookAt.m128_f32[1], info.m_lookAt.m128_f32[2],
                    info.m_nearPlane, info.m_farPlane, info.m_viewWidth, info.m_viewHeight);
                report += line;

                const Mathf::Matrix lvp = info.m_lightViewProjection;
                report += "      lvp";
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        std::snprintf(line, sizeof(line), " %.6f", lvp.m[r][c]);
                        report += line;
                    }
                }
                report += "\n";
            }

            // 스냅샷이 살아 있는 카메라와 실제로 같은 값인지 확인한다.
            //
            // 이식은 "패스가 camera.CalculateView()를 부르던 것을 스냅샷 읽기로
            // 바꾼다"인데, 그게 동작 보존인지는 두 값이 같아야 성립한다. 픽셀로는
            // 노이즈에 묻혀 확인이 안 되므로 여기서 직접 대조한다.
            // 락스텝이 걸린 지금은 같아야 하고, 락스텝을 푼 뒤에는 의도적으로
            // 달라진다 — 그때 이 명령은 '한 프레임 차이'를 보여 주는 도구가 된다.
            {
                const Mathf::Matrix liveView = camera->CalculateView();
                const Mathf::Matrix liveProj = camera->CalculateProjection();

                float maxMatrixDelta = 0.f;
                for (int r = 0; r < 4; ++r)
                {
                    for (int c = 0; c < 4; ++c)
                    {
                        maxMatrixDelta = (std::max)(maxMatrixDelta,
                            std::fabs(liveView.m[r][c] - Mathf::Matrix(data->GetFrameSnapshot().view).m[r][c]));
                        maxMatrixDelta = (std::max)(maxMatrixDelta,
                            std::fabs(liveProj.m[r][c] - Mathf::Matrix(data->GetFrameSnapshot().projection).m[r][c]));
                    }
                }

                const float eyeDelta = Mathf::Vector3(
                    Mathf::Vector3(data->GetFrameSnapshot().eyePosition) - Mathf::Vector3(camera->m_eyePosition)).Length();

                std::snprintf(line, sizeof(line),
                    "  live-vs-snapshot: matrix %.9f eye %.9f fov %.9f near %.9f far %.9f\n",
                    maxMatrixDelta, eyeDelta,
                    std::fabs(data->GetFrameSnapshot().fov - camera->m_fov),
                    std::fabs(data->GetFrameSnapshot().nearPlane - camera->m_nearPlane),
                    std::fabs(data->GetFrameSnapshot().farPlane - camera->m_farPlane));
                report += line;
            }

            const auto& constant = data->m_shadowCamera.m_shadowMapConstant;
            std::snprintf(line, sizeof(line),
                "  constant: end1 %.6f end2 %.6f end3 %.6f w %d h %d cascade %d\n",
                constant.m_casCadeEnd1, constant.m_casCadeEnd2, constant.m_casCadeEnd3,
                constant.m_shadowMapWidth, constant.m_shadowMapHeight,
                static_cast<int>(constant.useCasCade));
            report += line;
        }

        if (report.empty()) report = "(카메라 없음)\n";

        std::printf("[shadowinfo]\n%s", report.c_str());
        std::fflush(stdout);
        Debug->LogWarning("[shadowinfo]\n" + report);
    }
    else if (cmd == "crash.status")
    {
        // 이번 실행이 덤프를 남길 수 있는 상태인지 확인한다.
        // 크래시가 난 뒤에 '덤프가 없네'로 알게 되는 일이 없도록 하는 것이 목적.
        const bool ready = Log::HasCrashDumpWriter();
        std::printf("[CLI] 크래시 덤프 기록자: %s\n", ready ? "등록됨" : "미등록");
        std::printf("[CLI] 덤프 경로: %ls\n", PathFinder::DumpPath().c_str());
        std::printf("[CLI] 무인 모드: %s\n", CoreWindow::IsUnattended() ? "예(대화상자 없음)" : "아니오");
    }
    else if (cmd == "crash.test")
    {
        // 크래시 핸들러 자체를 검증하는 수단.
        //
        // 덤프 경로는 크래시가 나야만 실행되므로 평소에는 검증이 안 되고,
        // 검증되지 않은 채로 조용히 망가져 있었다(로그에 CRASH 줄만 남고 .dmp 없음).
        // 종류별로 일부러 죽여서 각 경로가 .dmp와 .txt를 남기는지 본다.
        const std::string kind = (parts.size() > 1) ? parts[1] : std::string("av");

        std::printf("[CLI] crash.test %s - 의도적으로 프로세스를 죽인다\n", kind.c_str());
        Debug->LogWarning("[crash.test] 의도적 크래시: " + kind);
        Log::FlushNow();

        if (kind == "av")
        {
            // 널 역참조. volatile이라 최적화로 사라지지 않는다.
            volatile int* nullPointer = nullptr;
            *nullPointer = 1;
        }
        else if (kind == "abort")
        {
            std::abort();
        }
        else if (kind == "terminate")
        {
            std::terminate();
        }
        else if (kind == "throw")
        {
            // 미처리 C++ 예외 → terminate 경로.
            throw std::runtime_error("crash.test throw");
        }
        else
        {
            std::printf("[CLI] 알 수 없는 종류: %s (av|abort|terminate|throw)\n", kind.c_str());
        }
    }
    else
    {
        std::printf("[CLI] 알 수 없는 명령: %s  ('help' 참고)\n", cmd.c_str());
    }
}

bool ConsoleCommandSystem::IsEditorCameraFollowing() noexcept
{
    return g_editorCameraFollowsGame;
}

bool ConsoleCommandSystem::MatchEditorCameraToGameCamera()
{
    Camera* editorCamera = EnhancedSceneRenderer::GetEditorCamera();
    const auto gameCamera = CameraManagement->GetLastCamera();
    if (nullptr == editorCamera || !gameCamera || gameCamera.get() == editorCamera)
    {
        return false;
    }

    editorCamera->m_eyePosition = gameCamera->m_eyePosition;
    editorCamera->m_forward = gameCamera->m_forward;
    editorCamera->m_up = gameCamera->m_up;
    editorCamera->m_right = gameCamera->m_right;
    editorCamera->m_lookAt = gameCamera->m_lookAt;
    editorCamera->rotate = gameCamera->rotate;

    // 투영도 맞춘다. 시점만 같고 화각이 다르면 두 그림이 여전히 안 겹쳐
    // 대조가 성립하지 않는다.
    editorCamera->m_fov = gameCamera->m_fov;
    editorCamera->m_nearPlane = gameCamera->m_nearPlane;
    editorCamera->m_farPlane = gameCamera->m_farPlane;
    editorCamera->m_isOrthographic = gameCamera->m_isOrthographic;

    // ★ HandleMovement의 누적 각도까지 되돌린다.
    //
    //   그러지 않으면 다음에 우클릭하는 순간 카메라가 옛 자세로 튄다 —
    //   forward는 우클릭 중에만 deltaYaw/deltaPitch에서 다시 만들어지므로
    //   여기서 넣은 값이 그때 통째로 버려진다.
    //
    //   HandleMovement의 조립(qYaw 뒤 그 축의 qPitch)을 역으로 푼 것이다:
    //     forward = (sin(yaw)cos(pitch), -sin(pitch), cos(yaw)cos(pitch))
    {
        Mathf::Vector3 forward{};
        XMStoreFloat3(&forward, XMVector3Normalize(editorCamera->m_forward));
        editorCamera->deltaYaw = std::atan2(forward.x, forward.z);
        editorCamera->deltaPitch = -std::asin(std::clamp(forward.y, -1.f, 1.f));
    }
    return true;
}

void ConsoleCommandSystem::PrintHelp() const
{
    std::printf(
        "\n[CLI] 사용 가능한 명령\n"
        "  scene.new [이름]     빈 씬을 만들어 활성화한다(기능 테스트 씬 저작용)\n"
        "  scene.load <경로>    씬을 로드한다(활성 씬은 그대로)\n"
        "  scene.switch <경로>  씬을 로드하고 활성 씬으로 교체한다(언로드 유발)\n"
        "  scene.save <경로>    활성 씬을 .creator로 저장한다\n"
        "  scene.dump [라벨]    활성 씬의 오브젝트 계층을 로그에 남긴다\n"
        "  game.pak             게임 에셋 pak을 생성한다(x64\\GameBuild\\, B2의 Pak 단계)\n"
        "  model.load <경로>    모델을 에셋으로 임포트한다(fbx/gltf/glb/obj)\n"
        "  model.place <이름>   임포트한 모델을 활성 씬에 배치한다\n"
        "  object.create <이름> [타입]  빈 오브젝트를 만든다(Empty/Light/Camera/Mesh)\n"
        "  object.rename <이전> <새>  오브젝트 이름을 바꾼다(같은 모델 여러 번 배치용)\n"
        "  object.transform <이름> <px py pz> [rx ry rz] [sx sy sz]  변환을 지정한다(회전은 도)\n"
        "  object.property <오브젝트> <컴포넌트> <필드> <값>  리플렉션으로 프로퍼티를 설정한다\n"
        "  play / stop          에디터의 재생·정지와 같은 동작\n"
        "  lifecycle.trace on [틱프레임]|off|clear|status  생명주기 호출 순서를 받아 적는다\n"
        "  lifecycle.registry on|off|status  생명주기 디스패치 경로 전환(9-1, 씬 재로드 필요)\n"
        "  lifecycle.dump [파일]  기록을 TSV로 쓴다(기록 0건이면 실패로 끝난다)\n"
        "  lifecycle.stress destroy|churn|reentrant [개수]  수명 경로를 흔든다(reentrant는 순회 한복판)\n"
        "  gc.stats|gc.delta [라벨]  관리 힙 지표(수집 횟수·힙 크기). delta는 첫 호출을 기준선으로\n"
        "  gc.collect           관리 힙 확정 수집(씬 전환이 자동으로 부르는 그 경로)\n"
        "  bt.status            행동 트리 지표(트리 수·틱 누계·프레임당 경계 통과)\n"
        "  bt.reset             BT 누계만 0으로(트리는 그대로) — 구간 측정용\n"
        "  camera.editor match|follow on|off|status  에디터 카메라를 게임 카메라와 같은 시점으로\n"
        "  window.resize <너비> <높이>  창 클라이언트 크기를 바꾼다(해상도 검증용)\n"
        "  window.info          엔진이 인식하는 클라이언트 크기를 출력한다\n"
        "  dx12.selftest [파일]  DX12 브링업 자가 검증(삼각형 렌더 → PNG)\n"
        "  dx12.psocache [파일]  PSO 캐시 자가 검증(2회차 컴파일 0건)\n"
        "  dx12.uploadring      업로드 링 자가 검증(정렬·구간분리·되감기·넘침·GPU도달)\n"
        "  dx12.descriptorheap  디스크립터 링·샘플러 힙 자가 검증(연속성·구간·되감기·넘침·중복제거)\n"
        "  dx12.rendergraph     렌더 그래프 검증(순서·흐름·배리어·컬링·실행)\n"
        "  dx12.gbuffer         GBuffer 패스 검증(입력조립·MRT5·깊이·그래프 배리어)\n"
        "  dx12.resize          크기 추종 검증(DX11 정책·DX12 리사이즈·리사이즈 후 렌더)\n"
        "  dx12.parallel        커맨드 기록 병렬화 검증(링 원자성·순차 대비 동일성)\n"
        "  render.exposure      자동 노출이 무엇을 재고 무엇을 결정했는지\n"
        "  render.post           비활성 레거시 명령(DX11 포스트 설정)\n"
        "  render.rtinfo        창·뷰포트·추종 텍스처 크기를 나란히 찍는다\n"
        "  dx12.scene           씬 연결 검증(카메라 스냅샷·메시 업로드·실제 드로우)\n"
        "  dx12.grid            그리드 패스 검증(라인·셀 내부·밀도·카메라 반응)\n"
        "  dx12.gizmoline       기즈모 라인 패스 검증(도형 정점 수·픽셀·드로우 병합)\n"
        "  dx12.gizmoicon       기즈모 아이콘 패스 검증(빌보드 회전·알파 상한·배칭)\n"
        "  dx12.wireframe       와이어프레임 패스 검증(변·내부 비채움·인스턴싱·메시 캐시)\n"
        "  dx12.gizmoscene      Gizmo 씬 연결 검증(밀봉 복사·4패스 체인·타깃 공유)\n"
        "  dx12.shadowquality   그림자 품질 검증(경사 비례 편향·캐스케이드 경계 블렌딩 A/B)\n"
        "  dx12.skybox          스카이박스 패스 검증(면 방향·원평면 밀어넣기·전면 커버)\n"
        "  dx12.ibl             IBL 생성 체인 검증(rect→cube·조도·프리필터·BRDF LUT)\n"
        "  dx12.iblshade        IBL 앰비언트 소비 검증(끔=검정·조도 방향성·금속 정반사)\n"
        "  dx12.skinning        GBuffer 스키닝 검증(본 이동·가중 혼합·비스킨드 불변)\n"
        "  dx12.sss             SSS 패스 검증(번짐·축 분리·표면 추종·에너지)\n"
        "  dx12.decal           데칼 패스 검증(상자 판정·하늘 게이트·원본 혼합 3종·배칭)\n"
        "  dx12.ssr             SSR 패스 검증(반사 발생·금속 마스크·두께 게이트·비트플래그)\n"
        "  dx12.fog             볼류메트릭 포그 검증(산란·누적 투과율·시간축 히스토리·합성)\n"
        "  dx12.bench11         DX11 vs DX12 API 오버헤드 실측(전제 검증 · Release 전용)\n"
        "  dx12.encoderbench    인코더 오버헤드 실측(R3 착수 조건 · Release 전용)\n"
        "  dx12.live on|status      EnhancedRenderer 메인 런타임 상태\n"
        "  render.backend dx12|status  고정 백엔드 확인(dx11은 dead code)\n"
        "  pix.capture begin|end|status  PIX 주입 실행의 명시적 GPU 캡처 경계\n"
        "  ui.rect <오브젝트|*>  오브젝트 이하의 worldRect·sizeDelta·앵커·배율을 출력한다\n"
        "  ui.anchor <오브젝트> <minX> <minY> <maxX> <maxY>  앵커를 직접 지정한다\n"
        "  ui.size <오브젝트> <x> <y>  sizeDelta를 직접 지정한다\n"
        "  ui.hitbox            버튼의 rect와 클릭 판정 상자를 나란히 출력한다\n"
        "  script.add <오브젝트> <타입>  C# 스크립트를 오브젝트에 부착한다\n"
        "  script.fields <id>   스크립트의 노출 필드와 현재 값을 확인한다\n"
        "  script.set <id> <인덱스> <값>  노출 필드 값을 바꾼다\n"
        "  script.reload        게임 스크립트 어셈블리를 다시 로드한다(핫리로드)\n"
        "  script.status        CLR 상태와 활성 스크립트 수를 확인한다\n"
        "  gpu.baseline         현재 상태를 기준선으로 삼는다\n"
        "  gpu.census [라벨]    VRAM과 엔진 에셋 수를 로그에 기록\n"
        "  gpu.delta [라벨]     기준선 대비 증감을 기록(씬 왕복 누수 확인용)\n"
        "  assets.unload        사용하지 않는 에셋 캐시 정리\n"
        "  wait <프레임>        지정 프레임만큼 다음 명령을 미룬다\n"
        "  log.flush            로그를 디스크에 즉시 반영\n"
        "  render.syncstats [reset]  게임/렌더 락스텝 배리어의 대기 비용을 출력한다\n"
        "  render.shadowinfo    그림자 캐스케이드 계산 결과를 출력한다(스냅샷 검증용)\n"
        "  crash.status         크래시 덤프 기록자 등록 여부와 덤프 경로를 확인한다\n"
        "  crash.test <종류>    일부러 죽여 덤프 경로를 검증한다(av|abort|terminate|throw)\n"
        "  quit                 에디터 종료\n"
        "\n실행 인자: --exec \"<명령>\"  |  --script <파일>  |  --console\n"
        "           --heapcheck  CRT 디버그 힙 전수 검사(힙 손상을 손상 시점에서 잡는다, 매우 느림)\n\n");
}

void ConsoleCommandSystem::Shutdown()
{
    m_running.store(false, std::memory_order_release);

    if (!m_stdinThread.joinable()) return;

    // 블로킹 중인 콘솔 읽기를 깨운다.
    //
    // 예전에는 깨우지 않고 그냥 detach했다. 그러면 이 스레드는 getline 안 —
    // 즉 CRT의 stdio·iostream 내부 — 에 갇힌 채로 남고, 프로세스가 죽을 때
    // ExitProcess가 그 임의 지점에서 스레드를 강제 종료한다. 하필 힙 락을 쥔
    // 순간이었다면 뒤이은 종료 절차가 그 위에서 힙을 만지게 되고, 결과는
    // 종료 구간의 간헐적 힙 손상(0xC0000374)이다. 회귀 세트가 전부 CLI
    // 실행이라 이 경로에서만 나타났고, 에디터를 손으로 쓸 때는 보이지 않았다.
    ::CancelSynchronousIo(static_cast<HANDLE>(m_stdinThread.native_handle()));

    // 취소가 항상 듣는다는 보장은 없어서(콘솔 구현에 따라 다르다) 기한을 둔다.
    // 종료가 통째로 막히는 것은 더 나쁘다.
    constexpr auto kJoinTimeout = std::chrono::milliseconds(500);
    const bool exited = m_stdinDoneFuture.valid()
        && m_stdinDoneFuture.wait_for(kJoinTimeout) == std::future_status::ready;

    if (exited)
    {
        m_stdinThread.join();
        return;
    }

    // 최후의 수단. 여기까지 오면 예전과 같은 위험이 남지만, 적어도 스크립트
    // 실행 경로(--script/--exec)에는 이 스레드가 아예 없다.
    std::fputs("[CLI] 표준 입력 스레드를 회수하지 못했다 - 종료 중 위험 구간.\n", stdout);
    std::fflush(stdout);
    m_stdinThread.detach();
}
#endif // !DYNAMICCPP_EXPORTS







