#include "Transform.h"
#include "Scene.h"
#include "Terrain.h"
#include "DataSystem.h"
#include "Interfaces/AssetAuthoringPort.h"
#include "Experiment/Cooked/CookedTerrain.h"
#include "AuthoringParsedDocument.h"
#include "SceneManager.h"
#include "RenderScene.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>

static std::string Utf8Encode(const std::wstring& wstr)
{
	int size = static_cast<int>(wstr.size());
	const wchar_t* wstrPtr = wstr.c_str();
	int size_needed = WideCharToMultiByte(
		CP_UTF8, 0,
		wstrPtr, size,
		nullptr, 0, nullptr, nullptr);

	std::string str(size_needed, 0);

	WideCharToMultiByte(
		CP_UTF8, 0,
		wstrPtr, size,
		&str[0], size_needed,
		nullptr, nullptr);

	return str;
}

TerrainComponent::TerrainComponent()
{
	Initialize();
}

void TerrainComponent::Initialize()
{
	m_heightMap.assign(m_width * m_height, 0.0f);
	m_vNormalMap.assign(m_width * m_height, math::vector3{ 0.0f, 1.0f, 0.0f });

	// 레이어 초기화
	m_layers.clear();
	m_layerHeightMap.clear();

	// 한 번만 초기 메쉬 생성
	std::vector<Vertex> verts(m_width * m_height);
	for (int i = 0; i < m_height; ++i)
	{
		for (int j = 0; j < m_width; ++j)
		{
			int idx = i * m_width + j;
			verts[idx] = Vertex(
				// 위치(x, 높이, z)
				math::vector3{ (float)j, m_heightMap[idx], (float)i },
				// 노말
				m_vNormalMap[idx],
				// UV0
				math::vector2{ (float)j / (float)m_width, (float)i / (float)m_height }
			);
		}
	}

	std::vector<uint32_t> indices;
	indices.reserve((m_width - 1) * (m_height - 1) * 6);

	for (int i = 0; i < m_height - 1; ++i)
	{
		for (int j = 0; j < m_width - 1; ++j)
		{
			uint32_t topLeft = i * m_width + j;
			uint32_t bottomLeft = (i + 1) * m_width + j;
			uint32_t topRight = i * m_width + (j + 1);
			uint32_t bottomRight = (i + 1) * m_width + (j + 1);

			// 삼각형 1
			indices.push_back(topLeft);
			indices.push_back(bottomLeft);
			indices.push_back(topRight);
			// 삼각형 2
			indices.push_back(bottomLeft);
			indices.push_back(bottomRight);
			indices.push_back(topRight);
		}
	}

	// TerrainMesh 생성 (한 번만)
	m_pTerrainMesh = std::make_shared<TerrainMesh>(m_name.ToString(), verts, indices, (uint32_t)m_width);
	/*m_pMesh = new TerrainMesh(
		m_name.ToString(),
		verts,
		indices,
		(uint32_t)m_width
	);*/

	m_pMaterial = std::make_shared<TerrainMaterial>();
	//// TerrainMaterial 초기화 -> 스플랫맵 텍스처 생성
	m_pMaterial->Initialize(m_width, m_height);
}

void TerrainComponent::Resize(int newWidth, int newHeight)
{
	m_width = newWidth;
	m_height = newHeight;
	m_heightMap.assign(m_width * m_height, 0.0f);
	m_vNormalMap.assign(m_width * m_height, { 0.0f, 1.0f, 0.0f });

	// 레이어 가중치 맵도 새 크기에 맞게 다시 할당합니다.
	for (auto& w : m_layerHeightMap)
		w.assign(m_width * m_height, 0.0f);

	// 기존 메시를 해제합니다.
	m_pTerrainMesh.reset();

	// 새 크기로 메시를 재생성합니다.
	{
		std::vector<Vertex> verts(m_width * m_height);
		for (int i = 0; i < m_height; ++i)
		{
			for (int j = 0; j < m_width; ++j)
			{
				int idx = i * m_width + j;
				verts[idx] = Vertex(
					{ (float)j, m_heightMap[idx], (float)i },
					m_vNormalMap[idx],
					{ (float)j / m_width, (float)i / m_height }
				);
			}
		}
		std::vector<uint32_t> indices;
		indices.reserve((m_width - 1) * (m_height - 1) * 6);
		for (int i = 0; i < m_height - 1; ++i)
		{
			for (int j = 0; j < m_width - 1; ++j)
			{
				uint32_t tl = i * m_width + j;
				uint32_t bl = (i + 1) * m_width + j;
				uint32_t tr = i * m_width + (j + 1);
				uint32_t br = (i + 1) * m_width + (j + 1);
				indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
				indices.push_back(bl); indices.push_back(br); indices.push_back(tr);
			}
		}
		m_pTerrainMesh = std::make_shared<TerrainMesh>(m_name.ToString(), verts, indices, (uint32_t)m_width);
	}

	// [수정] MateialDataUpdate를 호출하여 모든 머티리얼 리소스를 새 크기에 맞게 재구성합니다.
	if (m_pMaterial)
	{
		m_pMaterial->MateialDataUpdate(newWidth, newHeight, m_layers, m_layerHeightMap);
	}
}

