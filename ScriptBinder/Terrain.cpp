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


//utill :wsting->utf8 string 나중에 utill쪽으로 빼는거 생각중
static std::string Utf8Encode(const std::wstring& wstr) {
	int size_needed = WideCharToMultiByte(
		CP_UTF8, 0,
		wstr.c_str(), (int)wstr.size(),
		nullptr, 0, nullptr, nullptr);
	std::string str(size_needed, 0);
	WideCharToMultiByte(
		CP_UTF8, 0,
		wstr.c_str(), (int)wstr.size(),
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
	std::wstring metaPath = (terrainPath / (name + L".terrain")).wstring();

	
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
	metaData["assetGuid"] = m_trrainAssetGuid.ToString();
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
	std::ofstream ofs(metaPath);
	ofs << metaData.dump(4); // 4칸 들여쓰기


	std::wcout << L"Terrain saved to: " << metaPath << std::endl;
	Debug->LogDebug("Terrain saved to: " + Utf8Encode(metaPath));
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
	std::swap(m_layerHeightMap, tmpLayerHeightMap);
	InitSplatMapTexture(m_width, m_height); // 스플랫맵 텍스처 초기화
	UpdateSplatMapPatch(0, 0, m_width, m_height); // 스플랫맵 패치 업데이트
	std::swap(m_layers, tmpLayerDescs);
	m_nextLayerID = tmpNextLayerID;
	m_selectedLayerID = 0xFFFFFFFF; // 선택된 레이어 초기화
	LoadLayers();	


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
	std::vector<uint16_t> buffer(w * h);

	for (int i = 0; i < w * h; ++i) {
		float n = (m_heightMap[i] - minH) / (maXH - minH);   //H-(-100) / 500-(-100);
		buffer[i] = static_cast<uint16_t>(std::clamp(n, 0.0f, 1.0f) * 65535.0f);
	}
	//utf8 경로	변환
	auto utf8Path = Utf8Encode(pngPath);
	if (
		stbi_write_png(
			utf8Path.c_str(),
			w, h,
			2,
			buffer.data(),
			w * sizeof(uint16_t)
		) == 0) {
		//저장 실패시
		Debug->LogError("Failed to save height map to PNG: " + utf8Path);
		throw std::runtime_error("Failed to save height map to PNG: " + utf8Path);
	}
}

bool TerrainComponent::LoadEditorHeightMap(std::filesystem::path& pngPath,float dataWidth,float dataHeight, float minH, float maXH, std::vector<float>& out)
{
	
    int width, height, channels;  
	auto path = pngPath.string();
    uint16_t* data = (uint16_t*)stbi_load_16(path.c_str(), &width, &height, &channels, 2);

    if (!data || width!= dataWidth ||height!= dataHeight)
    {  
		Debug->LogError("Failed to load height map from PNG: " + path);
		if (data) {
			stbi_image_free(data);
		}
		return false;
    }  

	size_t N = size_t(width) * height;
	size_t DoubleN = N * 2;
	//out.reserve(width * height); // Reserve space for output vector
	out.resize(DoubleN); // Resize to fit the height map data

	for (int i = 0; i < DoubleN; ++i) {
		float n = data[i] / 65535.0f; // Normalize to [0, 1] todo :체널 나눠라
		out[i] = n; // Scale to [minH, maXH]
	}
    
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

void TerrainComponent::Awake()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->m_ActiveRenderScene;
	if (scene)
	{
		scene->CollectTerrainComponent(this);
	}
}

void TerrainComponent::OnDistroy()
{
	auto scene = SceneManagers->GetActiveScene();
	auto renderScene = SceneManagers->m_ActiveRenderScene;
	if (scene)
	{
		scene->UnCollectTerrainComponent(this);
	}
}

void TerrainComponent::TestSaveLayerTexture(const std::wstring& saveDir)
{
	namespace fs = std::filesystem;
	//auto utf8SaveDir = Utf8Encode(saveDir);

	//레이어에 사용되었던 텍스쳐들 복사
	for (const auto& layer : m_layers)
	{
		//diffuseTexturePath가 비어있으면 건너뜀
		if (layer.diffuseTexturePath.empty()) {
			continue;
		}

		fs::path originalPath = layer.diffuseTexturePath;

		if (fs::exists(originalPath))
		{

			fs::path destPath = saveDir / originalPath.filename();
			
			//폴더가 없으면 생성
			if (!fs::exists(destPath.parent_path())) {
				fs::create_directories(destPath.parent_path());
			}
			

			////이미 존제하면 복	사하지 않음
			if (!fs::exists(destPath)) {
				m_threadPool.Enqueue(
					[src = originalPath, dst = destPath]()
					{
						std::error_code ec;
						fs::copy_file(
							src, dst,
							fs::copy_options::overwrite_existing | fs::copy_options::skip_existing,
							ec
						);

						if (ec) {
							Debug->LogError("Failed to copy layer texture: " + Utf8Encode(src) + " to " + Utf8Encode(dst));
						}
						else {
							Debug->Log("Layer texture copied: " + Utf8Encode(src) + " to " + Utf8Encode(dst));
						}
					}
				);
			}

		}

	}
	m_threadPool.NotifyAllAndWait();
	Debug->Log("end");
}

void TerrainComponent::loatFromPng(const std::string& filepath, std::vector<std::vector<float>>& outLayerWeights, int& outWidth, int& outHeights) {
	int width, height, channels;
	unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
	if (!data) {
		throw std::runtime_error("Failed to load image: " + filepath);
	}
	outWidth = width;
	outHeights = height;
	// 높이 맵 초기화
	m_heightMap.resize(width * height);
	m_vNormalMap.resize(width * height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });
	// 레이어 가중치 초기화
	outLayerWeights.resize(m_layers.size());
	for (auto& layer : outLayerWeights) {
		layer.resize(width * height, 0.0f);
	}
	// 픽셀 데이터 처리
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int idx = y * width + x;
			unsigned char* pixel = &data[(y * width + x) * channels];
			// 높이 맵은 R 채널로 설정
			m_heightMap[idx] = pixel[0] / 255.0f;
			// 레이어 가중치 설정 (예시: R=레이어1, G=레이어2 등)
			for (size_t i = 0; i < m_layers.size(); ++i) {
				if (i < channels) {
					outLayerWeights[i][idx] = pixel[i] / 255.0f;
				}
			}
		}
	}
	stbi_image_free(data);
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

	m_layerSRV = arrayTex->m_pSRV;

}
