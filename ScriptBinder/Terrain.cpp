#include "Transform.h"
#include "Terrain.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#pragma pack(push, 1) // 1 byte alignment for DirectX structures
struct TerrainBinHeader {
	uint32_t magic; // 'TRBN'
	uint32_t version; // 1
	uint32_t terrainID; // Unique ID for the terrain
	uint32_t width; // Width of the terrain
	uint32_t height; // Height of the terrain
	float minHeight; // Minimum height value
	float maxHeight; // Maximum height value
	uint32_t layers; // Number of layers in the terrain
};
#pragma pack(pop) // Restore previous alignment


//utill :wsting->utf8 string ���߿� utill������ ���°� ������
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

TerrainComponent::TerrainComponent() : m_threadPool(4) 
{
	m_name = "TerrainComponent";
	m_typeID = TypeTrait::GUIDCreator::GetTypeID<TerrainComponent>();
	Initialize();
}

void TerrainComponent::Initialize()
{
	m_heightMap.assign(m_width * m_height, 0.0f);
	m_vNormalMap.assign(m_width * m_height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });

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
				DirectX::XMFLOAT3((float)j, m_heightMap[idx], (float)i),
				// 노말
				m_vNormalMap[idx],
				// UV0
				DirectX::XMFLOAT2((float)j / (float)m_width, (float)i / (float)m_height)
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
	m_pMesh = new TerrainMesh(
		m_name.ToString(),
		verts,
		indices,
		(uint32_t)m_width
	);


	m_pMaterial = new TerrainMaterial();
	//// TerrainMaterial 초기화 -> 스플랫맵 텍스처 생성
	m_pMaterial->Initialize(m_width, m_height);
}

void TerrainComponent::Resize(int newWidth, int newHeight)
{

	m_width = newWidth;
	m_height = newHeight;
	m_heightMap.assign(m_width * m_height, 0.0f);
	m_vNormalMap.assign(m_width * m_height, { 0.0f, 1.0f, 0.0f });

	// 레이어 가중치 역시 다시 초기화
	for (auto& w : m_layerHeightMap)
		w.assign(m_width * m_height, 0.0f);

	// 2) 기존 메시 해제
	if (m_pMesh) {
		delete m_pMesh;
		m_pMesh = nullptr;
	}

	// 3) 메시 재생성 (initMesh 로직 그대로 재사용)
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
		m_pMesh = new TerrainMesh(m_name.ToString(), verts, indices, (uint32_t)m_width);
	}

	// 4) 스플랫 맵 텍스처 초기화
	m_pMaterial->InitSplatMapTexture(m_width, m_height);
}

