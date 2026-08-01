#ifndef DYNAMICCPP_EXPORTS
#include "ConsoleCommandSystem.h"

#include "SceneManager.h"
#include "ClrHost.h"
#include "ScriptComponent.h"
#include "PrefabUtility.h"
#include "ComponentFactory.h"
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
#include "DeviceResources.h"
#include "DeviceState.h"
#include "LogSystem.h"
#include "PathFinder.h"
#include "CoreWindow.h"
#include "RHI/DX12/EnhancedSceneRenderer.h"
#include "RenderPassData.h"

#include <Windows.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace
{
    // 앞뒤 공백 제거
    std::string TrimLine(const std::string& s)
    {
        const auto begin = s.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return {};
        const auto end = s.find_last_not_of(" \t\r\n");
        return s.substr(begin, end - begin + 1);
    }

    std::vector<std::string> Split(const std::string& line)
    {
        std::vector<std::string> parts;
        std::istringstream iss(line);
        std::string token;
        while (iss >> token) parts.push_back(token);
        return parts;
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

        FILE* dummy = nullptr;
        freopen_s(&dummy, "CONIN$", "r", stdin);
        freopen_s(&dummy, "CONOUT$", "w", stdout);
        freopen_s(&dummy, "CONOUT$", "w", stderr);
        std::ios::sync_with_stdio(true);

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
        Scene* scene = SceneManagers->LoadScene(parts[1]);
        if (!scene)
        {
            Debug->LogError("[CLI] 씬 로드 실패: " + parts[1]);
            std::printf("[CLI] 씬 로드 실패: %s\n", parts[1].c_str());
            return;
        }

        if (cmd == "scene.switch")
        {
            SceneManagers->ActivateScene(scene, true);
        }
        std::printf("[CLI] %s 완료: %s\n", cmd.c_str(), parts[1].c_str());
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
            std::sscanf(rawValue.c_str(), "%f,%f", &v.x, &v.y);
            clr.SetFieldFloat2(id, index, v);
            break;
        }
        case ClrHost::ScriptFieldType::Float3:
        {
            ClrHost::ScriptFloat3 v{};
            std::sscanf(rawValue.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
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
    else if (cmd == "dx12.sharedtexture")
    {
        // 병존 출력 경로 실증(PHASE 3-3 미결 결정). DX11 디바이스가 필요하므로
        // 에디터가 떠 있는 상태에서만 의미가 있다.
        EnhancedSceneRenderer renderer;
        std::string log;
        const bool passed = renderer.RunSharedTextureTest(log);

        std::printf("%s", log.c_str());
        Debug->LogWarning(std::string("[dx12.sharedtexture] ") + (passed ? "통과" : "실패") + "\n" + log);
        std::printf("[CLI] dx12.sharedtexture %s\n", passed ? "통과" : "실패");
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
        // 도달했는지를 UI 계산과 같은 출처(g_ClientRect)로 확인한다.
        const auto& client = DirectX11::DeviceStates->g_ClientRect;
        std::printf("[CLI] 클라이언트 영역: %.0fx%.0f\n", client.width, client.height);
        Debug->LogWarning("[CLI] 클라이언트 영역: " +
            std::to_string(static_cast<int>(client.width)) + "x" +
            std::to_string(static_cast<int>(client.height)));
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
        if (auto* device = DirectX11::DeviceResources::GetActive())
        {
            device->ResetLiveObjectBaseline();
            std::printf("[CLI] 기준선 초기화 (이후 gpu.delta는 이 시점과 비교)\n");
        }
    }
    else if (cmd == "gpu.census" || cmd == "gpu.delta")
    {
        const std::string label = (parts.size() > 1) ? parts[1] : std::string("CLI 요청");
        if (auto* device = DirectX11::DeviceResources::GetActive())
        {
            // 실행 중에는 VRAM만 남는다. 타입별 집계는 디버그 레이어를 망가뜨려
            // 이후 렌더에서 죽으므로 종료 시점 리포트로만 얻을 수 있다.
            if (cmd == "gpu.delta") device->LogLiveObjectDelta(label);
            else                    device->LogLiveObjectCensus(label);

            std::printf("[CLI] GPU %s 기록: %s (VRAM 기준, 타입별 집계는 종료 리포트 참조)\n",
                (cmd == "gpu.delta") ? "증감" : "집계", label.c_str());
        }
    }
    else if (cmd == "assets.unload")
    {
        DataSystems->UnloadUnusedAssets();
        std::printf("[CLI] 사용하지 않는 에셋 정리 요청\n");
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

void ConsoleCommandSystem::PrintHelp() const
{
    std::printf(
        "\n[CLI] 사용 가능한 명령\n"
        "  scene.load <경로>    씬을 로드한다(활성 씬은 그대로)\n"
        "  scene.switch <경로>  씬을 로드하고 활성 씬으로 교체한다(언로드 유발)\n"
        "  scene.dump [라벨]    활성 씬의 오브젝트 계층을 로그에 남긴다\n"
        "  model.load <경로>    모델을 에셋으로 임포트한다(fbx/gltf/glb/obj)\n"
        "  model.place <이름>   임포트한 모델을 활성 씬에 배치한다\n"
        "  play / stop          에디터의 재생·정지와 같은 동작\n"
        "  window.resize <너비> <높이>  창 클라이언트 크기를 바꾼다(해상도 검증용)\n"
        "  window.info          엔진이 인식하는 클라이언트 크기를 출력한다\n"
        "  dx12.selftest [파일]  DX12 브링업 자가 검증(삼각형 렌더 → PNG)\n"
        "  dx12.psocache [파일]  PSO 캐시 자가 검증(2회차 컴파일 0건)\n"
        "  dx12.uploadring      업로드 링 자가 검증(정렬·구간분리·되감기·넘침·GPU도달)\n"
        "  dx12.descriptorheap  디스크립터 링·샘플러 힙 자가 검증(연속성·구간·되감기·넘침·중복제거)\n"
        "  dx12.sharedtexture   병존 출력 경로 검증(DX12가 그린 것을 DX11이 SRV로 보는가)\n"
        "  dx12.rendergraph     렌더 그래프 검증(순서·순환·배리어·컬링·실행)\n"
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







