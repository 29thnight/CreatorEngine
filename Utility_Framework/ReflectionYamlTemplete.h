#pragma once
#include "ReflectionFunction.h"
#include "ReflectionRegister.h"
// (죽은 include 제거 이력: IObject.h·DataSystem.h — 본문에서 아무것도 쓰지
//  않으면서 코어→상위층 간선 2개를 만들고 있었다. L1-2에서 제거.)
#include <yaml-cpp/yaml.h>
#include <unordered_map>

namespace MetaYml = YAML;

namespace Meta
{
	inline constexpr const char GAMEOBJECT_YAML_KEY[] = "GameObject";
    inline constexpr const char COMPONENT_YAML_KEY[] = "Component";
    // -----------------------------
    // 1) Scalar Property <-> YAML
    // -----------------------------

    // �⺻ ���ø�: �״�� as<T>, any_cast<T>
    template <typename T>
    inline void ToYamlScalar(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        node[prop.name] = std::any_cast<T>(value);
    }

    template <typename T>
    inline void FromYamlScalar(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        prop.setter(instance, node[prop.name].as<T>());
    }

    // ---- HashingString ----
    template <>
    inline void ToYamlScalar<HashingString>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        node[prop.name] = std::any_cast<HashingString>(value).ToString();
    }

    template <>
    inline void FromYamlScalar<HashingString>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        prop.setter(instance, HashingString(node[prop.name].as<std::string>()));
    }

    // ---- HashedGuid ----
    template <>
    inline void ToYamlScalar<HashedGuid>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        node[prop.name] = std::any_cast<HashedGuid>(value).m_ID_Data;
    }

    template <>
    inline void FromYamlScalar<HashedGuid>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        // ���� �ڵ�� �����ϰ� uint32_t�� ������ȭ
        prop.setter(instance, HashedGuid(node[prop.name].as<size_t>())); // CT6-a: uint32 절단 수정 — FNV64 값이 BadConversion 나던 잠재 버그
    }

    // ---- file::path ----
    template <>
    inline void ToYamlScalar<file::path>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        node[prop.name] = std::any_cast<file::path>(value).string();
    }

    template <>
    inline void FromYamlScalar<file::path>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        prop.setter(instance, file::path(node[prop.name].as<std::string>()));
    }

    // ---- Mathf::Vector2 ----
    template <>
    inline void ToYamlScalar<Mathf::Vector2>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto vec = std::any_cast<Mathf::Vector2>(value);
        MetaYml::Node vecNode;
        vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
        vecNode["x"] = vec.x;
        vecNode["y"] = vec.y;
        node[prop.name] = vecNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Vector2>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node vecNode = node[prop.name];
        Mathf::Vector2 vec;
        vec.x = vecNode["x"].as<float>();
        vec.y = vecNode["y"].as<float>();
        prop.setter(instance, vec);
    }

    // ---- Mathf::Vector3 ----
    template <>
    inline void ToYamlScalar<Mathf::Vector3>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto vec = std::any_cast<Mathf::Vector3>(value);
        MetaYml::Node vecNode;
        vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
        vecNode["x"] = vec.x;
        vecNode["y"] = vec.y;
        vecNode["z"] = vec.z;
        node[prop.name] = vecNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Vector3>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node vecNode = node[prop.name];
        Mathf::Vector3 vec;
        vec.x = vecNode["x"].as<float>();
        vec.y = vecNode["y"].as<float>();
        vec.z = vecNode["z"].as<float>();
        prop.setter(instance, vec);
    }

    // ---- Mathf::Color4 ----
    template <>
    inline void ToYamlScalar<Mathf::Color4>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto color = std::any_cast<Mathf::Color4>(value);
        MetaYml::Node vecNode;
        vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
        vecNode["r"] = color.x;
        vecNode["g"] = color.y;
        vecNode["b"] = color.z;
        vecNode["a"] = color.w;
        node[prop.name] = vecNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Color4>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node vecNode = node[prop.name];
        Mathf::Color4 color;
        color.x = vecNode["r"].as<float>();
        color.y = vecNode["g"].as<float>();
        color.z = vecNode["b"].as<float>();
        color.w = vecNode["a"].as<float>();
        prop.setter(instance, color);
    }

    // ---- Mathf::Vector4 ----
    template <>
    inline void ToYamlScalar<Mathf::Vector4>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto vec = std::any_cast<Mathf::Vector4>(value);
        MetaYml::Node vecNode;
        vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
        vecNode["x"] = vec.x;
        vecNode["y"] = vec.y;
        vecNode["z"] = vec.z;
        vecNode["w"] = vec.w;
        node[prop.name] = vecNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Vector4>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node vecNode = node[prop.name];
        Mathf::Vector4 vec;
        vec.x = vecNode["x"].as<float>();
        vec.y = vecNode["y"].as<float>();
        vec.z = vecNode["z"].as<float>();
        vec.w = vecNode["w"].as<float>();
        prop.setter(instance, vec);
    }

    // ---- Mathf::Quaternion ----
    template <>
    inline void ToYamlScalar<Mathf::Quaternion>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto quat = std::any_cast<Mathf::Quaternion>(value);
        MetaYml::Node vecNode;
        vecNode.SetStyle(MetaYml::EmitterStyle::Flow);
        vecNode["x"] = quat.x;
        vecNode["y"] = quat.y;
        vecNode["z"] = quat.z;
        vecNode["w"] = quat.w;
        node[prop.name] = vecNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Quaternion>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node vecNode = node[prop.name];
        Mathf::Quaternion quat;
        quat.x = vecNode["x"].as<float>();
        quat.y = vecNode["y"].as<float>();
        quat.z = vecNode["z"].as<float>();
        quat.w = vecNode["w"].as<float>();
        prop.setter(instance, quat);
    }

    // ---- Mathf::Rect ----
    template <>
    inline void ToYamlScalar<Mathf::Rect>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        auto rect = std::any_cast<Mathf::Rect>(value);
        MetaYml::Node rectNode;
        rectNode.SetStyle(MetaYml::EmitterStyle::Flow);
        rectNode["x"] = rect.x;
        rectNode["y"] = rect.y;
        rectNode["width"] = rect.width;
        rectNode["height"] = rect.height;
        node[prop.name] = rectNode;
    }

    template <>
    inline void FromYamlScalar<Mathf::Rect>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        MetaYml::Node rectNode = node[prop.name];
        Mathf::Rect rect;
        rect.x = rectNode["x"].as<float>();
        rect.y = rectNode["y"].as<float>();
        rect.width = rectNode["width"].as<float>();
        rect.height = rectNode["height"].as<float>();
        prop.setter(instance, rect);
    }

    // ---- FileGuid ----
    template <>
    inline void ToYamlScalar<FileGuid>(const Meta::Property& prop, MetaYml::Node& node, std::any& value)
    {
        node[prop.name] = std::any_cast<FileGuid>(value).ToString();
    }

    template <>
    inline void FromYamlScalar<FileGuid>(const Meta::Property& prop, void* instance, const MetaYml::Node& node)
    {
        prop.setter(instance, FileGuid(node[prop.name].as<std::string>()));
    }

} // namespace Meta