void TerrainComponent::ApplyBrush(const TerrainBrush& brush) {
	// 1) 브러시가 닿을 최소/최대 X,Y 계산
	Mathf::Vector3 pivotWorldPos;
	Mathf::Quaternion rot;
	Mathf::Vector3 scale;
	{
		// 월드 매트릭스에서 위치 부분만 분해
		Mathf::Matrix worldMat = GetOwner()->m_transform.GetWorldMatrix();
		worldMat.Decompose(scale, rot, pivotWorldPos);
	}

	// 1) 브러시 월드 위치
	Mathf::Vector3 brushWorldPos{ brush.m_center.x, 0.0f, brush.m_center.y };

	// 2) 로컬 그리드 위치 = 브러시 월드 좌표 – 피벗 월드 좌표
	Mathf::Vector3 localPos = brushWorldPos - pivotWorldPos;
		
	
	int minX = std::max(0, int(localPos.x - brush.m_radius));
	int maxX = std::min(m_width - 1, int(localPos.x + brush.m_radius));
	int minY = std::max(0, int(localPos.z - brush.m_radius));
	int maxY = std::min(m_height - 1, int(localPos.z + brush.m_radius));

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
	m_pMesh->UpdateVertexBufferPatch(
		patchVerts.data(),
		(uint32_t)minX,
		(uint32_t)minY,
		(uint32_t)patchW,
		(uint32_t)patchH
	);


	// 레이어 페인트 splet 맵 업데이트
	
	std::vector<BYTE> materialpatchData(m_width * m_height * 4, 0); // RGBA 4채널
	for (int y = 0; y < m_height; ++y)
	{
		for (int x = 0; x < m_width; ++x)
		{
			int idx = y * m_width + x;
			int dstOffset = (y * m_width + x) * 4; // RGBA 4채널

			// 레이어 가중치 계산
			for (int layerIdx = 0; layerIdx < (int)m_layers.size() && layerIdx < 4; ++layerIdx) // 최대 4개 레이어만 사용
			{
				float w = std::clamp(m_layerHeightMap[layerIdx][idx], 0.0f, 1.0f);
				materialpatchData[dstOffset + layerIdx] = static_cast<BYTE>(w * 255.0f); // R, G, B, A 채널에 가중치 저장
			}
		}
	}

	m_pMaterial->UpdateSplatMapPatch(0, 0, m_width, m_height, materialpatchData); // layer 추가 후 스플랫맵 업데이트
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

			DirectX::XMFLOAT3 normal;
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

void TerrainComponent::PaintLayer(uint32_t layerId, int x, int y, float strength) {
	if (layerId >= m_layers.size()) return; // 유효한 레이어인지 확인
	int idx = y * m_width + x;
	if (idx < 0 || idx >= m_height * m_width) return; // 범위 체크

	// 해당 레이어 가중치 업데이트
	m_layerHeightMap[layerId][idx] += strength;

	float sum = 0.0f;
	for (auto& layer : m_layerHeightMap) {
		sum += layer[idx];
	}
	//오버플로우 처리
	if (sum > 1.0f) {
		// 가중치가 1.0f를 초과하면 오버플로우 발생
		// 편집 하지 않는 각 레이어의 가중치를 비율에 따라 조정
		if (m_layerHeightMap.size() > 1) {
			float overflow = sum - 1.0f / (m_layerHeightMap.size() - 1);
			for (auto& layer : m_layerHeightMap) {
				if (layer != m_layerHeightMap[layerId]) {
					layer[idx] -= overflow;
					if (layer[idx] < 0.0f) {
						layer[idx] = 0.0f; // 음수 방지
					}
				}
			}
		}
		else {
			// 레이어가 하나뿐인 경우, 그냥 1.0f로 설정
			m_layerHeightMap[layerId][idx] = 1.0f;
		}
	}
}

void TerrainComponent::Save(const std::wstring& assetRoot, const std::wstring& name)
{

	//debug용
	std::wcout << L"Saving dir: " << assetRoot << std::endl;
	std::wcout << L"Saving terrain: " << name << std::endl;

	namespace fs = std::filesystem;
	fs::path assetPath = fs::path(assetRoot);
	fs::path terrainDir = PathFinder::Relative("Terrain");
	fs::path terrainPath; 
	//assetPath가 terrainDir 와 같은면 상대 경로로 저장 아니면 풀패스로 저장해야됨
	if (terrainDir == assetPath) {
		terrainPath = fs::relative(assetPath, terrainDir);
	}
	else {
		//존재하지 않으면 풀패스로 저장
		terrainPath = assetPath;
	}
	//터레인 내부 텍스쳐 저장 경로
	fs::path difusePath = terrainPath / L"Texture";

	
	if (!fs::exists(difusePath)) {
		fs::create_directories(difusePath);
	}

	std::wstring heightMapPath = (terrainPath / (name + L"_HeightMap.png")).wstring();
	std::wstring splatMapPath = (terrainPath / (name + L"_SplatMap.png")).wstring();
	m_terrainTargetPath = (terrainPath / (name + L".terrain")).wstring();

	
	//SaveEditorHeightMap(heightMapPath, m_minHeight, m_maxHeight);
	//SaveEditorSplatMap(splatMapPath);
	//스레드로 이미지 저장 부터
	m_threadPool.Enqueue(
		[this, heightMapPath]() 
		{
			SaveEditorHeightMap(heightMapPath, m_minHeight, m_maxHeight);
		}
	);
	m_threadPool.Enqueue(
		[this, splatMapPath]() 
		{
			SaveEditorSplatMap(splatMapPath);
		}
	);

	std::vector<fs::path> diffuseTexturePaths;

	//레이어에 사용되었던 텍스쳐들 복사
	for (const auto& layer : m_layers)
	{	
		if (fs::exists(layer.diffuseTexturePath)) 
		{
			fs::path destPath = difusePath / fs::path(layer.diffuseTexturePath).filename();
			//이미 존제하면 복	사하지 않음
			if (!fs::exists(destPath)) {
				m_threadPool.Enqueue(
					[src = layer.diffuseTexturePath, dst = destPath.wstring()]()
					{
						std::error_code ec;
						fs::copy_file(
							src,dst,
							fs::copy_options::overwrite_existing | fs::copy_options::skip_existing,
							ec
						);
					}
				);
			}
			diffuseTexturePaths.push_back(destPath);
		}
	}

	//스레드 대기
	m_threadPool.NotifyAllAndWait();

	//풀페스 저장 하면 다른 사람이 쓰김 힘듬 상대경로 쓸레
	fs::path relheightMap = fs::relative(heightMapPath, terrainDir);
	fs::path relsplatMap = fs::relative(splatMapPath, terrainDir);

	//메타데이터 저장 .meta 인대 json 쓸거임
	json metaData;
	metaData["name"] = name;
	metaData["terrainID"] = m_terrainID;
	metaData["width"] = m_width;
	metaData["height"] = m_height;
	metaData["minHeight"] = m_minHeight;
	metaData["maxHeight"] = m_maxHeight;
	//metaData["heightmap"] = fs::path(heightMapPath).u8string(); //ASCII경로이면 한글 쓰려면 utf8 변환 해야할듯
	
	metaData["heightmap"] = Utf8Encode(relheightMap);
	//metaData["splatmap"] = fs::path(splatMapPath).u8string(); //ASCII경로이면 한글 쓰려면 utf8 변환 해야할듯
	metaData["splatmap"] = Utf8Encode(relsplatMap);

	metaData["layers"] = json::array();
	int index = 0;
	for (const auto& layer : m_layers) {
		json layerData;
		layerData["layerID"] = layer.m_layerID;
		layerData["layerName"] = layer.layerName;
		fs::path reldiffusePath = fs::relative(diffuseTexturePaths[index], terrainDir); //복사해둔 경로
		//layerData["diffuseTexturePath"] = reldiffusePath.u8string(); //ASCII경로이면 한글 쓰려면 utf8 변환 해야할듯
		layerData["diffuseTexturePath"]= Utf8Encode(reldiffusePath);
		layerData["tilling"] = layer.tilling;
		metaData["layers"].push_back(layerData);
		index++;
	}

	std::ofstream ofs(m_terrainTargetPath);
	ofs << metaData.dump(4); // 4ĭ �鿩����

	std::wcout << L"Terrain saved to: " << m_terrainTargetPath << std::endl;
	Debug->LogDebug("Terrain saved to: " + Utf8Encode(m_terrainTargetPath));

	ofs.flush();
	std::this_thread::sleep_for(std::chrono::seconds(1));

	file::path metaPath = m_terrainTargetPath + L".meta";
	if (file::exists(metaPath))
	{
		auto node = MetaYml::LoadFile(metaPath.string());

		if (node["guid"] && !node["guid"].IsNull())
		{
			FileGuid fileGuid = node["guid"].as<std::string>();
			if (fileGuid != nullFileGuid)
			{
				m_trrainAssetGuid = fileGuid;
			}
		}
	}

}

bool TerrainComponent::Load(const std::wstring& filePath)
{
	//debug용
	Debug->LogDebug("Loading terrain from: " + Utf8Encode(filePath));

	//세이브 역순으로 메타데이터 부터 읽어 오는대 filePath로 .meta 파일 읽어옴
	//.meta
	namespace fs = std::filesystem;
	fs::path metaPath = filePath;

	if (!fs::exists(metaPath)) {
		Debug->LogError("Terrain meta file does not exist: " + Utf8Encode(metaPath.wstring()));
		return false;
	}

	//metaPath가 예정된 경로가 아니면 풀패스로 사용 아니면 상대경로로 사용 //예정된 경로는 PathFinder::Relative("Terrain") 이다.
	bool isRelative = false;
	fs::path rel = PathFinder::Relative("Terrain");
	if (metaPath.parent_path() != rel) {
		//풀패스로 사용
		isRelative = false;
	}
	else {
		//상대경로로 사용
		isRelative = true;
	}




	//.meta -> json 읽기

	std::ifstream ifs(metaPath);
	json metaData;
	try {
		ifs >> metaData;
	}
	catch (const json::parse_error& e) {
		Debug->LogError("Failed to parse terrain meta file: " + Utf8Encode(metaPath.wstring()) + " Error: " + e.what());
		return false;
	}

	//임시저장변수 선언
	uint32_t tmpTerrainID = metaData["terrainID"].get<uint32_t>();
	int tmpWidth = metaData["width"].get<int>();
	int tmpHeight = metaData["height"].get<int>();
	fs::path heightMapPath = fs::path(metaData["heightmap"].get<std::string>());
	fs::path splatMapPath = fs::path(metaData["splatmap"].get<std::string>());
	auto tmpHeightMap = std::vector<float>(tmpWidth * tmpHeight, 0.0f);
	auto tmpLayerHeightMap = std::vector<std::vector<float>>(4, std::vector<float>(tmpWidth * tmpHeight, 0.0f)); //4개 레이어로 초기화
	auto tmpLayerDescs = std::vector<TerrainLayer>();

	heightMapPath = isRelative ? PathFinder::TerrainSourcePath(heightMapPath.string()) : heightMapPath; //상대경로로 변환
	splatMapPath = isRelative ? PathFinder::TerrainSourcePath(splatMapPath.string()) : splatMapPath; //상대경로로 변환

	//헤이트맵 임시저장
	LoadEditorHeightMap(heightMapPath, tmpWidth, tmpHeight,  metaData["minHeight"].get<float>(), metaData["maxHeight"].get<float>(), tmpHeightMap);
	//스플랫맵 임시저장
	LoadEditorSplatMap(splatMapPath, tmpWidth, tmpHeight, tmpLayerHeightMap);

	float tmpNextLayerID = 0;
	for (const auto& layerData : metaData["layers"]) {
		TerrainLayer desc;
		desc.m_layerID = layerData["layerID"].get<uint32_t>();
		desc.layerName = layerData["layerName"].get<std::string>();
		auto diffusePath = fs::path(layerData["diffuseTexturePath"].get<std::string>());
		diffusePath = isRelative ? PathFinder::TerrainSourcePath(diffusePath.string()) : diffusePath; //상대경로로 변환
		desc.diffuseTexturePath = diffusePath;
		desc.tilling = layerData["tilling"].get<float>();
		
		//diffuseTexture 로드
		ID3D11Resource* diffuseResource = nullptr;
		ID3D11Texture2D* diffuseTexture = nullptr;
		ID3D11ShaderResourceView* diffuseSRV = nullptr;
		if (CreateTextureFromFile(DeviceState::g_pDevice, desc.diffuseTexturePath, &diffuseResource, &diffuseSRV) == S_OK) {
			diffuseResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&diffuseTexture));
			TerrainLayer newLayer;
			desc.diffuseTexture = diffuseTexture;
			desc.diffuseSRV = diffuseSRV;
			//m_layerHeightMap.push_back(std::vector<float>(tmpWidth * tmpHeight, 0.0f)); // 레이어별 높이 맵 초기화 => loadLoadEditorSplatMap() 이미 로드
		}
		else {
			Debug->LogError("Failed to load diffuse texture: " + diffusePath.string());
			continue; // 로드 실패시 해당 레이어는 무시
		}

		//레이어 정보 저장
		tmpLayerDescs.push_back(desc);
		tmpNextLayerID++;
	}

	//임시저장 변수 스왑 및
	//바뀐 맴버 정보로 메쉬 및 버퍼,텍스쳐 업데이트
	std::swap(m_terrainID, tmpTerrainID);
	std::swap(m_width, tmpWidth);
	std::swap(m_height, tmpHeight);
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
	m_pMesh->UpdateVertexBuffer(
		patchVerts.data(),
		(uint32_t)m_width *
		(uint32_t)m_height
	); // 높이맵 변경 후 메쉬 업데이트
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
	for (auto& layer : tmpLayerDescs) {
		if (layer.diffuseTexture) {
			layer.diffuseTexture->Release();
			layer.diffuseTexture = nullptr;
		}
		if (layer.diffuseSRV) {
			layer.diffuseSRV->Release();
			layer.diffuseSRV = nullptr;
		}
	}
	tmpLayerDescs.clear();

}

