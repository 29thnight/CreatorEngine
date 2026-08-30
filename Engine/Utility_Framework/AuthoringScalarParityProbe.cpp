#include "AuthoringScalarParityProbe.h"

#include "AuthoringRymlErrorPolicy.h"
#include "ReflectionYml.h"

#include <ryml/ryml.hpp>
#include <ryml/ryml_std.hpp>

#include <charconv>
#include <cstdio>
#include <limits>

namespace Authoring
{
	namespace
	{
		// 값을 문자열로 정규화한다. 부동소수는 표기 차이(1 vs 1.0)로 갈리면 안 되므로
		// 충분한 자릿수로 찍고, NaN/Inf는 이름으로 적는다.
		std::string Normalize(float value)
		{
			if (value != value) return "nan";
			if (value == std::numeric_limits<float>::infinity()) return "inf";
			if (value == -std::numeric_limits<float>::infinity()) return "-inf";
			char buffer[64]{};
			std::snprintf(buffer, sizeof(buffer), "%.9g", static_cast<double>(value));
			return buffer;
		}

		std::string Normalize(bool value) { return value ? "true" : "false"; }
		std::string Normalize(std::uint64_t value) { return std::to_string(value); }
		std::string Normalize(std::int64_t value) { return std::to_string(value); }

		// ── yaml-cpp 쪽 ───────────────────────────────────────────────────────
		template<class T>
		bool ReadYamlCpp(const std::string& document, std::string& out)
		{
			try
			{
				const MetaYml::Node root = MetaYml::Load(document);
				const MetaYml::Node value = root["v"];
				if (!value) return false;
				out = Normalize(value.as<T>());
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		template<>
		bool ReadYamlCpp<std::string>(const std::string& document, std::string& out)
		{
			try
			{
				const MetaYml::Node root = MetaYml::Load(document);
				const MetaYml::Node value = root["v"];
				if (!value) return false;
				out = value.as<std::string>();
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		// ── ryml 쪽 ───────────────────────────────────────────────────────────
		//
		// ★ 없는 키를 `operator[]`로 만지면 ryml은 visit 채널로 죽는다. 그래서
		//   반드시 `has_child`를 먼저 본다 — 정책이 예외로 바꿔 주긴 하지만,
		//   "없는 키는 예외가 아니라 부재"라는 yaml-cpp 의미를 맞춰야 한다.
		bool RymlScalar(const std::string& document, ryml::csubstr& out, ryml::Tree& tree)
		{
			EnsureRymlErrorPolicy();
			try
			{
				tree = ryml::parse_in_arena(ryml::to_csubstr(document));
				const ryml::ConstNodeRef root = tree.rootref();
				if (!root.is_map() || !root.has_child("v")) return false;
				const ryml::ConstNodeRef value = root["v"];
				if (!value.has_val()) return false;
				out = value.val();
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		template<class T>
		bool ReadRyml(const std::string& document, std::string& out)
		{
			ryml::Tree tree;
			ryml::csubstr scalar;
			if (!RymlScalar(document, scalar, tree)) return false;

			T value{};
			if (!ryml::from_chars(scalar, &value)) return false;
			out = Normalize(value);
			return true;
		}

		template<>
		bool ReadRyml<std::string>(const std::string& document, std::string& out)
		{
			ryml::Tree tree;
			ryml::csubstr scalar;
			if (!RymlScalar(document, scalar, tree)) return false;
			out.assign(scalar.str, scalar.len);
			return true;
		}

		void Add(ScalarParityResult& result, const char* name, const char* type,
			const std::string& document,
			bool (*yamlCpp)(const std::string&, std::string&),
			bool (*ryml)(const std::string&, std::string&))
		{
			ScalarParityCase entry;
			entry.name = name;
			entry.type = type;
			entry.document = document;
			entry.yamlCppOk = yamlCpp(document, entry.yamlCppValue);
			entry.rymlOk = ryml(document, entry.rymlValue);
			entry.agrees = (entry.yamlCppOk == entry.rymlOk)
				&& (!entry.yamlCppOk || entry.yamlCppValue == entry.rymlValue);
			if (entry.agrees) ++result.agreeCount; else ++result.divergeCount;
			result.cases.push_back(std::move(entry));
		}
	}

	ScalarParityResult ProbeScalarConversions()
	{
		ScalarParityResult result;

		const auto addString = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "string", doc, &ReadYamlCpp<std::string>, &ReadRyml<std::string>);
		};
		const auto addFloat = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "float", doc, &ReadYamlCpp<float>, &ReadRyml<float>);
		};
		const auto addBool = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "bool", doc, &ReadYamlCpp<bool>, &ReadRyml<bool>);
		};
		const auto addU64 = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "u64", doc, &ReadYamlCpp<std::uint64_t>, &ReadRyml<std::uint64_t>);
		};
		const auto addI64 = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "i64", doc, &ReadYamlCpp<std::int64_t>, &ReadRyml<std::int64_t>);
		};

		// ── string ───────────────────────────────────────────────────────────
		// 이 저장소가 실제로 저작하는 형태 + 파서가 갈릴 만한 형태.
		addString("str-plain",      "v: hello\n");
		addString("str-quoted",     "v: \"hello\"\n");
		addString("str-single",     "v: 'hello'\n");
		addString("str-empty",      "v: \"\"\n");
		addString("str-escape-nl",  "v: \"a\\nb\"\n");
		addString("str-escape-tab", "v: \"a\\tb\"\n");
		addString("str-escape-q",   "v: \"a\\\"b\"\n");
		addString("str-path",       "v: Assets/Models/Prim_Cube.glb\n");
		addString("str-guid",       "v: 1c936ecf-a6cc-4ce4-ba79-b98b2c738bb5\n");
		addString("str-spaces",     "v: hello world\n");
		addString("str-numeric",    "v: \"123\"\n");
		addString("str-utf8",       "v: \"\xed\x95\x9c\xea\xb8\x80\"\n");

		// ── float ────────────────────────────────────────────────────────────
		addFloat("f-int",       "v: 1\n");
		addFloat("f-decimal",   "v: 1.0\n");
		addFloat("f-negative",  "v: -0.5\n");
		addFloat("f-exp",       "v: 1e-5\n");
		addFloat("f-exp-cap",   "v: 1E5\n");
		addFloat("f-leading",   "v: .5\n");
		addFloat("f-zero",      "v: 0\n");
		addFloat("f-inf",       "v: .inf\n");
		addFloat("f-neg-inf",   "v: -.inf\n");
		addFloat("f-nan",       "v: .nan\n");
		addFloat("f-quoted",    "v: \"1.5\"\n");

		// ── bool ─────────────────────────────────────────────────────────────
		// ★ 가장 갈릴 만한 축이다. yaml-cpp는 YAML 1.1의 넓은 규약을 쓴다.
		addBool("b-true",   "v: true\n");
		addBool("b-false",  "v: false\n");
		addBool("b-True",   "v: True\n");
		addBool("b-TRUE",   "v: TRUE\n");
		addBool("b-yes",    "v: yes\n");
		addBool("b-no",     "v: no\n");
		addBool("b-on",     "v: on\n");
		addBool("b-off",    "v: off\n");
		addBool("b-one",    "v: 1\n");
		addBool("b-zero",   "v: 0\n");

		// ── 정수 ─────────────────────────────────────────────────────────────
		addU64("u-zero",      "v: 0\n");
		addU64("u-small",     "v: 42\n");
		addU64("u-32bit",     "v: 4294967296\n");
		addU64("u-max",       "v: 18446744073709551615\n");
		addI64("i-negative",  "v: -1\n");
		addI64("i-min",       "v: -9223372036854775808\n");
		addU64("u-hex",       "v: 0x10\n");
		addU64("u-octal",     "v: 0o10\n");

		// ── 부재 키 ──────────────────────────────────────────────────────────
		// yaml-cpp는 `if (!sub) return;`로 조용히 넘긴다(레거시 파리티). ryml에서
		// 없는 키를 `operator[]`로 만지면 visit 채널로 죽으므로 `has_child`가 필수다.
		// 두 쪽 모두 "실패"로 나와야 의미가 같다.
		addString("missing-key", "other: 1\n");
		addFloat("null-tilde",   "v: ~\n");
		addString("null-tilde-s", "v: ~\n");

		return result;
	}
}