namespace Meta
{
    // --------------------------------------
    // 2) Vector<T> <-> YAML sequence ����
    // --------------------------------------

    // 1) �⺻ ���ø�: T �� ��ü�� ����ȭ/������ȭ
    template <typename T>
    inline void VectorElementToYaml(MetaYml::Node& arrayNode, void* elementVoid)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        T* elem = static_cast<T*>(elementVoid);
        arrayNode.push_back(*elem);
    }

    template <typename T>
    inline void VectorFromYamlScalar(void* instance, size_t offset, const MetaYml::Node& node)
    {
        auto vecYaml = node.as<std::vector<T>>();
        auto* vec = reinterpret_cast<std::vector<T>*>(reinterpret_cast<char*>(instance) + offset);
        vec->clear();
        vec->assign(vecYaml.begin(), vecYaml.end());
    }

    // --------------------------------------------------
    // 2) file::path vector Ư��ȭ
    //  - YAML: std::vector<std::string> �� ����
    // --------------------------------------------------
    template <>
    inline void VectorElementToYaml<file::path>(MetaYml::Node& arrayNode, void* elementVoid)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        auto* p = static_cast<file::path*>(elementVoid);
        arrayNode.push_back(p->string());
    }

    template <>
    inline void VectorFromYamlScalar<file::path>(void* instance, size_t offset, const MetaYml::Node& node)
    {
        auto vecYaml = node.as<std::vector<std::string>>();
        auto* vec = reinterpret_cast<std::vector<file::path>*>(reinterpret_cast<char*>(instance) + offset);
        vec->clear();
        for (auto& s : vecYaml)
            vec->emplace_back(s);
    }

    // --------------------------------------------------
    // 3) HashingString vector Ư��ȭ
    //  - YAML: std::vector<std::string> �� ����
    // --------------------------------------------------
    template <>
    inline void VectorElementToYaml<HashingString>(MetaYml::Node& arrayNode, void* elementVoid)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        auto* p = static_cast<HashingString*>(elementVoid);
        arrayNode.push_back(p->ToString());
    }

    template <>
    inline void VectorFromYamlScalar<HashingString>(void* instance, size_t offset, const MetaYml::Node& node)
    {
        auto vecYaml = node.as<std::vector<std::string>>();
        auto* vec = reinterpret_cast<std::vector<HashingString>*>(reinterpret_cast<char*>(instance) + offset);
        vec->clear();
        for (auto& s : vecYaml)
            vec->emplace_back(s);
    }

    // --------------------------------------------------
    // 4) HashedGuid vector Ư��ȭ
    //  - YAML: std::vector<uint32_t> �� ���� (���� ���� HashedGuid�� ���� ��Ģ)
    // --------------------------------------------------
    template <>
    inline void VectorElementToYaml<HashedGuid>(MetaYml::Node& arrayNode, void* elementVoid)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        auto* p = static_cast<HashedGuid*>(elementVoid);
        arrayNode.push_back(p->m_ID_Data);
    }

    template <>
    inline void VectorFromYamlScalar<HashedGuid>(void* instance, size_t offset, const MetaYml::Node& node)
    {
        auto vecYaml = node.as<std::vector<size_t>>(); // CT6-a: uint32 truncation fix
        auto* vec = reinterpret_cast<std::vector<HashedGuid>*>(reinterpret_cast<char*>(instance) + offset);
        vec->clear();
        for (auto& v : vecYaml)
            vec->emplace_back(v);
    }

    // --------------------------------------------------
    // 5) FileGuid vector Ư��ȭ
    //  - YAML: std::vector<std::string> (ToString()) �� ����
    // --------------------------------------------------
    template <>
    inline void VectorElementToYaml<FileGuid>(MetaYml::Node& arrayNode, void* elementVoid)
    {
        arrayNode.SetStyle(MetaYml::EmitterStyle::Flow);
        auto* p = static_cast<FileGuid*>(elementVoid);
        arrayNode.push_back(p->ToString());
    }

    template <>
    inline void VectorFromYamlScalar<FileGuid>(void* instance, size_t offset, const MetaYml::Node& node)
    {
        auto vecYaml = node.as<std::vector<std::string>>();
        auto* vec = reinterpret_cast<std::vector<FileGuid>*>(reinterpret_cast<char*>(instance) + offset);
        vec->clear();
        for (auto& s : vecYaml)
            vec->emplace_back(s);
    }

} // namespace Meta