void TerrainComponent::ApplyBrush(const TerrainBrush& brush) {
	// 1) 브러시가 닿을 최소/최대 X,Y 계산
	const math::vector3 pivotWorldPos = GetOwner()->Transform_().GetWorldPosition();

	// 1) 브러시 월드 위치
	const math::vector3 brushWorldPos{ brush.m_center.x, 0.0f, brush.m_center.y };

	// 2) 로컬 그리드 위치 = 브러시 월드 좌표 – 피벗 월드 좌표
	const math::vector3 localPos = brushWorldPos - pivotWorldPos;


	int minX = std::max(0, int(localPos.x - brush.m_radius));
	int maxX = std::min(m_width - 1, int(localPos.x + brush.m_radius));
	int minY = std::max(0, int(localPos.z - brush.m_radius));
	int maxY = std::min(m_height - 1, int(localPos.z + brush.m_radius));

	// [수정] 브러시가 터레인 영역을 벗어났을 경우, 아무 작업도 하지 않고 즉시 리턴합니다.
	if (minX > maxX || minY > maxY) {
		return;
	}

	// 2) 높이 맵 갱신: 브러시 원 내부만
	for (int i = minY; i <= maxY; ++i)
	{
		for (int j = minX; j <= maxX; ++j)
		{
			float dx = localPos.x - (float)j;
			float dy = localPos.z - (float)i;
			float distSq = dx * dx + dy * dy;
			if (distSq <= brush.m_radius * brush.m_radius)
			{
				float dist = std::sqrt(distSq);
				float t = brush.m_strength * (1.0f - (dist / brush.m_radius));
				//거리 비례 브러시 강도 -> mask 적용
				if (brush.m_maskID != -1 && brush.m_masks.size() > brush.m_maskID)
				{
					const auto& mask = brush.m_masks[brush.m_maskID];

					//distSq로 mask의 uv 좌표 계산
					//uv 좌표 정규화
					float u = (dx / (2.0f * brush.m_radius) + 0.5f);
					float v = (dy / (2.0f * brush.m_radius) + 0.5f);
					//mask의 uv 좌표 계산
					int maskX = static_cast<int>(u * (mask.m_maskWidth - 1));
					int maskY = static_cast<int>(v * (mask.m_maskHeight - 1));

					float maskValue = mask.m_mask[maskY * mask.m_maskWidth + maskX] / 255.0f; //png 파일은 0~255 범위이므로 255로 나누어 0~1 범위로 변환
					t = maskValue; // 브러시 강도에 마스크 적용
				}
				int idx = i * m_width + j;

				switch (brush.m_mode)
				{
				case TerrainBrush::Mode::Raise:
					m_heightMap[idx] += t;
					if (m_heightMap[idx] > m_maxHeight) m_heightMap[idx] = m_maxHeight; // 최대 높이 제한
					break;
				case TerrainBrush::Mode::Lower:
					m_heightMap[idx] -= t;
					if (m_heightMap[idx] < m_minHeight) m_heightMap[idx] = m_minHeight; // 최소 높이 제한
					break;
				case TerrainBrush::Mode::Flatten:
					m_heightMap[idx] = brush.m_flatTargetHeight;
					break;
				case TerrainBrush::Mode::PaintLayer:
					PaintLayer(brush.m_layerID, j, i, t);
					break;
				}
			}
		}
	}

	// 3) 노멀 재계산 (바뀐 영역 + 주변 1픽셀만)
	RecalculateNormalsPatch(minX, minY, maxX, maxY);

	// 4) 버텍스 버퍼 부분 업로드
	//    패치 크기 = (maxX-minX+1) × (maxY-minY+1)
	int patchW = maxX - minX + 1;
	int patchH = maxY - minY + 1;

	if (0 > patchH || 0 > patchW) return;

	std::vector<Vertex> patchVerts;
	patchVerts.reserve(patchW * patchH);

	for (int i = minY; i <= maxY; ++i)
	{
		for (int j = minX; j <= maxX; ++j)
		{
			int idx = i * m_width + j;
			Vertex v;
			v.position = { (float)j, m_heightMap[idx], (float)i };
			v.normal = m_vNormalMap[idx];
			v.uv0 = { (float)j / (float)m_width, (float)i / (float)m_height };
			// uv1, tangent, bitangent, boneIndices, boneWeights는 필요할 때 추가 복사
			patchVerts.push_back(v);
		}
	}

	// 실제 GPU 버퍼에 패치만 업로드
	m_pTerrainMesh->UpdateVertexBufferPatch(
		patchVerts.data(),
		(uint32_t)minX, (uint32_t)minY, (uint32_t)patchW, (uint32_t)patchH
	);
	/*m_pMesh->UpdateVertexBufferPatch(
		patchVerts.data(),
		(uint32_t)minX,
		(uint32_t)minY,
		(uint32_t)patchW,
		(uint32_t)patchH
	);*/


	// --- [수정된 스플랫맵 업데이트 로직] ---
	if (brush.m_mode == TerrainBrush::Mode::PaintLayer)
	{
		// 페인팅으로 인해 여러 레이어의 가중치가 변경되었으므로, 모든 레이어를 순회하며
		// 브러시가 닿은 영역만 부분적으로 업데이트합니다.
		for (uint32_t i = 0; i < m_layers.size(); ++i)
		{
			// 브러시 영역만큼의 작은 데이터 패치를 생성합니다.
			std::vector<BYTE> patchData;
			patchData.reserve(patchW * patchH);

			for (int r = minY; r <= maxY; ++r)
			{
				for (int c = minX; c <= maxX; ++c)
				{
					int idx = r * m_width + c;
					float weight = m_layerHeightMap[i][idx];
					patchData.push_back(static_cast<BYTE>(std::clamp(weight, 0.0f, 1.0f) * 255.0f));
				}
			}
			// i번째 레이어의 스플랫맵 슬라이스에, 변경된 영역(patch)만 업데이트합니다.
			m_pMaterial->UpdateSplatMapPatch(i, minX, minY, patchW, patchH, patchData);
		}
	}
}

