#include "../VulkanSelfTest.h"

#include <AuthoringRymlErrorPolicy.h>
#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

// PBR-W1 — normal-map 저작 유무가 **실장면 프레임**에서 단일 정본으로 도달하는가.
//
// ★ W1 의 배선(저작 material snapshot → scene snapshot → GBuffer/Forward instance)은
//   진작 끝나 있었고 Debug 빌드도 통과했다. 계획서 §1 이 "실제 Gunner/primitive
//   런타임 장면 판정은 아직 하지 않음" 으로 남겨 둔 것이 정확히 이 검사다.
//   격리 fixture 는 이 축을 못 재운다 — 스냅샷이 없는 경계에서는 draw.useNormalMap
//   폴백이 쓰이기 때문이다(EnhancedGBufferPass.cpp:439). 제품 경로의 정본이
//   스냅샷이라는 것은 **스냅샷이 있는 프레임**에서만 확인된다.
//
// ★ 왜 픽셀이 아니라 manifest 를 읽나 ─ 캡처는 attachment 를 통째로 남기지
//   draw 별 영역을 남기지 않는다. "노멀맵 있는 draw 의 픽셀" 만 떼어낼 수단이
//   아직 없다(draw 별 영역은 W0 의 남은 항목이다). 대신 manifest 는 draw 마다
//   useNormalMap 과 텍스처 슬롯별 authored 를 이미 적고 있어, **두 유도가 같은
//   답을 내는가**를 실장면 데이터 위에서 직접 물을 수 있다.
namespace
{
    struct DrawNormalState
    {
        std::uint32_t useNormalMap{ 0 };
        bool normalSlotPresent{ false };
        bool normalAuthored{ false };
    };

    // ★ 이름을 파일 고유로 둔다. Editor 는 유니티 빌드라 여러 .cpp 가 한 묶음으로
    //   컴파일되고, 그 안에서는 **익명 네임스페이스가 격리해 주지 않는다** —
    //   `ReadText` 로 두었더니 PbrCaptureCompare.cpp 의 동명 함수와 C2084 로 부딪쳤다.
    bool ReadNormalPairManifestText(const std::filesystem::path& path,
        std::string& out, std::string& error)
    {
        std::ifstream input(path, std::ios::binary);
        if (!input) { error = "파일을 열 수 없다: " + path.string(); return false; }
        out.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        return true;
    }

    bool ReadDraws(const std::filesystem::path& directory,
        std::vector<DrawNormalState>& out, std::string& error)
    {
        std::string text;
        if (!ReadNormalPairManifestText(directory / "manifest.json", text, error)) return false;
        Authoring::EnsureRymlErrorPolicy();
        const ryml::Tree tree = ryml::parse_in_arena(ryml::to_csubstr(text));
        const ryml::ConstNodeRef root = tree.rootref();
        if (!root.is_map() || !root.has_child("draws"))
        {
            error = "manifest에 draws가 없다: " + directory.string();
            return false;
        }
        for (const ryml::ConstNodeRef node : root["draws"])
        {
            // 스냅샷이 없는 draw 는 useNormalMap 자체를 적지 않는다. 그런 draw 가
            // 있다면 제품 경로를 안 탄 것이므로 조용히 건너뛰지 않고 실패시킨다.
            if (!node.has_child("useNormalMap"))
            {
                error = "useNormalMap이 없는 draw가 있다(제품 밀봉 경로 밖): "
                    + directory.string();
                return false;
            }
            DrawNormalState state;
            node["useNormalMap"] >> state.useNormalMap;
            if (node.has_child("textures"))
            {
                for (const ryml::ConstNodeRef binding : node["textures"])
                {
                    std::string property;
                    binding["property"] >> property;
                    if (property != "normalMap") continue;
                    state.normalSlotPresent = true;
                    std::string authored;
                    binding["authored"] >> authored;
                    state.normalAuthored = (authored == "true");
                    break;
                }
            }
            out.push_back(state);
        }
        return !out.empty();
    }
}

bool RunPbrNormalPairVerification(const std::string& directory, std::string& outLog)
{
    std::string error;
    std::vector<DrawNormalState> draws;
    if (!ReadDraws(directory, draws, error))
    {
        outLog += error + "\n";
        return false;
    }

    std::size_t withNormal = 0;
    std::size_t withoutNormal = 0;
    std::size_t slotMissing = 0;
    std::size_t disagreed = 0;
    for (std::size_t i = 0; i < draws.size(); ++i)
    {
        const DrawNormalState& draw = draws[i];
        if (0 != draw.useNormalMap) ++withNormal; else ++withoutNormal;
        if (!draw.normalSlotPresent) { ++slotMissing; continue; }

        // ★ 이것이 W1 의 본문이다. 왼쪽은 저작 material snapshot 이 인스턴스
        //   채널로 실어 보낸 값이고, 오른쪽은 같은 프레임에 실제로 바인딩된
        //   텍스처의 저작 여부다. 둘은 **다른 곳에서 유도된 같은 사실**이라
        //   어긋나면 정본이 둘이라는 뜻이다.
        const bool authoredSaysUse = draw.normalAuthored;
        if ((0 != draw.useNormalMap) != authoredSaysUse)
        {
            outLog += "draw " + std::to_string(i) + ": useNormalMap="
                + std::to_string(draw.useNormalMap) + " 인데 normalMap authored="
                + (authoredSaysUse ? "true" : "false") + "\n";
            ++disagreed;
        }
    }

    // ★ 빈 집합·한쪽뿐인 집합 위에서는 위 단정이 공짜로 참이다. 대조가 실제로
    //   존재하는지를 먼저 물어야 "통과" 가 뜻을 갖는다. fixture 가 빠졌거나
    //   배치가 실패해도 게이트가 초록이 되는 길을 여기서 막는다.
    const bool contrast = (0 != withNormal) && (0 != withoutNormal);
    const bool passed = contrast && (0 == disagreed) && (0 == slotMissing);

    outLog += "normal pair — draw " + std::to_string(draws.size())
        + " · useNormalMap 1/0 = " + std::to_string(withNormal) + "/"
        + std::to_string(withoutNormal)
        + " · 슬롯 없음 " + std::to_string(slotMissing)
        + " · 불일치 " + std::to_string(disagreed) + "\n";
    if (!contrast)
    {
        outLog += "대조쌍이 없다 — 노멀맵 있는 draw 와 없는 draw 가 같은 캡처에 있어야 한다\n";
    }
    if (0 != slotMissing)
    {
        outLog += "normalMap 슬롯이 없는 draw가 있다 — 텍스처 표가 고정 슬롯을 잃었다\n";
    }
    return passed;
}