namespace Meta
{

    struct YamlSerializerEntry
    {
        HashedGuid typeID;
        void (*toYaml)(const Meta::Property&, MetaYml::Node&, std::any&);
        void (*fromYaml)(const Meta::Property&, void*, const MetaYml::Node&);
    };

    struct YamlVectorEntry
    {
        HashedGuid elementTypeID;
        void (*toYamlElement)(MetaYml::Node& arrayNode, void* element);
        void (*fromYamlVector)(void* instance, size_t offset, const MetaYml::Node& node);
    };

#define type_serializer(T) { type_guid(T), &ToYamlScalar<T>, &FromYamlScalar<T> }
#define type_vector_serializer(T) { type_guid(T), &VectorElementToYaml<T>, &VectorFromYamlScalar<T> }

    // Ÿ�� ����Ʈ �� ���� ����
    inline static YamlSerializerEntry g_yamlSerializers[] = {
        type_serializer(int),
        type_serializer(float),
        type_serializer(bool),
        type_serializer(int8_t),
        type_serializer(uint8_t),
        type_serializer(int16_t),
        type_serializer(uint16_t),
        type_serializer(int32_t),
        type_serializer(uint32_t),
        type_serializer(int64_t),
        type_serializer(uint64_t),
        type_serializer(double),
        type_serializer(std::string),
        //Ư��ȭ�� Ÿ�Ե�
        type_serializer(file::path),
        type_serializer(HashingString),
        type_serializer(HashedGuid),
        type_serializer(Mathf::Vector2),
        type_serializer(Mathf::Vector3),
        type_serializer(Mathf::Color4),
        type_serializer(Mathf::Vector4),
        type_serializer(Mathf::Quaternion),
        type_serializer(Mathf::Rect),
        type_serializer(FileGuid),
    };

