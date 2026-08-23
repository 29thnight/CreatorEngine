#include "Material.h"
#include "Material.h"
#include "DataSystem.h"
#include <cstring>

Material::Material()
{
}

Material::Material(const Material& material) :
    m_name(material.m_name),
    m_pBaseColor(material.m_pBaseColor),
    m_pNormal(material.m_pNormal),
    m_pOccRoughMetal(material.m_pOccRoughMetal),
    m_AOMap(material.m_AOMap),
    m_pEmissive(material.m_pEmissive),
    m_materialInfo(material.m_materialInfo),
    m_fileGuid(material.m_fileGuid),
    m_baseColorTexName(material.m_baseColorTexName),
    m_normalTexName(material.m_normalTexName),
    m_ORM_TexName(material.m_ORM_TexName),
    m_AO_TexName(material.m_AO_TexName),
    m_EmissiveTexName(material.m_EmissiveTexName),
    m_flowInfo(material.m_flowInfo),
    m_renderingMode(material.m_renderingMode),
    m_cbMeta(material.m_cbMeta),
    m_cbufferValues(material.m_cbufferValues),
	m_dirtyCBs(material.m_dirtyCBs)
{
}

Material::Material(Material&& material) noexcept
{
    std::exchange(m_name, material.m_name);
    std::exchange(m_pBaseColor, material.m_pBaseColor);
    std::exchange(m_pNormal, material.m_pNormal);
    std::exchange(m_pOccRoughMetal, material.m_pOccRoughMetal);
    std::exchange(m_AOMap, material.m_AOMap);
    std::exchange(m_pEmissive, material.m_pEmissive);
    std::exchange(m_fileGuid, material.m_fileGuid);
    std::exchange(m_baseColorTexName, material.m_baseColorTexName);
    std::exchange(m_normalTexName, material.m_normalTexName);
    std::exchange(m_ORM_TexName, material.m_ORM_TexName);
    std::exchange(m_AO_TexName, material.m_AO_TexName);
    std::exchange(m_EmissiveTexName, material.m_EmissiveTexName);
    m_materialGuid = std::move(material.m_materialGuid);
    m_renderingMode = std::move(material.m_renderingMode);
    m_materialInfo = std::move(material.m_materialInfo);
    m_flowInfo = std::move(material.m_flowInfo);
    std::exchange(m_cbMeta, material.m_cbMeta);
    m_cbufferValues = std::move(material.m_cbufferValues);
	m_dirtyCBs = std::move(material.m_dirtyCBs);
}

Material::~Material()
{
}

std::shared_ptr<Material> Material::InstantiateShared(const Material* origin, std::string_view newName)
{
	if (!origin)
		return nullptr;

	// Create a new Material instance
	auto cloneMaterial = std::make_shared<Material>(*origin);

	const std::string cloneSuffix = "_Clone";

	// Determine the base name depending on whether a new name was provided
	std::string baseName = newName.empty() ? std::string(origin->m_name) : std::string(newName);

	auto stripCloneSuffix = [&](std::string& name)
	{
		auto pos = name.rfind(cloneSuffix);
		if (pos != std::string::npos)
		{
			auto digitsPos = pos + cloneSuffix.size();
			if (digitsPos == name.size() ||
				std::all_of(name.begin() + digitsPos, name.end(), [](unsigned char c) { return std::isdigit(c); }))
			{
				name.erase(pos);
			}
		}
	};

	// If no name was provided, start with the base name plus the clone suffix
	std::string finalName;
	if (newName.empty())
	{
		stripCloneSuffix(baseName);
		finalName = baseName + cloneSuffix;
	}
	else
	{
		finalName = baseName;
	}

	// Ensure the name is unique and avoid nested clone suffixes
	std::lock_guard<std::mutex> materialCacheGuard(DataSystems->m_materialMutex);
	if (DataSystems->Materials.contains(finalName))
	{
		stripCloneSuffix(baseName);
		finalName = baseName + cloneSuffix;
		int cloneIndex = 0;
		while (DataSystems->Materials.contains(finalName))
		{
			finalName = baseName + cloneSuffix + std::to_string(++cloneIndex);
		}
	}

	cloneMaterial->m_name = finalName;

	// 캐시에도 등록해 에디터·직렬화가 이름으로 찾을 수 있게 한다.
	// 캐시가 정리되더라도 호출자가 반환된 shared_ptr을 보관하는 한 클론은 살아 있다.
	DataSystems->Materials[cloneMaterial->m_name] = cloneMaterial;

	return cloneMaterial;
}

Material& Material::SetBaseColor(Mathf::Color3 color)
{
    m_materialInfo.m_baseColor = { color.x, color.y, color.z, 1.f };

	return *this;
}

