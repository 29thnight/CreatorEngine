#include "AuthoringScalarParityProbe.h"

#include "AuthoringRymlErrorPolicy.h"
#include "AuthoringScalarConvert.h"
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

		// ── 이식 변환기 쪽 ───────────────────────────────────────────────────
		//
		// 파싱은 yaml-cpp로 하고 **변환만** 이식 함수로 한다. 이 축이 재려는 것은
		// 파서가 아니라 변환이므로, 파서를 고정해야 차이의 원인이 하나로 좁혀진다.
		bool RawScalar(const std::string& document, std::string& out)
		{
			try
			{
				const MetaYml::Node root = MetaYml::Load(document);
				const MetaYml::Node value = root["v"];
				if (!value) return false;
				// ★ **널 노드는 문자열 "null"이다.** yaml-cpp `as<std::string>`의
				//   실측 동작이고(`v: ~`·`v: null` 둘 다), 이것을 빠뜨리면 변환기가
				//   yaml-cpp와 갈린다 — 게이트가 잡았다. 이 규칙은 변환이 아니라
				//   **노드→원문 추출**의 규칙이므로, D3-b-2b-1b가 ryml 쪽에서도
				//   같은 규칙을 세워야 한다(`val_is_null()`).
				if (value.IsNull()) { out = "null"; return true; }
				if (!value.IsScalar()) return false;
				out = value.Scalar();
				return true;
			}
			catch (const std::exception&)
			{
				return false;
			}
		}

		bool ConvertString(const std::string& document, std::string& out)
		{
			// 문자열은 변환이 없다 — 스칼라 원문이 곧 값이다. 다만 yaml-cpp는
			// `~`를 "null"로 정규화하므로 그 차이가 여기서 드러난다.
			return RawScalar(document, out);
		}

		bool ConvertBool(const std::string& document, std::string& out)
		{
			std::string raw;
			if (!RawScalar(document, raw)) return false;
			bool value{};
			if (!Authoring::Scalar::TryParseBool(raw, value)) return false;
			out = Normalize(value);
			return true;
		}

		bool ConvertFloat(const std::string& document, std::string& out)
		{
			std::string raw;
			if (!RawScalar(document, raw)) return false;
			float value{};
			if (!Authoring::Scalar::TryParseFloat(raw, value)) return false;
			out = Normalize(value);
			return true;
		}

		bool ConvertU64(const std::string& document, std::string& out)
		{
			std::string raw;
			if (!RawScalar(document, raw)) return false;
			std::uint64_t value{};
			if (!Authoring::Scalar::TryParseUInt64(raw, value)) return false;
			out = Normalize(value);
			return true;
		}

		bool ConvertI64(const std::string& document, std::string& out)
		{
			std::string raw;
			if (!RawScalar(document, raw)) return false;
			std::int64_t value{};
			if (!Authoring::Scalar::TryParseInt64(raw, value)) return false;
			out = Normalize(value);
			return true;
		}

		void Add(ScalarParityResult& result, const char* name, const char* type,
			const std::string& document,
			bool (*yamlCpp)(const std::string&, std::string&),
			bool (*ryml)(const std::string&, std::string&),
			bool (*converter)(const std::string&, std::string&))
		{
			ScalarParityCase entry;
			entry.name = name;
			entry.type = type;
			entry.document = document;
			entry.yamlCppOk = yamlCpp(document, entry.yamlCppValue);
			entry.rymlOk = ryml(document, entry.rymlValue);
			entry.converterOk = converter(document, entry.converterValue);
			entry.agrees = (entry.yamlCppOk == entry.rymlOk)
				&& (!entry.yamlCppOk || entry.yamlCppValue == entry.rymlValue);
			entry.converterAgrees = (entry.yamlCppOk == entry.converterOk)
				&& (!entry.yamlCppOk || entry.yamlCppValue == entry.converterValue);
			if (entry.agrees) ++result.agreeCount; else ++result.divergeCount;
			if (!entry.converterAgrees) ++result.converterDivergeCount;
			result.cases.push_back(std::move(entry));
		}
	}

	ScalarParityResult ProbeScalarConversions()
	{
		ScalarParityResult result;

		const auto addString = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "string", doc, &ReadYamlCpp<std::string>, &ReadRyml<std::string>, &ConvertString);
		};
		const auto addFloat = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "float", doc, &ReadYamlCpp<float>, &ReadRyml<float>, &ConvertFloat);
		};
		const auto addBool = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "bool", doc, &ReadYamlCpp<bool>, &ReadRyml<bool>, &ConvertBool);
		};
		const auto addU64 = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "u64", doc, &ReadYamlCpp<std::uint64_t>, &ReadRyml<std::uint64_t>, &ConvertU64);
		};
		const auto addI64 = [&result](const char* name, const std::string& doc)
		{
			Add(result, name, "i64", doc, &ReadYamlCpp<std::int64_t>, &ReadRyml<std::int64_t>, &ConvertI64);
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

		// ── 진법·부호·부분 파싱·오버플로 ─────────────────────────────────────
		//
		// 변환기를 **이식**했다고 말하려면 정상 표기뿐 아니라 이 경계들이 같아야
		// 한다. 특히 부분 파싱("12abc")을 성공으로 읽으면 저작 오타가 조용히
		// 숫자가 된다.
		addU64("u-hex-upper",   "v: 0X1F\n");
		addU64("u-octal-lead0", "v: 010\n");
		addU64("u-plus",        "v: +42\n");
		addU64("u-negative",    "v: -1\n");
		addU64("u-partial",     "v: 12abc\n");
		addU64("u-empty-quote", "v: \"\"\n");
		addU64("u-space",       "v: \" 42 \"\n");
		addU64("u-overflow",    "v: 99999999999999999999999\n");
		addI64("i-hex",         "v: 0x1F\n");
		addI64("i-plus",        "v: +7\n");
		addI64("i-partial",     "v: 7x\n");
		addFloat("f-plus",      "v: +1.5\n");
		addFloat("f-partial",   "v: 1.5x\n");
		addFloat("f-inf-caps",  "v: .Inf\n");
		addFloat("f-nan-caps",  "v: .NaN\n");
		addFloat("f-huge",      "v: 1e400\n");
		addFloat("f-hex",       "v: 0x10\n");
		addBool("b-yEs-mixed",  "v: yEs\n");
		addBool("b-y",          "v: y\n");
		addBool("b-n",          "v: n\n");
		addBool("b-empty",      "v: \"\"\n");
		addString("str-tilde-quoted", "v: \"~\"\n");
		addString("str-null-word",    "v: null\n");

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
