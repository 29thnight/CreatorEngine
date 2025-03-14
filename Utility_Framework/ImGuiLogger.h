#pragma once
#include "imgui.h"
#include "Core.Minimal.h"

namespace Log
{
    inline std::string MatrixToString(const DirectX::XMMATRIX& matrix)
    {
        using namespace DirectX;
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, matrix); // XMMATRIX → XMFLOAT4X4 변환

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3); // 소수점 3자리까지 출력
        oss << "[ " << m._11 << ", " << m._12 << ", " << m._13 << ", " << m._14 << " ]\n";
        oss << "[ " << m._21 << ", " << m._22 << ", " << m._23 << ", " << m._24 << " ]\n";
        oss << "[ " << m._31 << ", " << m._32 << ", " << m._33 << ", " << m._34 << " ]\n";
        oss << "[ " << m._41 << ", " << m._42 << ", " << m._43 << ", " << m._44 << " ]\n";

        return oss.str();
    }
}

class Logger : public Singleton<Logger>
{
private:
	friend class Singleton;

	Logger() = default;
	~Logger() = default;

public:
    void AddLog(const char* fmt, ...)
    {
        char buf[1024];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);

        logs.push_back(buf);
        scrollToBottom = true;
    }

    void AddMatrixLog(const DirectX::XMMATRIX& matrix, const char* label = "Matrix")
    {
        std::string matrixStr = Log::MatrixToString(matrix);
        AddLog("%s:\n%s", label, matrixStr.c_str());
    }

    void Draw(const char* title)
    {
        ImGui::Begin(title);

        if (ImGui::Button("Clear") || logs.size() > 2000)
        {
            logs.clear();
        }

        ImGui::Separator();
        ImGui::BeginChild("LogRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& log : logs)
        {
            ImGui::TextUnformatted(log.c_str());
        }

        if (scrollToBottom)
        {
            ImGui::SetScrollHereY(1.0f);
            scrollToBottom = false;
        }

        ImGui::EndChild();
        ImGui::End();
    }

private:
    std::vector<std::string> logs;
    bool scrollToBottom = false;
};

static inline auto& LoggerSystem = Logger::GetInstance();