Material& Material::SetBaseColor(float r, float g, float b)
{
	m_materialInfo.m_baseColor = { r, g, b, 1.f };

	return *this;
}

Material& Material::SetMetallic(float metallic)
{
	m_materialInfo.m_metallic = metallic;

	return *this;
}

Material& Material::SetRoughness(float roughness)
{
	m_materialInfo.m_roughness = roughness;

	return *this;
}

Material& Material::UseBaseColorMap(Texture* texture)
{
	m_pBaseColor = texture;
	m_materialInfo.m_useBaseColor = true;

	return *this;
}

Material& Material::UseNormalMap(Texture* texture)
{
	m_pNormal = texture;
	m_materialInfo.m_useNormalMap = USE_NORMAL_MAP;

	return *this;
}

Material& Material::UseBumpMap(Texture* texture)
{
	m_pNormal = texture;
	m_materialInfo.m_useNormalMap = USE_BUMP_MAP;
	return *this;
}

Material& Material::UseOccRoughMetalMap(Texture* texture)
{
	m_pOccRoughMetal = texture;
	m_materialInfo.m_useOccRoughMetal = true;

	return *this;
}

Material& Material::UseAOMap(Texture* texture)
{
	m_AOMap = texture;
	m_materialInfo.m_useAOMap = true;

	return *this;
}

Material& Material::UseEmissiveMap(Texture* texture)
{
	m_pEmissive = texture;
	m_materialInfo.m_useEmissive = true;
	
	return *this;
}

Material& Material::ConvertToLinearSpace(bool32 convert)
{
	m_materialInfo.m_convertToLinearSpace = convert;
	
	return *this;
}

Material& Material::SetWindVector(const Mathf::Vector4& windVector)
{
	m_flowInfo.m_windVector = windVector;

	return *this;
}

Material& Material::SetUVScroll(const Mathf::Vector2& uvScroll)
{
	m_flowInfo.m_uvScroll = uvScroll;

	return *this;
}

Material::VarView Material::FindVar(std::string_view cb, std::string_view var) const
{
    VarView out{};
    if (!m_cbMeta) return out;

    auto itCB = m_cbMeta->find(std::string(cb));
    if (itCB == m_cbMeta->end()) return out;

    out.cb = &itCB->second;
    auto& vars = out.cb->variables;
    auto itVar = std::find_if(vars.begin(), vars.end(),
        [&](const MaterialParam::VariableDesc& d) { return d.name == var; });
    if (itVar == vars.end()) { out.cb = nullptr; return {}; }
    out.var = &(*itVar);
    return out;
}

bool Material::SplitQualified(std::string_view q, std::string& outCB, std::string& outVar)
{
    auto dot = q.find('.');
    if (dot == std::string_view::npos) return false;
    outCB = std::string(q.substr(0, dot));
    outVar = std::string(q.substr(dot + 1));
    return (!outCB.empty() && !outVar.empty());
}

bool Material::WriteBytes(const VarView& v, const void* src, size_t size)
{
    if (!v.cb || !v.var) return false;
    if (size > v.var->size) return false;
    auto it = m_cbufferValues.find(v.cb->name);
    if (it == m_cbufferValues.end()) return false;

    auto& bytes = it->second;
    if (v.var->offset + size > bytes.size()) return false;
    std::memcpy(bytes.data() + v.var->offset, src, size);

    m_dirtyCBs.insert(v.cb->name);
    return true;
}

bool Material::ReadBytes(const VarView& v, void* dst, size_t size) const
{
    if (!v.cb || !v.var) return false;
    if (size > v.var->size) return false;
    auto it = m_cbufferValues.find(v.cb->name);
    if (it == m_cbufferValues.end()) return false;

    const auto& bytes = it->second;
    if (v.var->offset + size > bytes.size()) return false;
    std::memcpy(dst, bytes.data() + v.var->offset, size);
    return true;
}

// ��������������������������������������������������������������
// Ÿ�� ���� Setter / Getter
// ��������������������������������������������������������������
bool Material::TrySetFloat(std::string_view cb, std::string_view var, float v)
{
    return WriteBytes(FindVar(cb, var), &v, sizeof(float));
}
bool Material::TryGetFloat(std::string_view cb, std::string_view var, float& out) const
{
    return ReadBytes(FindVar(cb, var), &out, sizeof(float));
}

bool Material::TrySetInt(std::string_view cb, std::string_view var, int32_t v)
{
    return WriteBytes(FindVar(cb, var), &v, sizeof(int32_t));
}
bool Material::TryGetInt(std::string_view cb, std::string_view var, int32_t& out) const
{
    return ReadBytes(FindVar(cb, var), &out, sizeof(int32_t));
}