void TerrainComponent::SaveEditorHeightMap(const std::wstring& pngPath, float minH, float maXH)
{
	int w = m_width;
	int h = m_height;
	std::vector<uint8_t> buf(w * h * 4);

	for (int i = 0; i < w * h; ++i)
	{
		float   f = m_heightMap[i];
		uint32_t bits;
		static_assert(sizeof(float) == 4, "float must be 32-bit");
		std::memcpy(&bits, &f, sizeof(bits));  // float 비트열을 uint32_t 로 복사

		// 빅엔디안 순서로 채널에 저장 (R=MSB, A=LSB)
		buf[i * 4 + 0] = uint8_t((bits >> 24) & 0xFF);
		buf[i * 4 + 1] = uint8_t((bits >> 16) & 0xFF);
		buf[i * 4 + 2] = uint8_t((bits >> 8) & 0xFF);
		buf[i * 4 + 3] = uint8_t((bits) & 0xFF);
	}

	auto pathUtf8 = Utf8Encode(pngPath);
	// comp=4, stride = w*4 bytes
	if (!stbi_write_png(pathUtf8.c_str(), w, h, 4, buf.data(), w * 4))
	{
		throw std::runtime_error("Failed to save 32bit heightmap PNG: " + pathUtf8);
	}

}

bool TerrainComponent::LoadEditorHeightMap(std::filesystem::path& pngPath,float dataWidth,float dataHeight, float minH, float maXH, std::vector<float>& out)
{
	
	int width, height, channels;
	auto pathUtf8 = pngPath.string();
	
	// comp=4 for RGBA8
	uint8_t* data = stbi_load(pathUtf8.c_str(), &width, &height, &channels, 4);
	if (!data)
		return false;

	size_t N = static_cast<size_t>(width) * height;
	out.resize(N);

	for (size_t i = 0; i < N; ++i)
	{
		uint32_t b0 = data[i * 4 + 0];
		uint32_t b1 = data[i * 4 + 1];
		uint32_t b2 = data[i * 4 + 2];
		uint32_t b3 = data[i * 4 + 3];
		uint32_t bits = (b0 << 24) | (b1 << 16) | (b2 << 8) | b3;

		float f;
		std::memcpy(&f, &bits, sizeof(f));
		out[i] = f;
	}
	
	//float normalized = (static_cast<float>(data[i * channels + c]) / 100.0f) + minH; // 0 ~ 1 범위로 정규화
	//out[i] = normalized;

    stbi_image_free(data);  
    return true;  
    
}


