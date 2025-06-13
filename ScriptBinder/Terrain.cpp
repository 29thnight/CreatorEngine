#include "Terrain.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"



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
	fs::path assetPath = fs::path(assetRoot) / L"Assets";
	fs::path terrainPath = assetPath / L"Terrain";
	fs::path difusePath = terrainPath / L"Texture";
	if (!fs::exists(difusePath)) {
		fs::create_directories(difusePath);
	}

	fs::path terrainDir = PathFinder::Relative("Terrain");




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
		float n = (m_heightMap[i] - minH) / (maXH - minH);
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

	out.reserve(width * height); // Reserve space for output vector

	for (int i = 0; i < width * height; ++i) {
		float n = data[i] / 65535.0f; // Normalize to [0, 1]
		out[i] = n * (maXH - minH) + minH; // Scale to [minH, maXH]
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

			
			////m_threadPool 전에 검증 부터
			//if (!fs::exists(destPath)) {
			//	std::error_code ec;
			//	fs::copy_file(
			//		originalPath, destPath,
			//		fs::copy_options::overwrite_existing | fs::copy_options::skip_existing,
			//		ec
			//	);
			//	if (ec) {
			//		Debug->LogError("Failed to copy layer texture: " + Utf8Encode(originalPath) + " to " + Utf8Encode(destPath));
			//	}
			//	else {
			//		Debug->Log("Layer texture copied: " + Utf8Encode(originalPath) + " to " + Utf8Encode(destPath));
			//	}
			//}
			//else {
			//	Debug->Log("Layer texture already exists: " + Utf8Encode(destPath));
			//}


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