void TerrainComponent::RecalculateNormalsPatch(int minX, int minY, int maxX, int maxY)
{
	int startX = std::max(0, minX - 1);
	int endX = std::min(m_width - 1, maxX + 1);
	int startY = std::max(0, minY - 1);
	int endY = std::min(m_height - 1, maxY + 1);

	for (int i = startY; i <= endY; ++i)
	{
		for (int j = startX; j <= endX; ++j)
		{
			float heightL = (j > 0) ? m_heightMap[i * m_width + (j - 1)] : m_heightMap[i * m_width + j];
			float heightR = (j < m_width - 1) ? m_heightMap[i * m_width + (j + 1)] : m_heightMap[i * m_width + j];
			float heightD = (i > 0) ? m_heightMap[(i - 1) * m_width + j] : m_heightMap[i * m_width + j];
			float heightU = (i < m_height - 1) ? m_heightMap[(i + 1) * m_width + j] : m_heightMap[i * m_width + j];

			math::vector3 normal;
			normal.x = heightL - heightR;
			normal.y = 2.0f;
			normal.z = heightD - heightU;

			float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
			if (len > 0.0f)
			{
				normal.x /= len;
				normal.y /= len;
				normal.z /= len;
			}
			m_vNormalMap[i * m_width + j] = normal;
		}
	}
}

void TerrainComponent::PaintLayer(uint32_t targetLayerId, int x, int y, float strength) {
	if (targetLayerId >= m_layers.size() || m_layers.empty()) return;

	int idx = y * m_width + x;
	if (idx < 0 || idx >= m_width * m_height) return;

	// 1. 현재 타겟 레이어의 가중치를 가져오고, 이번에 더할 양을 결정합니다.
	//    (기존 가중치와 더해서 1.0을 넘지 않도록)
	float originalTargetWeight = m_layerHeightMap[targetLayerId][idx];
	float amountToAdd = strength;
	if (originalTargetWeight + amountToAdd > 1.0f) {
		amountToAdd = 1.0f - originalTargetWeight;
	}
	if (amountToAdd <= 0.0001f) return; // 더할 양이 거의 없으면 종료

	// 2. 타겟이 아닌 다른 레이어들의 현재 가중치 합을 계산합니다.
	float otherLayersWeightSum = 0.0f;
	for (uint32_t i = 0; i < m_layers.size(); ++i) {
		if (i != targetLayerId) {
			otherLayersWeightSum += m_layerHeightMap[i][idx];
		}
	}

	// 3. 타겟 레이어에 가중치를 더합니다.
	m_layerHeightMap[targetLayerId][idx] += amountToAdd;

	// 4. 다른 레이어들에서 가중치를 비례하여 뺍니다.
	if (otherLayersWeightSum > 0.0001f) // 0으로 나누는 것을 방지
	{
		float removalFactor = amountToAdd / otherLayersWeightSum;
		for (uint32_t i = 0; i < m_layers.size(); ++i) {
			if (i != targetLayerId) {
				m_layerHeightMap[i][idx] -= m_layerHeightMap[i][idx] * removalFactor;
			}
		}
	}

	// 5. (선택적이지만 권장) 부동소수점 오차 누적을 막기 위해 최종 정규화를 수행합니다.
	float finalSum = 0.0f;
	for (uint32_t i = 0; i < m_layers.size(); ++i) {
		finalSum += m_layerHeightMap[i][idx];
	}

	if (finalSum > 0.0001f) {
		for (uint32_t i = 0; i < m_layers.size(); ++i) {
			m_layerHeightMap[i][idx] /= finalSum;
		}
	}
}