void TerrainComponent::SaveEditorSplatMap(const std::wstring& pngPath)
{
	int w = m_width;
	int h = m_height;
	std::vector<uint8_t> buffer(w * h * 4, 0); // RGBA 4채널
	for (int i = 0; i < w * h; ++i) {
		for (int c = 0; c<4&& c < m_layers.size(); ++c){
			buffer[i * 4 + c] = static_cast<uint8_t>(std::clamp(m_layerHeightMap[c][i], 0.0f, 1.0f) * 255.0f);
		}
	}
	auto utf8Path = Utf8Encode(pngPath);
	if (
		stbi_write_png(
			utf8Path.c_str(),
			w, h,
			4,
			buffer.data(),
			w * 4
		) == 0) {
		throw std::runtime_error("Failed to save splat map to PNG: " + utf8Path);
	}
}


bool TerrainComponent::LoadEditorSplatMap(std::filesystem::path& pngPath, float dataWidth, float dataHeight, std::vector<std::vector<float>>& out)
{
	int width, height, channels;
	auto path = pngPath.string();
	unsigned char* data = stbi_load(path.c_str(), &width, &height, &channels, 4);
	if (!data || width != dataWidth || height != dataHeight) {
		Debug->LogError("Failed to load splat map from PNG: " + path);
		if (data) {
			stbi_image_free(data);
		}
		return false;
	}
	out.resize(4);
	for (size_t i = 0; i < 4; ++i) {
		out[i].resize(width * height, 0.0f);

		for (int y = 0; y < height; ++y) {
			for (int x = 0; x < width; ++x) {
				int idx = y * width + x;
				unsigned char* pixel = &data[(y * width + x) * channels];
				if (i < channels) {
					out[i][idx] = pixel[i] / 255.0f; // R, G, B, A 채널에 가중치 저장
				}
			}
		}

	}

	stbi_image_free(data);
	return true;


}

