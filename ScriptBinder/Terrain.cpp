#include "Terrain.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include "Terrain.h"

//utill :wsting->utf8 string ���߿� utill������ ���°� ������
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

	//debug��
	std::wcout << L"Saving dir: " << assetRoot << std::endl;
	std::wcout << L"Saving terrain: " << name << std::endl;


	namespace fs = std::filesystem;
	fs::path assetPath = fs::path(assetRoot) / L"Assets";
	fs::path terrainPath = assetPath / L"Terrain";
	fs::path difusePath = terrainPath / L"Texture";
	if (!fs::exists(difusePath)) {
		fs::create_directories(difusePath);
	}


	std::wstring heightMapPath = (terrainPath / (name + L"_HeightMap.png")).wstring();
	std::wstring splatMapPath = (terrainPath / (name + L"_SplatMap.png")).wstring();
	std::wstring metaPath = (terrainPath / (name + L".meta")).wstring();

	//������� �̹��� ���� ����
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

	//���̾ ���Ǿ��� �ؽ��ĵ� ����
	for (const auto& layer : m_layerDescs) 
	{	
		if (fs::exists(layer.diffuseTexturePath)) 
		{
			fs::path destPath = difusePath / fs::path(layer.diffuseTexturePath).filename();
			//�̹� �����ϸ� ��	������ ����
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
		}
	}

	//������ ���
	m_threadPool.NotifyAllAndWait();

	//��Ÿ������ ���� .meta �δ� json ������
	json metaData;
	metaData["name"] = name;
	metaData["terrainID"] = m_terrainID;
	metaData["assetGuid"] = m_trrainAssetGuid.ToString();
	metaData["width"] = m_width;
	metaData["height"] = m_height;
	metaData["minHeight"] = m_minHeight;
	metaData["maxHeight"] = m_maxHeight;
	metaData["heightmap"] = fs::path(heightMapPath).u8string(); //ASCII����̸� �ѱ� ������ utf8 ��ȯ �ؾ��ҵ�
	metaData["splatmap"] = fs::path(splatMapPath).u8string(); //ASCII����̸� �ѱ� ������ utf8 ��ȯ �ؾ��ҵ�
	metaData["layers"] = json::array();
	for (const auto& layer : m_layerDescs) {
		json layerData;
		layerData["layerID"] = layer.layerID;
		layerData["layerName"] = layer.layerName;
		fs::path reldiffusePath = difusePath / fs::path(layer.diffuseTexturePath).filename(); //�����ص� ���
		layerData["diffuseTexturePath"] = reldiffusePath.u8string(); //ASCII����̸� �ѱ� ������ utf8 ��ȯ �ؾ��ҵ�
		layerData["tilling"] = layer.tilling;
		metaData["layers"].push_back(layerData);
	}
	std::ofstream ofs(metaPath);
	ofs << metaData.dump(4); // 4ĭ �鿩����


	std::wcout << L"Terrain saved to: " << metaPath << std::endl;
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
	//utf8 ���	��ȯ
	auto utf8Path = Utf8Encode(pngPath);
	if (
		stbi_write_png(
			utf8Path.c_str(),
			w, h,
			2,
			buffer.data(),
			w * sizeof(uint16_t)
		) == 0) {
		//���� ���н�
		throw std::runtime_error("Failed to save height map to PNG: " + utf8Path);
	}
}


void TerrainComponent::SaveEditorSplatMap(const std::wstring& pngPath)
{
	int w = m_width;
	int h = m_height;
	std::vector<uint8_t> buffer(w * h * 4, 0); // RGBA 4ä��
	for (int i = 0; i < w * h; ++i) {
		for (size_t j = 0; j < m_layers.size() && j < 4; ++j) { // �ִ� 4�� ���̾ ���
			buffer[i * 4 + j] = static_cast<uint8_t>(std::clamp(m_layerHeightMap[j][i], 0.0f, 1.0f) * 255.0f);
		}
	}
	auto utf8Path = Utf8Encode(pngPath);
	if (
		stbi_write_png(
			utf8Path.c_str(),
			w, h,
			4,
			buffer.data(),
			w * sizeof(uint8_t) * 4
		) == 0) {
		throw std::runtime_error("Failed to save splat map to PNG: " + utf8Path);
	}
}

void TerrainComponent::loatFromPng(const std::string& filepath, std::vector<std::vector<float>>& outLayerWeights, int& outWidth, int& outHeights) {
	int width, height, channels;
	unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
	if (!data) {
		throw std::runtime_error("Failed to load image: " + filepath);
	}
	outWidth = width;
	outHeights = height;
	// ���� �� �ʱ�ȭ
	m_heightMap.resize(width * height);
	m_vNormalMap.resize(width * height, DirectX::XMFLOAT3{ 0.0f, 1.0f, 0.0f });
	// ���̾� ����ġ �ʱ�ȭ
	outLayerWeights.resize(m_layers.size());
	for (auto& layer : outLayerWeights) {
		layer.resize(width * height, 0.0f);
	}
	// �ȼ� ������ ó��
	for (int y = 0; y < height; ++y) {
		for (int x = 0; x < width; ++x) {
			int idx = y * width + x;
			unsigned char* pixel = &data[(y * width + x) * channels];
			// ���� ���� R ä�η� ����
			m_heightMap[idx] = pixel[0] / 255.0f;
			// ���̾� ����ġ ���� (����: R=���̾�1, G=���̾�2 ��)
			for (size_t i = 0; i < m_layers.size(); ++i) {
				if (i < channels) {
					outLayerWeights[i][idx] = pixel[i] / 255.0f;
				}
			}
		}
	}
	stbi_image_free(data);
}