bool Material::TrySetBool(std::string_view cb, std::string_view var, bool v)
{
    // HLSL cbuffer bool�� 4����Ʈ ������ �����Ƿ� int�� ����
    int32_t iv = v ? 1 : 0;
    return WriteBytes(FindVar(cb, var), &iv, sizeof(int32_t));
}
bool Material::TryGetBool(std::string_view cb, std::string_view var, bool& out) const
{
    int32_t iv{};
    if (!ReadBytes(FindVar(cb, var), &iv, sizeof(int32_t))) return false;
    out = (iv != 0);
    return true;
}

bool Material::TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector2& v)
{
    return WriteBytes(FindVar(cb, var), &v, sizeof(v));
}
bool Material::TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector3& v)
{
    return WriteBytes(FindVar(cb, var), &v, sizeof(v));
}
bool Material::TrySetVector(std::string_view cb, std::string_view var, const Mathf::Vector4& v)
{
    return WriteBytes(FindVar(cb, var), &v, sizeof(v));
}
bool Material::TryGetVector(std::string_view cb, std::string_view var, Mathf::Vector4& out) const
{
    // �ִ� 16����Ʈ�� float4�� �о ������ (var size�� 8/12�� �պκи� ��ȿ)
    auto v = FindVar(cb, var);
    if (!v.cb || !v.var) return false;
    std::memset(&out, 0, sizeof(out));
    size_t n = std::min<size_t>(sizeof(out), v.var->size);
    return ReadBytes(v, &out, n);
}

bool Material::TrySetMatrix(std::string_view cb, std::string_view var, const Mathf::xMatrix& m)
{
    return WriteBytes(FindVar(cb, var), &m, sizeof(m));
}
bool Material::TryGetMatrix(std::string_view cb, std::string_view var, Mathf::xMatrix& out) const
{
    return ReadBytes(FindVar(cb, var), &out, sizeof(out));
}

bool Material::TrySetValue(std::string_view cb, std::string_view var, const void* src, size_t size)
{
	return WriteBytes(FindVar(cb, var), src, size);
}

// ���� Qualified name sugar ("CB.Var") ����
bool Material::TrySetFloat(std::string_view q, float v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetFloat(cb, var, v);
}
bool Material::TryGetFloat(std::string_view q, float& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetFloat(cb, var, out);
}
bool Material::TrySetInt(std::string_view q, int32_t v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetInt(cb, var, v);
}
bool Material::TryGetInt(std::string_view q, int32_t& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetInt(cb, var, out);
}
bool Material::TrySetBool(std::string_view q, bool v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetBool(cb, var, v);
}
bool Material::TryGetBool(std::string_view q, bool& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetBool(cb, var, out);
}
bool Material::TrySetVector(std::string_view q, const Mathf::Vector2& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TrySetVector(std::string_view q, const Mathf::Vector3& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TrySetVector(std::string_view q, const Mathf::Vector4& v) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetVector(cb, var, v);
}
bool Material::TryGetVector(std::string_view q, Mathf::Vector4& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetVector(cb, var, out);
}
bool Material::TrySetMatrix(std::string_view q, const Mathf::xMatrix& m) {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TrySetMatrix(cb, var, m);
}
bool Material::TryGetMatrix(std::string_view q, Mathf::xMatrix& out) const {
    std::string cb, var; if (!SplitQualified(q, cb, var)) return false;
    return TryGetMatrix(cb, var, out);
}

void Material::TrySetMaterialInfo()
{
    TrySetVector("PBRMaterial", "gAlbedo", m_materialInfo.m_baseColor);
	TrySetFloat("PBRMaterial", "gMetallic", m_materialInfo.m_metallic);
	TrySetFloat("PBRMaterial", "gRoughness", m_materialInfo.m_roughness);

	TrySetInt("PBRMaterial", "gUseAlbedoMap", m_materialInfo.m_useBaseColor);
    TrySetInt("PBRMaterial", "gUseOccMetalRough", m_materialInfo.m_useOccRoughMetal);
    TrySetInt("PBRMaterial", "gUseAoMap", m_materialInfo.m_useAOMap);
    TrySetInt("PBRMaterial", "gUseEmmisive", m_materialInfo.m_useEmissive);
    TrySetInt("PBRMaterial", "gNormalState", m_materialInfo.m_useNormalMap);
    TrySetInt("PBRMaterial", "gConvertToLinear", m_materialInfo.m_convertToLinearSpace);

	TrySetFloat("PBRMaterial", "gIOR", m_materialInfo.m_IOR);
}