void TerrainComponent::UpdateLayerDesc(uint32_t layerID)
{
	TerrainLayerBuffer layerBuffer;
	if (m_layers.size() > 0)
	{
		layerBuffer.useLayer = true;
	}

	float tilefector[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
	for (int i = 0; i < m_layers.size(); ++i)
	{
		if (i < m_layers.size())
		{
			tilefector[i] = m_layers[i].tilling;
		}
		else
		{
			tilefector[i] = 1.0f; // 기본값
		}
	}

	//tilefector[layerID] = newDesc.tilling; // 업데이트된 타일링 값
	//layerBuffer.layerTilling = DirectX::XMFLOAT4(tilefector[0] / 4096, tilefector[1] / 4096, tilefector[2] / 4096, tilefector[3] / 4096);

	layerBuffer.layerTilling0 = tilefector[0];
	layerBuffer.layerTilling1 = tilefector[1];
	layerBuffer.layerTilling2 = tilefector[2];
	layerBuffer.layerTilling3 = tilefector[3];

	m_pMaterial->UpdateBuffer(layerBuffer);
}

void TerrainComponent::Awake()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->CollectTerrainComponent(this);
	}
}

void TerrainComponent::OnDistroy()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->GetRenderScene();
	if (scene)
	{
		scene->UnCollectTerrainComponent(this);
	}
}