void TerrainComponent::Save(const std::wstring& assetRoot, const std::wstring& name)
{
	if (m_width <= 0 || m_height <= 0 ||
		m_layers.size() != m_layerHeightMap.size())
	{
		Debug::PrintLog(spdlog::level::err, "Terrain authoring snapshot is inconsistent");
		return;
	}

	TerrainAuthoringRequest request{};
	request.destinationDirectory = file::path(assetRoot);
	request.name = name;
	request.terrainId = m_terrainID;
	request.width = static_cast<uint32>(m_width);
	request.height = static_cast<uint32>(m_height);
	request.minHeight = m_minHeight;
	request.maxHeight = m_maxHeight;
	request.heightMap = m_heightMap;
	request.layers.reserve(m_layers.size());
	for (size_t index = 0; index < m_layers.size(); ++index)
	{
		const TerrainLayer& layer = m_layers[index];
		TerrainAuthoringLayerSnapshot snapshot{};
		snapshot.layerId = layer.m_layerID;
		snapshot.name = layer.layerName;
		snapshot.diffuseTextureSource = layer.diffuseTexturePath;
		snapshot.tiling = layer.tilling;
		snapshot.splatWeights = m_layerHeightMap[index];
		request.layers.push_back(std::move(snapshot));
	}

	TerrainAuthoringResult result{};
	if (!AssetAuthoringPort::WriteTerrain(request, result))
	{
		Debug::PrintLog(spdlog::level::err,
			"Terrain save requires a complete Editor authoring transaction");
		return;
	}

	m_terrainTargetPath = result.descriptorPath.wstring();
	m_trrainAssetGuid = result.guid;
	Debug::PrintLog(spdlog::level::debug, "Terrain saved to: " + Utf8Encode(m_terrainTargetPath));
}
bool TerrainComponent::Load(const std::wstring& filePath)
{
	Debug::PrintLog(spdlog::level::debug, "Loading terrain from: " + Utf8Encode(filePath));

	namespace fs = std::filesystem;
	const fs::path descriptorPath = filePath;
	if (!fs::is_regular_file(descriptorPath))
	{
		Debug::PrintLog(spdlog::level::err, "Terrain descriptor does not exist: "
			+ Utf8Encode(descriptorPath.wstring()));
		return false;
	}

    std::ifstream kindStream(descriptorPath, std::ios::binary);
    std::array<unsigned char, 4> magic{};
    kindStream.read(reinterpret_cast<char*>(magic.data()), magic.size());
    if (kindStream && magic == std::array<unsigned char, 4>{ 0x4eu, 0x52u, 0x42u, 0x54u })
    {
        return LoadRunTimeTerrain(filePath);
    }
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        Debug::PrintLog(spdlog::level::err, "Terrain requires its neutral v2 package artifact; recook: " + descriptorPath.string());
        return false;
    }

	std::string parseError;
	const Authoring::ParsedDocument document =
		Authoring::ParsedDocument::ParseFile(descriptorPath.string(), parseError);
	if (!document)
	{
		Debug::PrintLog(spdlog::level::err, "Terrain descriptor parse failed: "
			+ Utf8Encode(descriptorPath.wstring()) + " / " + parseError);
		return false;
	}

	const Authoring::ReadNode root = document.Root();
	const Authoring::ReadNode splatmaps = root["splatmaps"];
	const Authoring::ReadNode layers = root["layers"];
	if (!root.IsMap() || root["schemaVersion"].As<int>(0) != 1
		|| !root["name"].IsScalar() || !root["terrainID"].IsScalar()
		|| !root["width"].IsScalar() || !root["height"].IsScalar()
		|| !root["minHeight"].IsScalar() || !root["maxHeight"].IsScalar()
		|| !root["heightmap"].IsScalar() || !splatmaps.IsSequence()
		|| !layers.IsSequence() || splatmaps.Size() != layers.Size()
		|| layers.Size() > 4)
	{
		Debug::PrintLog(spdlog::level::err, "Terrain descriptor schema is invalid: "
			+ descriptorPath.string());
		return false;
	}

	uint32_t tmpTerrainID = root["terrainID"].As<uint32_t>();
	int tmpWidth = root["width"].As<int>();
	int tmpHeight = root["height"].As<int>();
	const float tmpMinHeight = root["minHeight"].As<float>();
	const float tmpMaxHeight = root["maxHeight"].As<float>();
	if (tmpWidth <= 0 || tmpHeight <= 0
		|| !std::isfinite(tmpMinHeight) || !std::isfinite(tmpMaxHeight)
		|| tmpMinHeight > tmpMaxHeight
		|| static_cast<std::size_t>(tmpHeight)
			> std::numeric_limits<std::size_t>::max()
				/ static_cast<std::size_t>(tmpWidth))
	{
		Debug::PrintLog(spdlog::level::err, "Terrain descriptor dimensions are invalid: "
			+ descriptorPath.string());
		return false;
	}

	const fs::path terrainRoot = PathFinder::Relative("Terrain");
	const bool usesRelativePaths =
		descriptorPath.parent_path().lexically_normal()
			== terrainRoot.lexically_normal();
	const auto ResolvePath = [&](const Authoring::ReadNode& pathNode)
	{
		const fs::path stored = pathNode.AsString();
		return usesRelativePaths
			? PathFinder::TerrainSourcePath(stored.generic_string()) : stored;
	};

	fs::path heightMapPath = ResolvePath(root["heightmap"]);
	auto tmpHeightMap = std::vector<float>(
		static_cast<std::size_t>(tmpWidth) * static_cast<std::size_t>(tmpHeight),
		0.0f);
	if (!LoadEditorHeightMap(heightMapPath, static_cast<float>(tmpWidth),
		static_cast<float>(tmpHeight), tmpMinHeight, tmpMaxHeight, tmpHeightMap))
	{
		Debug::PrintLog(spdlog::level::err, "Terrain height map load failed: "
			+ heightMapPath.string());
		return false;
	}

	auto tmpLayerHeightMap =
		std::vector<std::vector<float>>(splatmaps.Size());
	std::size_t splatIndex = 0;
	for (const Authoring::ReadNode splatmap : splatmaps)
	{
		if (!splatmap.IsScalar()) return false;
		fs::path splatPath = ResolvePath(splatmap);
		if (!LoadEditorSplatMap(splatPath, tmpWidth, tmpHeight,
			static_cast<int>(splatIndex), tmpLayerHeightMap))
		{
			Debug::PrintLog(spdlog::level::err, "Terrain splat map load failed: "
				+ splatPath.string());
			return false;
		}
		++splatIndex;
	}

	auto tmpLayerDescs = std::vector<TerrainLayer>();
	tmpLayerDescs.reserve(layers.Size());
	float tmpNextLayerID = 0;
	for (const Authoring::ReadNode layerData : layers)
	{
		if (!layerData.IsMap() || !layerData["layerID"].IsScalar()
			|| !layerData["layerName"].IsScalar()
			|| !layerData["diffuseTexturePath"].IsScalar()
			|| !layerData["tiling"].IsScalar())
		{
			Debug::PrintLog(spdlog::level::err, "Terrain layer schema is invalid: "
				+ descriptorPath.string());
			return false;
		}

		TerrainLayer desc;
		desc.m_layerID = layerData["layerID"].As<uint32_t>();
		desc.layerName = layerData["layerName"].AsString();
		desc.diffuseTexturePath =
			ResolvePath(layerData["diffuseTexturePath"]);
		desc.tilling = layerData["tiling"].As<float>();
		if (desc.layerName.empty() || !std::isfinite(desc.tilling)
			|| !file::is_regular_file(desc.diffuseTexturePath))
		{
			Debug::PrintLog(spdlog::level::err, "Terrain layer value is invalid: " + desc.layerName);
			return false;
		}
		own::shared_owner<const Texture> diffuseTexture{
			Texture::LoadFormPath(desc.diffuseTexturePath) };
		if (!diffuseTexture)
		{
			Debug::PrintLog(spdlog::level::err, "Failed to load diffuse texture: " + desc.layerName);
			return false;
		}
		desc.diffuseTexture = std::move(diffuseTexture);
		tmpLayerDescs.push_back(desc);
		++tmpNextLayerID;
	}


	//임시저장 변수 스왑 및
	//바뀐 맴버 정보로 메쉬 및 버퍼,텍스쳐 업데이트
	std::swap(m_terrainID, tmpTerrainID);
	std::swap(m_width, tmpWidth);
	std::swap(m_height, tmpHeight);
	m_minHeight = tmpMinHeight;
	m_maxHeight = tmpMaxHeight;
	Resize(m_width, m_height); // 높이맵 크기 변경
	std::swap(m_heightMap, tmpHeightMap);
	RecalculateNormalsPatch(0, 0, m_width, m_height); // 높이맵 변경 후 노말 재계산
	//메쉬 업데이트
	std::vector<Vertex> patchVerts;
	patchVerts.reserve(m_width * m_height);
	for (int i = 0; i < m_height; ++i) {
		for (int j = 0; j < m_width; ++j) {
			int idx = i * m_width + j;
			patchVerts.push_back(Vertex(
				{ (float)j, m_heightMap[idx], (float)i },
				m_vNormalMap[idx],
				{ (float)j / m_width, (float)i / m_height }
			));
		}
	}
	m_pTerrainMesh->UpdateVertexBuffer(
		patchVerts.data(),
		(uint32_t)m_width *
		(uint32_t)m_height
	); // 높이맵 변경 후 메쉬 업데이트

	//m_pMesh->UpdateVertexBuffer(
	//	patchVerts.data(),
	//	(uint32_t)m_width *
	//	(uint32_t)m_height
	//); // 높이맵 변경 후 메쉬 업데이트
	//InitSplatMapTexture(m_width, m_height); // 스플랫맵 텍스처 초기화
	std::swap(m_layerHeightMap, tmpLayerHeightMap);
	//UpdateSplatMapPatch(0, 0, m_width, m_height); // 스플랫맵 패치 업데이트
	std::swap(m_layers, tmpLayerDescs);
	m_pMaterial->MateialDataUpdate(m_width, m_height, m_layers, m_layerHeightMap); // 레이어 정보 업데이트
	m_nextLayerID = tmpNextLayerID;
	m_selectedLayerID = 0xFFFFFFFF; // 선택된 레이어 초기화
	//LoadLayers();


	//로드 완료 후 리소스 해제
	tmpHeightMap.clear();
	tmpLayerHeightMap.clear();
	tmpLayerDescs.clear();
	PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);

	file::path Path = filePath + L".meta";
	if (file::exists(Path))
	{
		std::string parseError;
		const Authoring::ParsedDocument document =
			Authoring::ParsedDocument::ParseFile(Path.string(), parseError);
		const Authoring::ReadNode node = document.Root();

		if (!document)
		{
			Debug::PrintLog(spdlog::level::err, "Terrain sidecar parse failed: " + parseError);
		}
		else if (node["guid"] && !node["guid"].IsNull())
		{
			FileGuid fileGuid = node["guid"].AsString();
			if (fileGuid != nullFileGuid)
			{
				m_trrainAssetGuid = fileGuid;
			}
		}
	}
	m_terrainTargetPath = filePath;
	return true;
}