    inline const YamlSerializerEntry* FindYamlSerializer(HashedGuid id)
    {
        // CT1: 배열 선형 스캔 → 해시맵. 스칼라 프로퍼티 하나마다 최대 23항목을
        // 순회하던 경로다. 함수-로컬 매직 스태틱이어야 한다 — 네임스페이스
        // 스코프 맵은 TU마다 동적 초기화가 돌아 SIOF 대상이 된다.
        static const std::unordered_map<size_t, const YamlSerializerEntry*> s_byId = []
        {
            std::unordered_map<size_t, const YamlSerializerEntry*> m;
            m.reserve(std::size(g_yamlSerializers));
            for (const auto& e : g_yamlSerializers)
            {
                m.emplace(e.typeID.m_ID_Data, &e);
            }
            return m;
        }();

        auto it = s_byId.find(id.m_ID_Data);
        return (it != s_byId.end()) ? it->second : nullptr;
    }

    // vector<T> �� ���� ������ Ÿ�Ե� ���� ���⼭ ����
    static inline YamlVectorEntry g_yamlVectorEntries[] = {
		type_vector_serializer(int),
		type_vector_serializer(float),
		type_vector_serializer(bool),
		type_vector_serializer(int8_t),
		type_vector_serializer(uint8_t),
		type_vector_serializer(int16_t),
		type_vector_serializer(uint16_t),
		type_vector_serializer(int32_t),
		type_vector_serializer(uint32_t),
		type_vector_serializer(int64_t),
		type_vector_serializer(uint64_t),
		type_vector_serializer(double),
		type_vector_serializer(std::string),
		//Ư��ȭ�� Ÿ�Ե�
		type_vector_serializer(file::path),
		type_vector_serializer(HashingString),
		type_vector_serializer(HashedGuid),
		type_vector_serializer(FileGuid),
    };

    inline const YamlVectorEntry* FindYamlVectorEntry(HashedGuid elementTypeID)
    {
        // CT1: FindYamlSerializer와 같은 이유의 해시맵 전환.
        static const std::unordered_map<size_t, const YamlVectorEntry*> s_byId = []
        {
            std::unordered_map<size_t, const YamlVectorEntry*> m;
            m.reserve(std::size(g_yamlVectorEntries));
            for (const auto& e : g_yamlVectorEntries)
            {
                m.emplace(e.elementTypeID.m_ID_Data, &e);
            }
            return m;
        }();

        auto it = s_byId.find(elementTypeID.m_ID_Data);
        return (it != s_byId.end()) ? it->second : nullptr;
    }
}