void TerrainComponent::AddLayer(const std::wstring& path, const std::wstring& diffuseFile, float tilling)
{

	TerrainLayer newLayer;
	newLayer.m_layerID = m_nextLayerID++;
	newLayer.tilling = tilling;
	newLayer.layerName = std::string(diffuseFile.begin(), diffuseFile.end());
	newLayer.diffuseTexturePath = path;
	newLayer.tilling = tilling;
	// diffuseTexture 로드

	m_pMaterial->AddLayer(newLayer); // 머티리얼에 레이어 추가
	m_layers.push_back(newLayer);
	m_layerHeightMap.push_back(std::vector<float>(m_width * m_height, 0.0f));
	std::vector<BYTE> splatMapData(m_width * m_height * 4, 0); // RGBA 4채널 초기화

	for (int y = 0; y < m_height; ++y)
	{
		for (int x = 0; x < m_width; ++x)
		{
			int idx = y * m_width + x;
			int dstOffset = (y * m_width + x) * 4; // RGBA 4채널

			// 레이어 가중치 계산
			for (int layerIdx = 0; layerIdx < (int)m_layers.size() && layerIdx < 4; ++layerIdx) // 최대 4개 레이어만 사용
			{
				float w = std::clamp(m_layerHeightMap[layerIdx][idx], 0.0f, 1.0f);
				splatMapData[dstOffset + layerIdx] = static_cast<BYTE>(w * 255.0f); // R, G, B, A 채널에 가중치 저장
			}
		}
	}

	m_pMaterial->UpdateSplatMapPatch(0, 0, m_width, m_height, splatMapData); // layer 추가 후 스플랫맵 업데이트
}