bool TerrainComponent::LoadEditorHeightMap(std::filesystem::path& pngPath, float dataWidth, float dataHeight, float minH, float maXH, std::vector<float>& out)
{
    TerrainSourceImage image;
    if (!AssetAuthoringPort::ReadTerrainSourceImage(pngPath, TerrainSourceImageKind::HeightBits, image)
        || static_cast<float>(image.width) != dataWidth || static_cast<float>(image.height) != dataHeight)
    {
        return false;
    }
    out = std::move(image.heights);
    return true;
}

bool TerrainComponent::LoadEditorSplatMap(std::filesystem::path& pngPath, int dataWidth, int dataHeight, int layerIndex, std::vector<std::vector<float>>& out)
{
    if (dataWidth <= 0 || dataHeight <= 0 || layerIndex < 0 || static_cast<size_t>(layerIndex) >= out.size())
    {
        return false;
    }
    TerrainSourceImage image;
    if (!AssetAuthoringPort::ReadTerrainSourceImage(pngPath, TerrainSourceImageKind::Gray8, image)
        || image.width != static_cast<uint32_t>(dataWidth) || image.height != static_cast<uint32_t>(dataHeight))
    {
        return false;
    }
    out[layerIndex].resize(image.gray.size());
    for (size_t index = 0; index < image.gray.size(); ++index)
    {
        out[layerIndex][index] = image.gray[index] / 255.0f;
    }
    return true;
}

