#pragma once
// reflgen 직렬화의 Authoring 백엔드 (reflgen 도입 P5) — reflgen::writer·reader 를 저작 트리(ryml) 위에 둔다.
//
// 엔진 YAML 형식 계약(ReflectionTypedYml.h 머리말)을 백엔드가 지키는 몫:
//   - 빈 시퀀스·빈 맵은 `~`(begin_array(0)·begin_object(0)). 반영 객체 봉투는 BeginRecord 로 연다 — 비어도 맵이다.
//   - 원소가 모두 스칼라이거나 한 줄 컨테이너인 시퀀스는 flow 다. null 원소나 여러 줄 컨테이너(객체)가 하나라도
//     있으면 block 이다(레거시: 스칼라 원소 시퀀스만 flow).
//   - prefer_inline() 뒤의 컨테이너는 flow(math 벡터·색·사각형·행렬).
//   - 산술 값은 C++ 타입 그대로 ryml 이 적는다 — float 은 write_float32 로 와서 float 의 표기가 된다.
//   - 읽기에서 `~` 인 시퀀스·맵은 빈 것이다.
// 나머지 계약(헤더·훅·enum·포인터·키 표기)은 serializer 특수화(ReflgenAuthoringSerializers.h)의 몫이다.
#include "AuthoringBase64.h"
#include "AuthoringReadNode.h"
#include "AuthoringScalarConvert.h"
#include "AuthoringWriteNode.h"