void TerrainComponent::RemoveLayer(uint32_t layerID)
{
	if (layerID >= m_layers.size()) {
		Debug->LogError("Invalid layer ID: " + std::to_string(layerID));
		return;
	}
	// 레이어 제거
	m_layers.erase(m_layers.begin() + layerID);
	m_layerHeightMap.erase(m_layerHeightMap.begin() + layerID);
	// 레이어 ID 업데이트
	for (uint32_t i = layerID; i < m_layers.size(); ++i) {
		m_layers[i].m_layerID = i;
	}
	// 다음 레이어 ID 업데이트
	if (m_nextLayerID > layerID) {
		m_nextLayerID--;
	}
	UpdateLayerDesc(layerID);
}

void TerrainComponent::ClearLayers()
{
	m_layers.clear();
	m_layerHeightMap.clear();
	m_nextLayerID = 0;
	
	m_pMaterial->ClearLayers(); // 머티리얼에서 레이어 제거
}


void TerrainComponent::BuildOutTrrain(const std::wstring& buildPath, const std::wstring& terrainName)
{
	//build시 name.tbin
	

	//debug용
	Debug->LogDebug("Building terrain: " + Utf8Encode(terrainName) + " at " + Utf8Encode(buildPath));
	//빌드 경로가 존재하지 않으면 생성
	namespace fs = std::filesystem;
	fs::path outDir = fs::path(buildPath) / L"Assets" / L"Terrain";
	if (!fs::exists(outDir)) {
		fs::create_directories(outDir);
	}
	//빌드 경로에 .tbin 파일 저장
	fs::path terrainFile = outDir / (terrainName + L".tbin");
	std::ofstream ofs(terrainFile, std::ios::binary);
	if (!ofs) {
		Debug->LogError("Failed to open terrain file for writing: " + Utf8Encode(terrainFile.wstring()));
		return;
	}

	TerrainBinHeader header;
	header.magic = 0x5442524E; // 'TRBN'
	header.version = 1;
	header.terrainID = m_terrainID;
	header.width = m_width;
	header.height = m_height;
	header.minHeight = m_minHeight;
	header.maxHeight = m_maxHeight;
	header.layers = static_cast<uint32_t>(m_layers.size());
	//헤더 쓰기
	ofs.write(reinterpret_cast<const char*>(&header), sizeof(header));
	//높이맵 쓰기
	ofs.write(reinterpret_cast<const char*>(m_heightMap.data()), sizeof(float)*m_width*m_height);
	//레이어별 가중치 쓰기
	{
		size_t N = size_t(m_width) * size_t(m_height);
		std::vector<uint8_t> buf(N * 4);
		for (size_t i = 0;  i < N; ++i) {
			for (int c = 0; c < 4;++c) {
				buf[i * 4 + c] = static_cast<uint8_t>(std::clamp(m_layerHeightMap[c][i], 0.0f, 1.0f) * 255.0f);
			}
		}
		ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
	}

	//레이어의 텍스쳐 이름만 기록 런타임에서 리소스 메니저에 없다면 빈 텍스쳐로 로드
	std::vector<std::string> textureNames;
	textureNames.reserve(m_layers.size());

	for (const auto& layer : m_layers) {
		if (!layer.diffuseTexturePath.empty()) {
			fs::path diffuseName = layer.diffuseTexturePath;
			textureNames.push_back(Utf8Encode(diffuseName.filename()));
		}
	}

	std::vector<uint32_t> offsets(m_layers.size());
	uint32_t cursor = 0;
	for (uint32_t i = 0; i < m_layers.size(); ++i) {
		offsets[i] = cursor;
		cursor += static_cast<uint32_t>(textureNames[i].size()) + 1; // +1 for null terminator
	}

	//오프셋 쓰기
	ofs.write(reinterpret_cast<const char*>(offsets.data()), sizeof(uint32_t)*header.layers);

	//텍스쳐 이름 쓰기
	for (const auto& name : textureNames) {
		ofs.write(name.c_str(), name.size());
		ofs.put('\0'); // null terminator
	}

	ofs.close();



	Debug->LogDebug("Terrain built successfully: " + Utf8Encode(terrainFile.wstring()));
}