void TerrainComponent::UpdateLayerDesc()
{
	if (!m_pMaterial) return;

	// 머티리얼의 버퍼 데이터를 직접 가져와 채웁니다.
	TerrainLayerBuffer& layerBufferData = m_pMaterial->m_layerBufferData;
	layerBufferData.useLayer = !m_layers.empty();
	layerBufferData.numLayers = static_cast<int>(m_layers.size());

	for (int i = 0; i < MAX_TERRAIN_LAYERS; ++i)
	{
		if (i < m_layers.size()) {
			// x에 타일링 값을 두고 나머지 lane은 future use를 위해 0으로 둔다.
			layerBufferData.layerTilling[i] = { m_layers[i].tilling, 0.f, 0.f, 0.f };
		}
		else {
			// [수정] 기본값도 4-float 형식으로 할당합니다.
			layerBufferData.layerTilling[i] = { 1.0f, 0.f, 0.f, 0.f };
		}
	}

	// 최종적으로 채워진 데이터로 상수 버퍼를 업데이트합니다.
	m_pMaterial->UpdateBuffer(layerBufferData);
}

void TerrainComponent::OnInitialized()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectTerrainComponent(this);
		if (renderScene) renderScene->RegisterCommand(this);
	}
}

void TerrainComponent::OnAddedToScene()
{
	if (!HasLifecycleState(State_Initialized) || !GetOwner()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->CollectTerrainComponent(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->RegisterCommand(this);
	}
}

void TerrainComponent::OnRemovingFromScene()
{
	if (!GetOwner() || GetOwner()->IsDestroyMark()) return;
	if (Scene* scene = GetOwner()->GetScene())
	{
		scene->UnCollectTerrainComponent(this);
		if (auto* renderScene = SceneManagers->GetRenderScene())
			renderScene->UnregisterCommand(this);
	}
}

void TerrainComponent::OnUninitializing()
{
	auto scene = GetOwner()->m_ownerScene;
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectTerrainComponent(this);
		if (renderScene) renderScene->UnregisterCommand(this);
	}
}

//void TerrainComponent::AddLayer(const std::wstring& path, const std::wstring& diffuseFile, float tilling)
//{
//	TerrainLayer newLayer;
//	newLayer.m_layerID = m_nextLayerID++;
//	newLayer.tilling = tilling;
//	newLayer.layerName = std::string(diffuseFile.begin(), diffuseFile.end());
//	newLayer.diffuseTexturePath = path;
//	newLayer.tilling = tilling;
//	// diffuseTexture 로드
//
//	m_pMaterial->AddLayer(newLayer); // 머티리얼에 레이어 추가
//	m_layers.push_back(newLayer);
//	m_layerHeightMap.push_back(std::vector<float>(m_width * m_height, 0.0f));
//	std::vector<BYTE> splatMapData(m_width * m_height * 4, 0); // RGBA 4채널 초기화
//
//	for (int y = 0; y < m_height; ++y)
//	{
//		for (int x = 0; x < m_width; ++x)
//		{
//			int idx = y * m_width + x;
//			int dstOffset = (y * m_width + x) * 4; // RGBA 4채널
//
//			// 레이어 가중치 계산
//			for (int layerIdx = 0; layerIdx < (int)m_layers.size() && layerIdx < 4; ++layerIdx) // 최대 4개 레이어만 사용
//			{
//				float w = std::clamp(m_layerHeightMap[layerIdx][idx], 0.0f, 1.0f);
//				splatMapData[dstOffset + layerIdx] = static_cast<BYTE>(w * 255.0f); // R, G, B, A 채널에 가중치 저장
//			}
//		}
//	}
//
//	m_pMaterial->UpdateSplatMapPatch(0, 0, m_width, m_height, splatMapData); // layer 추가 후 스플랫맵 업데이트
//}