#include <reflgen/serial/error.h>
#include <reflgen/serial/reader.h>
#include <reflgen/serial/writer.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Authoring
{
	class ReflgenWriter final : public reflgen::writer
	{
	public:
		// root 가 첫 값(보통 객체 하나)을 받는다.
		explicit ReflgenWriter(WriteNode root) : m_root(root) {}

		// 반영 객체 봉투 — 비어도 `~` 가 아니라 맵으로 연다(레거시 SerializeObjectInto 는 SetMap 으로 시작했다).
		void BeginRecord(std::size_t size)
		{
			m_record = true;
			begin_object(size);
		}

		// 지금 열린 컨테이너의 노드 — 훅(OnAfterSerialize)처럼 트리에 직접 키를 더하는 코드가 쓴다.
		[[nodiscard]] WriteNode CurrentNode() const
		{
			return m_frames.empty() ? m_root : m_frames.back().node;
		}

		// 다음 값 자리의 노드를 만들어 준다 — 그 값을 이 writer 밖(런타임 타입 디스패치)에서 채울 때.
		// 그 노드는 여러 줄 값으로 본다(시퀀스를 flow 로 접지 않는다).
		[[nodiscard]] WriteNode TakeValueNode()
		{
			return Next(ChildKind::block);
		}

		void write_null() override { Next(ChildKind::null).SetNull(); }
		void write_bool(bool value) override { Next(ChildKind::scalar).SetScalar(value); }
		void write_int(std::int64_t value) override { Next(ChildKind::scalar).SetScalar(value); }
		void write_uint(std::uint64_t value) override { Next(ChildKind::scalar).SetScalar(value); }
		void write_float(double value) override { Next(ChildKind::scalar).SetScalar(value); }
		void write_float32(float value) override { Next(ChildKind::scalar).SetScalar(value); }
		void write_string(std::string_view value) override { Next(ChildKind::scalar).SetScalar(value); }

		void write_bytes(std::span<const std::byte> value) override
		{
			Next(ChildKind::scalar).SetScalar(
				Base64::Encode(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
		}

		void prefer_inline() override { m_inlineNext = true; }

		void begin_array(std::size_t size) override { Open(size, false); }
		void end_array() override { Close(false); }
		void begin_object(std::size_t size) override { Open(size, true); }

		void write_key(std::string_view key) override
		{
			if (m_frames.empty() || !m_frames.back().isMap || m_frames.back().hasKey)
			{
				throw reflgen::serialization_error("authoring writer: write_key is only valid inside an object");
			}
			m_frames.back().key.assign(key);
			m_frames.back().hasKey = true;
		}

		void end_object() override { Close(true); }

	private:
		enum class ChildKind : unsigned char { scalar, null, inlineContainer, block };

		struct Frame
		{
			WriteNode node;
			bool isMap = false;
			bool isNull = false;   // 빈 컨테이너 — `~` 로 적었고 원소를 받지 않는다
			bool isInline = false; // 이 컨테이너가 flow 다
			bool flowCandidate = true; // 시퀀스: 지금까지의 원소가 모두 스칼라·한 줄 컨테이너다
			bool hasKey = false;
			std::string key;
		};

		// 다음 값의 자리 — 루트, 열린 맵의 key 자리, 열린 시퀀스의 뒤.
		WriteNode Next(ChildKind kind)
		{
			if (m_frames.empty())
			{
				if (m_rootUsed)
				{
					throw reflgen::serialization_error("authoring writer: a document holds exactly one root value");
				}
				m_rootUsed = true;
				return m_root;
			}
			Frame& top = m_frames.back();
			if (top.isNull)
			{
				throw reflgen::serialization_error("authoring writer: a value inside a container declared empty");
			}
			if (top.isMap)
			{
				if (!top.hasKey)
				{
					throw reflgen::serialization_error("authoring writer: write_key must precede each value");
				}
				top.hasKey = false;
				return top.node.Child(top.key);
			}
			if (kind == ChildKind::null || kind == ChildKind::block)
			{
				top.flowCandidate = false;
			}
			return top.node.Append();
		}

		void Open(std::size_t size, bool isMap)
		{
			const bool record = m_record;
			const bool inlineContainer = m_inlineNext || (!m_frames.empty() && m_frames.back().isInline);
			m_record = false;
			m_inlineNext = false;

			const WriteNode node = Next(inlineContainer ? ChildKind::inlineContainer : ChildKind::block);
			Frame frame{ node, isMap };
			frame.isInline = inlineContainer;
			if (0 == size && !record)
			{
				// 빈 시퀀스·빈 맵은 `~` — 모든 컨테이너에 같게(레거시 계약).
				node.SetNull();
				frame.isNull = true;
			}
			else if (isMap)
			{
				node.SetMap(inlineContainer);
			}
			else
			{
				node.SetSequence(inlineContainer);
			}
			m_frames.push_back(std::move(frame));
		}

		void Close(bool isMap)
		{
			if (m_frames.empty() || m_frames.back().isMap != isMap)
			{
				throw reflgen::serialization_error("authoring writer: unbalanced end_array/end_object");
			}
			const Frame frame = std::move(m_frames.back());
			m_frames.pop_back();
			if (!isMap && !frame.isNull && !frame.isInline && frame.flowCandidate && frame.node.Size() > 0)
			{
				frame.node.SetFlow(); // 스칼라 원소 시퀀스만 flow(레거시 EmitSequenceElement)
			}
		}

		WriteNode m_root;
		bool m_rootUsed = false;
		bool m_record = false;
		bool m_inlineNext = false;
		std::vector<Frame> m_frames;
	};

	class ReflgenReader final : public reflgen::reader
	{
	public:
		explicit ReflgenReader(ReadNode root) : m_root(root) {}

		// 열린 객체에서 key 의 값 노드 — 없으면 유효하지 않은 노드. 봉투가 필드마다 먼저 보고 판단할 때 쓴다.
		[[nodiscard]] ReadNode ValueOf(std::string_view key) const
		{
			if (m_frames.empty() || !m_frames.back().node.IsMap()) return {};
			return m_frames.back().node[std::string(key).c_str()];
		}

		// 다음에 읽을 값의 노드(소비하지 않는다) — 봉투가 모양을 보고 레거시처럼 건너뛸지 정할 때.
		[[nodiscard]] ReadNode PeekNode() const { return Current(); }

		// 다음에 읽을 값을 열린 객체의 key 자리로 옮긴다 — 봉투가 필드 순서대로 이름으로 찾아 읽을 때(레거시 순서).
		bool Seek(std::string_view key)
		{
			const ReadNode value = ValueOf(key);
			if (!value) return false;
			Frame& top = m_frames.back();
			top.current = value;
			top.hasCurrent = true;
			return true;
		}

		reflgen::value_kind peek() override
		{
			const ReadNode node = Current();
			if (node.IsNull()) return reflgen::value_kind::null;
			if (node.IsMap()) return reflgen::value_kind::object;
			if (node.IsSequence()) return reflgen::value_kind::array;
			const std::string_view text = node.Scalar();
			if (text == "true" || text == "false") return reflgen::value_kind::boolean;
			std::int64_t integer{};
			if (Authoring::Scalar::TryConvert(text, integer)) return reflgen::value_kind::integer;
			double floating{};
			if (Authoring::Scalar::TryConvert(text, floating)) return reflgen::value_kind::floating;
			return reflgen::value_kind::string;
		}

		void read_null() override
		{
			if (!Take().IsNull()) Fail("expected null");
		}

		bool read_bool() override { return Convert<bool>("a bool"); }
		std::int64_t read_int() override { return Convert<std::int64_t>("an integer"); }
		std::uint64_t read_uint() override { return Convert<std::uint64_t>("an unsigned integer"); }
		double read_float() override { return Convert<double>("a number"); }
		float read_float32() override { return Convert<float>("a float"); }

		std::string read_string() override
		{
			// 레거시 StringOf 와 같다 — 널 노드는 문자열 "null" 이다(ReadNode::AsStringChecked 가 소유하는 규칙).
			const ReadNode node = Take();
			if (!node.IsScalar() && !node.IsNull()) Fail("expected a string");
			return node.AsStringChecked();
		}

		std::vector<std::byte> read_bytes() override
		{
			std::vector<std::uint8_t> decoded;
			if (!Base64::Decode(read_string(), decoded)) Fail("invalid base64");
			std::vector<std::byte> bytes(decoded.size());
			for (std::size_t i = 0; i < decoded.size(); ++i) bytes[i] = static_cast<std::byte>(decoded[i]);
			return bytes;
		}

		std::optional<std::size_t> begin_array() override { return Open(false); }
		bool next_element() override { return Advance(false); }
		void end_array() override { Close(false); }

		std::optional<std::size_t> begin_object() override { return Open(true); }

		bool next_key(std::string& key) override
		{
			if (!Advance(true)) return false;
			key = m_frames.back().key;
			return true;
		}

		void end_object() override { Close(true); }

		void skip_value() override { (void)Take(); }

	private:
		struct Frame
		{
			ReadNode node;
			bool isMap = false;
			std::size_t index = 0;
			ReadNode current;
			bool hasCurrent = false;
			std::string key;
		};

		[[noreturn]] static void Fail(const char* what)
		{
			throw reflgen::serialization_error(std::string("authoring reader: ") + what);
		}

		[[nodiscard]] ReadNode Current() const
		{
			if (m_frames.empty())
			{
				if (m_rootTaken) Fail("the root value was already read");
				return m_root;
			}
			if (!m_frames.back().hasCurrent) Fail("no value to read — call next_element/next_key first");
			return m_frames.back().current;
		}

		ReadNode Take()
		{
			const ReadNode node = Current();
			if (m_frames.empty())
			{
				m_rootTaken = true;
			}
			else
			{
				m_frames.back().hasCurrent = false;
			}
			return node;
		}

		template<class T>
		T Convert(const char* what)
		{
			const ReadNode node = Take();
			T value{};
			if (!node.IsScalar() || !Authoring::Scalar::TryConvert(node.Scalar(), value))
			{
				Fail((std::string("expected ") + what).c_str());
			}
			return value;
		}

		std::optional<std::size_t> Open(bool isMap)
		{
			const ReadNode node = Take();
			Frame frame{ node, isMap };
			if (node.IsNull())
			{
				// `~` 는 빈 시퀀스·맵이다(쓰기 쪽 계약의 짝).
				m_frames.push_back(std::move(frame));
				return 0u;
			}
			if (isMap ? !node.IsMap() : !node.IsSequence())
			{
				Fail(isMap ? "expected a map" : "expected a sequence");
			}
			const std::size_t size = node.Size();
			m_frames.push_back(std::move(frame));
			return size;
		}

		bool Advance(bool isMap)
		{
			if (m_frames.empty() || m_frames.back().isMap != isMap) Fail("unbalanced next_element/next_key");
			Frame& top = m_frames.back();
			if (top.node.IsNull() || top.index >= top.node.Size()) return false;
			if (isMap)
			{
				std::size_t position = 0;
				for (const auto entry : top.node.Map())
				{
					if (position++ != top.index) continue;
					top.key = entry.key.AsStringChecked();
					top.current = entry.value;
					break;
				}
			}
			else
			{
				top.current = top.node.At(top.index);
			}
			top.hasCurrent = true;
			++top.index;
			return true;
		}

		void Close(bool isMap)
		{
			if (m_frames.empty() || m_frames.back().isMap != isMap) Fail("unbalanced end_array/end_object");
			m_frames.pop_back();
		}

		ReadNode m_root;
		bool m_rootTaken = false;
		std::vector<Frame> m_frames;
	};
}
