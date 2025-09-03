#include "ShaderDSL.h"
#include <regex>
#include <algorithm>
#include <string>

static std::string Trim(std::string s) {
    s.erase(0, s.find_first_not_of(" \t\r\n"));
    s.erase(s.find_last_not_of(" \t\r\n") + 1);
    return s;
}

static std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

bool ParseShaderDSL(std::string_view srcView, ShaderAssetDesc& out)
{
    std::string src(srcView);

    // 1) �ּ� ���� (ǥ�� regex�� ��� / ������ raw-string ������)
    {
        // // ... (���� �ּ�)
        static const std::regex lineComments(R"SHD(//[^\r\n]*)SHD");
        src = std::regex_replace(src, lineComments, "");

        static const std::regex blockComments(R"SHD(/\*[\s\S]*?\*/)SHD");
        src = std::regex_replace(src, blockComments, "");
    }

    // 2) ��� �Ľ�: Shader "Name"? : Tag { ... }  /  Shader : Tag { ... }
    {
        std::smatch m;

        // �̸��� �ִ� ����
        static const std::regex header1(
            R"SHD(Shader\s*(?:"([^"]+)")?\s*:\s*([A-Za-z0-9_.-]+)\s*\{)SHD"
        );
        if (std::regex_search(src, m, header1)) {
            if (m[1].matched) out.name = m[1];
            out.tag = m[2];
        }
        else {
            // �̸� ���� ������
            static const std::regex header2(
                R"SHD(Shader\s*:\s*([A-Za-z0-9_.-]+)\s*\{)SHD"
            );
            if (std::regex_search(src, m, header2)) {
                out.tag = m[1];
            }
            else {
                return false; // ��� ���� ����ġ
            }
        }
    }

    // 3) ���� ����
    const size_t l = src.find('{');
    const size_t r = src.rfind('}');
    if (l == std::string::npos || r == std::string::npos || r <= l) return false;
    std::string body = src.substr(l + 1, r - l - 1);

    // 4) key = "value" (��ǥ/�����ݷ� ���� ����, �ܼ� ��Ī)
    {
        static const std::regex assign(
            R"SHD(([A-Za-z_][A-Za-z0-9_]*)\s*=\s*"([^"]*)")SHD"
        );
        for (std::sregex_iterator it(body.begin(), body.end(), assign), end; it != end; ++it) {
            std::string key = Lower(Trim((*it)[1]));
            std::string val = Trim((*it)[2]);

            if (key == "vertexpass" || key == "vs" || key == "vertex")
                out.pass.vs = val;
            else if (key == "pixelpass" || key == "ps" || key == "pixel")
                out.pass.ps = val;
            else if (key == "geometrypass" || key == "gs" || key == "geometry")
                out.pass.gs = val;
            else if (key == "hullpass" || key == "hs" || key == "hull")
                out.pass.hs = val;
            else if (key == "domainpass" || key == "ds" || key == "domain")
                out.pass.ds = val;
            else if (key == "computepass" || key == "cs" || key == "compute")
                out.pass.cs = val;
            else if (key == "queue" || key == "tag")
                out.pass.queueTag = val;
        }
    }

    // 5) Keywords = ["A","B", ...]
    {
        static const std::regex kw(
            R"SHD((Keywords|KEYWORDS)\s*=\s*\[\s*([^\]]*)\])SHD"
        );
        std::smatch m;
        if (std::regex_search(body, m, kw)) {
            std::string payload = m[2];
            static const std::regex q(R"SHD("([^"]+)")SHD");
            for (std::sregex_iterator it(payload.begin(), payload.end(), q), end; it != end; ++it)
                out.pass.keywords.push_back((*it)[1]);
        }
    }

    // �̸��� ��������� ȣ���ڰ� ���ϸ� ������ ä��� ��
    return true;
}