void TerrainComponent::AddLayer(const std::wstring& path, const std::wstring& diffuseFile, float tilling)
{
	// 최대 4개 레이어 제한
	// MAX_TERRAIN_LAYERS 상수를 사용하도록 수정
	if (m_layers.size() >= MAX_TERRAIN_LAYERS)
	{
		Debug::PrintLog(spdlog::level::warn, "Cannot add more layers. The current limit is " + std::to_string(MAX_TERRAIN_LAYERS));
		return;
	}

	TerrainLayer newLayer;
	newLayer.m_layerID = m_nextLayerID;
	newLayer.tilling = tilling;
	newLayer.layerName = std::string(diffuseFile.begin(), diffuseFile.end());
	newLayer.diffuseTexturePath = path;

	// 1. TerrainComponent가 직접 텍스처를 로드합니다.
	file::path texturePath = file::path(newLayer.diffuseTexturePath);
	if (file::exists(texturePath))
	{
		newLayer.diffuseTexture = Texture::LoadFormPath(newLayer.diffuseTexturePath);
	}
	else
	{
		Debug::PrintLog(spdlog::level::err, "Failed to load diffuse texture: " + newLayer.layerName);
		return;
	}

	// 텍스처 로딩 성공 여부 확인
	if (!newLayer.diffuseTexture)
	{
		Debug::PrintLog(spdlog::level::err, "Texture object is null after loading: " + newLayer.layerName);
		return;
	}

	// 2. 컴포넌트의 CPU 측 데이터 구조를 업데이트합니다.
	m_layers.push_back(newLayer);
	m_layerHeightMap.push_back(std::vector<float>(m_width * m_height, 0.0f));
	m_nextLayerID++;

	// 3. 메인 업데이트 함수를 호출하여 GPU 상태 전체를 동기화합니다.
	m_pMaterial->MateialDataUpdate(m_width, m_height, m_layers, m_layerHeightMap);
}


void TerrainComponent::RemoveLayer(uint32_t layerID)
{
	if (layerID >= m_layers.size())
	{
		Debug::PrintLog(spdlog::level::err, "Invalid layer ID: " + std::to_string(layerID));
		return;
	}

	// 1. CPU 데이터에서 레이어와 가중치 맵을 제거합니다.
	m_layers.erase(m_layers.begin() + layerID);
	m_layerHeightMap.erase(m_layerHeightMap.begin() + layerID);

	// 2. 나머지 레이어들의 ID를 순차적으로 재정렬합니다.
	for (uint32_t i = 0; i < m_layers.size(); ++i)
	{
		m_layers[i].m_layerID = i;
	}

	// 3. 다음 레이어 ID를 업데이트합니다.
	m_nextLayerID = static_cast<uint32_t>(m_layers.size());

	// 4. TerrainMaterial의 전체 데이터 업데이트 함수를 호출하여
	//    GPU 리소스(스플랫맵, 텍스처 배열 등)를 한 번에 갱신합니다.
	//    사용자 요청에 따라 가중치 재정규화는 생략하며,
	//    셰이더의 normalize(splat) 연산이 최종 블렌딩을 처리합니다.
	if (m_pMaterial)
	{
		m_pMaterial->MateialDataUpdate(m_width, m_height, m_layers, m_layerHeightMap);
	}
}

void TerrainComponent::ClearLayers()
{
	m_layers.clear();
	m_layerHeightMap.clear();
	m_nextLayerID = 0;

	m_pMaterial->ClearLayers(); // 머티리얼에서 레이어 제거
}

void TerrainComponent::RefreshTexture()
{
    if (!PathFinder::IsAssetAuthoringEnabled())
    {
        return;
    }
	for (auto& layer : m_layers) {
		layer.diffuseTexture = Texture::LoadFormPath(layer.diffuseTexturePath);
	}
	m_pMaterial->MateialDataUpdate(m_width, m_height, m_layers, m_layerHeightMap);
}

/// 브러쉬 마스크 텍스쳐 로드
bool TerrainComponent::LoadBrushMaskTexture(const std::wstring& path, std::vector<uint8_t>& outMask, int& dataWidth, int& dataHeight)
{
    TerrainSourceImage image;
    if (!AssetAuthoringPort::ReadTerrainSourceImage(file::path(path), TerrainSourceImageKind::Gray8, image))
    {
        return false;
    }
    outMask = std::move(image.gray);
    dataWidth = static_cast<int>(image.width);
    dataHeight = static_cast<int>(image.height);
    return true;
}

void TerrainComponent::SetBrushMaskTexture(TerrainBrush* brush, const std::wstring& path)
{
	if (!brush) {
		Debug::PrintLog(spdlog::level::err, "Brush is null");
		return;
	}

	if (path.empty()) {
		Debug::PrintLog(spdlog::level::err, "Brush mask texture path is empty");
		return;
	}

	TerrainBrush::BrushMask mask;

	if (!LoadBrushMaskTexture(path, mask.m_mask, mask.m_maskWidth, mask.m_maskHeight)) {
		Debug::PrintLog(spdlog::level::err, "Failed to load brush mask texture: " + Utf8Encode(path));
		return;
	}

	// ★ 여기 있던 DX11 마스크 텍스처·SRV 생성과 Map/memcpy를 걷었다
	//   (PHASE 11 착수, 2026-08-08). 마스크의 진실은 CPU 배열(mask.m_mask)이고
	//   그것은 위 LoadBrushMaskTexture가 이미 채운다 — GPU 사본은 에디터
	//   썸네일 하나만 소비하고 있었다. 그 썸네일은 지형 패스와 함께 DX12로
	//   복원한다.

	brush->m_masks.push_back(mask);
	std::string maskName = "mask_" + std::to_string(brush->m_masks.size() - 1);
	brush->m_maskNames.push_back(maskName);
}