bool TerrainComponent::LoadRunTimeTerrain(const std::wstring& filePath)
{
	//debug용
	Debug->LogDebug("Loading runtime terrain from: " + Utf8Encode(filePath));
	namespace fs = std::filesystem;
	fs::path terrainPath = filePath;
	if (!fs::exists(terrainPath)) {
		Debug->LogError("Terrain file does not exist: " + Utf8Encode(terrainPath.wstring()));
		return false;
	}
	std::ifstream ifs(terrainPath, std::ios::binary);
	if (!ifs) {
		Debug->LogError("Failed to open terrain file for reading: " + Utf8Encode(terrainPath.wstring()));
		return false;
	}

	//파일 헤더 읽기
	TerrainBinHeader header;
	ifs.read(reinterpret_cast<char*>(&header), sizeof(header));
	if (header.magic != 0x5442524E || header.version != 1) {
		Debug->LogError("Invalid terrain file format: " + Utf8Encode(terrainPath.wstring()));
		return false;
	}
	m_terrainID = header.terrainID;
	m_width = header.width;
	m_height = header.height;
	m_minHeight = header.minHeight;
	m_maxHeight = header.maxHeight;
	size_t N = size_t(m_width) * size_t(m_height);

	//높이맵
	m_heightMap.assign(N, 0.0f);
	ifs.read(reinterpret_cast<char*>(m_heightMap.data()), sizeof(float) * N);
	
	//스플렛맵
	std::vector<uint8_t> splatMapData(N * 4); // RGBA 4채널
	ifs.read(reinterpret_cast<char*>(splatMapData.data()), splatMapData.size());
	m_layerHeightMap.assign(header.layers, std::vector<float>(N));
	for (size_t i = 0; i < N; ++i) {
		for (int c = 0; c < header.layers; ++c) {
			m_layerHeightMap[c][i] = splatMapData[i * 4 + c] / 255.0f; // RGBA 채널에서 가중치 추출
		}
	}

	//path offset 
	std::vector<uint32_t> textureOffsets(header.layers);
	ifs.read(reinterpret_cast<char*>(textureOffsets.data()), sizeof(uint32_t) * header.layers);

	//텍스쳐 이름 읽기
	std::streampos cur = ifs.tellg();
	ifs.seekg(0, std::ios::end);
	size_t remain = static_cast<size_t>(ifs.tellg() - cur);
	ifs.seekg(cur);

	std::vector<char> namesBuf(remain);
	ifs.read(namesBuf.data(), remain);

	std::vector<std::string> textureNames(header.layers);

	m_layers.clear();
	for (uint32_t i = 0; i < header.layers; ++i) {
		uint32_t offset = textureOffsets[i];
		const char* namePtr = namesBuf.data() + offset;
		textureNames[i] = std::string(namePtr); // null terminator로 자동 종료됨
	}

	UINT width = 512;
	UINT height = 512;
	DXGI_FORMAT format = DXGI_FORMAT_R32G32B32A32_FLOAT;

	auto arrayTex = Texture::CreateArray(
		width, height, "layerArray", format, D3D11_BIND_SHADER_RESOURCE, 4, nullptr
	);

	for (UINT slice = 0; slice < header.layers; ++slice)
	{
		auto layerTex = DataSystems->LoadTexture(textureNames[slice]);
		ID3D11Texture2D* src = layerTex->m_pTexture;
		ID3D11Texture2D* dst = arrayTex->m_pTexture;

		UINT dstSub = D3D11CalcSubresource(0, slice, 1);
		DeviceState::g_pDeviceContext->CopySubresourceRegion(
			dst, dstSub, 0, 0, 0,
			src, 0, nullptr
		);
	}


	arrayTex->CreateSRV(format,D3D11_SRV_DIMENSION_TEXTURE2DARRAY,1);

	m_pMaterial->m_layerSRV = arrayTex->m_pSRV; // 머티리얼에 레이어 배열 텍스처 설정 -> 바꿔야 할듯
}