bool TerrainComponent::LoadRunTimeTerrain(const std::wstring& filePath)
{
    namespace ck = experiment::cooked;
    static_assert(sizeof(Vertex) == ck::kCookedTerrainVertexBytes, "Update terrain working-set admission when the vertex layout changes");
    try
    {
        std::ifstream input(file::path(filePath), std::ios::binary | std::ios::ate);
        const auto size = input.tellg();
        if (!input || size <= 0 || static_cast<std::uint64_t>(size) > ck::kCookedTerrainMaxBytes)
        {
            return false;
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ck::CookedTerrainView terrain;
        std::string failure;
        if (!input || input.peek() != std::char_traits<char>::eof() || !ck::ReadCookedTerrain(bytes, terrain, failure))
        {
            Debug::PrintLog(spdlog::level::err, "Terrain artifact rejected: " + failure);
            return false;
        }
        // All offsets, dimensions, scalar values and trailing bytes are checked
        // before image/vector allocation. No source callback participates here.
        const auto pixels = static_cast<std::size_t>(terrain.width) * terrain.height;
        std::vector<float> heights(pixels);
        for (std::size_t index = 0u; index < pixels; ++index)
        {
            heights[index] = ck::CookedTerrainHeight(terrain.heights, index);
        }
        std::vector<std::vector<float>> weights(terrain.layerCount);
        std::vector<TerrainLayer> layers;
        layers.reserve(terrain.layerCount);
        std::uint32_t nextLayerId{};
        for (std::size_t index = 0u; index < terrain.layerCount; ++index)
        {
            const auto& source = terrain.layers[index];
            TerrainLayer layer;
            layer.m_layerID = source.id;
            layer.layerName = std::string(source.name);
            layer.diffuseTexturePath = file::u8path(source.diffuseReference).wstring();
            layer.tilling = source.tiling;
            layer.diffuseTexture = Texture::LoadSharedFromMemory(source.texture);
            if (!layer.diffuseTexture)
            {
                Debug::PrintLog(spdlog::level::err, "Terrain contains an invalid cooked diffuse texture: " + layer.layerName);
                return false;
            }
            weights[index].resize(pixels);
            for (std::size_t pixel = 0u; pixel < pixels; ++pixel)
            {
                weights[index][pixel] = static_cast<std::uint8_t>(source.gray[pixel]) / 255.0f;
            }
            nextLayerId = (std::max)(nextLayerId, source.id + 1u);
            layers.push_back(std::move(layer));
        }
        // Build replacement render inputs before publishing any live state.
        const auto width = static_cast<int>(terrain.width);
        const auto height = static_cast<int>(terrain.height);
        std::vector<math::vector3> normals(pixels);
        std::vector<Vertex> vertices;
        vertices.reserve(pixels);
        std::vector<std::uint32_t> indices;
        indices.reserve(static_cast<std::size_t>(width - 1) * (height - 1) * 6u);
        for (int row = 0; row < height; ++row)
        {
            for (int column = 0; column < width; ++column)
            {
                const auto index = static_cast<std::size_t>(row) * width + column;
                const double left = heights[column > 0 ? index - 1u : index];
                const double right = heights[column + 1 < width ? index + 1u : index];
                const double down = heights[row > 0 ? index - width : index];
                const double up = heights[row + 1 < height ? index + width : index];
                const double x = left - right;
                const double z = down - up;
                const double length = std::sqrt(x * x + 4.0 + z * z);
                normals[index] = { float(x / length), float(2.0 / length), float(z / length) };
                vertices.emplace_back(math::vector3{ float(column), heights[index], float(row) },
                    normals[index], math::vector2{ float(column) / width, float(row) / height });
                if (column + 1 < width && row + 1 < height)
                {
                    const auto top = static_cast<std::uint32_t>(index);
                    const auto bottom = top + terrain.width;
                    indices.insert(indices.end(), { top, bottom, top + 1u, bottom, bottom + 1u, top + 1u });
                }
            }
        }
        auto mesh = std::make_shared<TerrainMesh>(m_name.ToString(), vertices, indices, terrain.width);
        auto material = std::make_shared<TerrainMaterial>();
        material->MateialDataUpdate(width, height, layers, weights);
        std::wstring targetPath = filePath;
        m_width = width;
        m_height = height;
        m_terrainID = terrain.terrainId;
        m_minHeight = terrain.minHeight;
        m_maxHeight = terrain.maxHeight;
        m_heightMap = std::move(heights);
        m_vNormalMap = std::move(normals);
        m_layerHeightMap = std::move(weights);
        m_layers = std::move(layers);
        m_nextLayerID = nextLayerId;
        m_pTerrainMesh = std::move(mesh);
        m_pMaterial = std::move(material);
        m_terrainTargetPath = std::move(targetPath);
        PublishRenderProxyDirty(ProxyDirty::Material | ProxyDirty::Payload);
        return true;
    }
    catch (const std::exception& error)
    {
        Debug::PrintLog(spdlog::level::err, "Terrain artifact load failed: " + std::string(error.what()));
        return false;
    }
}


void TerrainComponent::OnDeserialized()
{
	// CT6-d: 구 ComponentFactory 분기 이동 — m_trrainAssetGuid는 반영 멤버라
	// typed 역직렬화가 이미 채웠다.
	if (m_trrainAssetGuid != nullFileGuid)
	{
		auto path = DataSystems->GetFilePath(m_trrainAssetGuid);
		Load(path);
	}
	else
	{
		Debug::PrintLog(spdlog::level::err, "Terrain component is missing m_trrainAssetGuid");
	}
}

