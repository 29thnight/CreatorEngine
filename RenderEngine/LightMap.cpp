#include "LightMap.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "LightProperty.h"
#include "Light.h"
#include "Renderer.h"
#include "RenderScene.h"
#include "Material.h"
#include "Core.Random.h"
#include "LightmapShadowPass.h"
#include "PositionMapPass.h"
namespace lm {
	struct alignas(16) CBData {
		int2 Offset;
		int2 Size;
		bool32 useAo;
	};

	struct CBTransform {
		Mathf::Matrix worldMat;
	};

	struct alignas(16) CBLight {
		Mathf::Matrix view;
		Mathf::Matrix proj;

		Mathf::Vector4 position;
		Mathf::Vector4 direction;
		Mathf::Color4  color;

		float constantAtt;
		float linearAtt;
		float quadAtt;
		float spotAngle;

		int lightType;
		int status;
	};

	struct alignas(16) CBSetting {
		float bias;
		int lightsize;
		int2 shadowmapSize;
		DirectX::XMFLOAT4 globalAmbient;
		bool32 useEnvMap;
	};

	LightMap::LightMap()
	{
	}
	LightMap::~LightMap()
	{
		delete sample;
	}

	void LightMap::Initialize()
	{
		m_computeShader = &ShaderSystem->ComputeShaders["Lightmap"];
		m_edgeComputeShader = &ShaderSystem->ComputeShaders["NeighborSampling"];
		m_edgeCoverComputeShader = &ShaderSystem->ComputeShaders["LightmapEdgeCover"];
		m_MSAAcomputeShader = &ShaderSystem->ComputeShaders["MSAA"];

		m_Buffer = DirectX11::CreateBuffer(sizeof(CBData), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_transformBuf = DirectX11::CreateBuffer(sizeof(CBTransform), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		//m_lightBuf = DirectX11::CreateBuffer(sizeof(CBLight), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_settingBuf = DirectX11::CreateBuffer(sizeof(CBSetting), D3D11_BIND_CONSTANT_BUFFER, nullptr);

		sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
		pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);


	}

	void LightMap::Prepare()
	{
		ClearLightMaps();

		edgeTexture = Texture::Create(
			canvasSize,
			canvasSize,
			"Lightmap",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		edgeTexture->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		edgeTexture->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		rects.clear();

		for (auto& obj : m_renderscene->GetScene()->m_SceneObjects) {
			auto* renderer = obj->GetComponent<MeshRenderer>();
			if (renderer == nullptr) continue;
			if (!renderer->IsEnabled()) continue;
			if (renderer->m_Mesh == nullptr)continue;

			// 해상도 push.
			Rect r;
			float size = rectSize * renderer->m_LightMapping.lightmapScale;
			r.w = size;
			r.h = size;

			r.data = renderer;
			r.worldMat = obj->m_transform.GetWorldMatrix();
			renderer->m_LightMapping.ligthmapResolution = size;
			renderer->m_LightMapping.lightmapTiling.x = size / canvasSize;
			renderer->m_LightMapping.lightmapTiling.y = size / canvasSize;
			rects.push_back(r);
		}
	}
	void LightMap::TestPrepare()
	{
		for (int i = 0; i < 50; i++) {

			Rect r;
			r.w = Random<float>(160, 240).Generate();
			r.h = r.w;//Random<float>(160, 240).Generate();
			rects.push_back(r);
		}
		//testMeshTextures = m_scene->GetSceneObject(1)->m_meshRenderer.m_Material->m_pBaseColor;

		CreateLightMap();
	}
	void LightMap::PrepareRectangles()
	{
		// rects에 각 메쉬의 해상도를 넣어줌


		// 정렬 (높이 오름차순)
		std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) {
			return a.h < b.h;
			});

		CreateLightMap();
	}

	void LightMap::CalculateRectangles()
	{
		int useSpace = 0;
		Rect nextPoint = { 0, 0, 0, 0 };
		int maxHeight = 0;
		int i = rects.size();
		int j = 0;
		int lightmapIndex = 0;

		while (i > 0) {
			if (rects[j].w + padding <= canvasSize - nextPoint.w) {
				rects[j].x = nextPoint.w;   // 현재 x 위치
				rects[j].y = nextPoint.h;  // 현재 y 위치

				nextPoint.w += rects[j].w + padding;
				maxHeight = std::max(maxHeight, rects[j].h);
				useSpace += rects[j].w * rects[j].h;
				rects[j].x = nextPoint.w - rects[j].w;
				rects[j].y = nextPoint.h + padding;

				MeshRenderer* renderer = static_cast<MeshRenderer*>(rects[j].data);
				renderer->m_LightMapping.lightmapIndex = lightmapIndex;
				renderer->m_LightMapping.lightmapOffset.x = rects[j].x / (float)canvasSize;
				renderer->m_LightMapping.lightmapOffset.y = rects[j].y / (float)canvasSize;

				j++;
				i--;
			}
			else {
				nextPoint.w = 0;
				nextPoint.h += maxHeight + padding;
				maxHeight = 0;
			}

			// 추가할 rect가 size를 초과하면 새로운 lightmap을 만들고 다시 배치.
			if (nextPoint.h + maxHeight + padding > canvasSize) {
				lightmapIndex++;
				j--;
				i++;
				useSpace = 0;
				nextPoint = { 0, 0, 0, 0 };
				maxHeight = 0;
				CreateLightMap();
			}
		}

		float efficiency = ((float)useSpace / (canvasSize * canvasSize)) * 100.0f;
		std::cout << "Efficiency: " << efficiency << "%\n";
	}

	void LightMap::DrawRectangles(
		const std::unique_ptr<LightmapShadowPass>& m_pLightmapShadowPass,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass
		//const std::unique_ptr<NormalMapPass>& m_pNormalMapPass
	)
	{
		int lightmapIndex = 0;

		for (auto& lightmap : lightmaps)
			DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(lightmap->m_pUAV, Colors::Transparent);

		DeviceState::g_pDeviceContext->CSSetShader(m_computeShader->GetShader(), nullptr, 0);
		DeviceState::g_pDeviceContext->CSSetSamplers(0, 1, &sample->m_SamplerState); // sampler 0
		DeviceState::g_pDeviceContext->CSSetSamplers(1, 1, &pointSample->m_SamplerState); // sampler 0

		DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[lightmapIndex]->m_pUAV, nullptr); // target texture 0

		CBSetting cbset = {};
		cbset.lightsize = MAX_LIGHTS;
		cbset.bias = bias;
		cbset.shadowmapSize = { m_pLightmapShadowPass->shadowmapSize, m_pLightmapShadowPass->shadowmapSize };
		cbset.globalAmbient = m_renderscene->m_LightController->GetProperties().m_globalAmbient;
		cbset.useEnvMap = true;
		//cbset.useAo = true;
		DirectX11::UpdateBuffer(m_settingBuf.Get(), &cbset);
		DirectX11::CSSetConstantBuffer(0, 1, m_settingBuf.GetAddressOf());	// setting 0

		{
			D3D11_TEXTURE2D_DESC texArrayDesc = {};
			texArrayDesc.Width = m_pLightmapShadowPass->shadowmapSize;
			texArrayDesc.Height = m_pLightmapShadowPass->shadowmapSize;
			texArrayDesc.MipLevels = 1;
			texArrayDesc.ArraySize = MAX_LIGHTS; // N개의 텍스처
			texArrayDesc.Format = DXGI_FORMAT_R32_TYPELESS;
			texArrayDesc.SampleDesc.Count = 1;
			texArrayDesc.Usage = D3D11_USAGE_DEFAULT;
			texArrayDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			texArrayDesc.CPUAccessFlags = 0;

			ID3D11Texture2D* textureArray = nullptr;
			auto hr = DeviceState::g_pDevice->CreateTexture2D(&texArrayDesc, nullptr, &textureArray);

			for (UINT i = 0; i < MAX_LIGHTS; i++) {
				DeviceState::g_pDeviceContext->CopySubresourceRegion(
					textureArray, D3D11CalcSubresource(0, i, 1), 0, 0, 0, // Dest
					m_pLightmapShadowPass->m_shadowmapTextures[i]->m_pTexture, 0, nullptr // Source
				);
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Texture2DArray.FirstArraySlice = 0;
			srvDesc.Texture2DArray.ArraySize = MAX_LIGHTS;

			
			hr = DeviceState::g_pDevice->CreateShaderResourceView(textureArray, &srvDesc, &textureArraySRV);

			DirectX11::CSSetShaderResources(0, 1, &textureArraySRV); // shadowmap texture array 0
		}

		{
			// light matrix
			std::vector<CBLight> lightMats;
			for (int i = 0; i < MAX_LIGHTS; i++) {
				// CB light
				CBLight cblit = {};
				auto& light = m_renderscene->m_LightController->GetLight(i);
				cblit.view = light.GetViewMatrix();
				cblit.proj = light.GetProjectionMatrix(0.1f, 100.f);
				cblit.position = light.m_position;
				cblit.direction = light.m_direction;
				cblit.color = light.m_color;
				cblit.constantAtt = light.m_constantAttenuation;
				cblit.linearAtt = light.m_linearAttenuation;
				cblit.quadAtt = light.m_quadraticAttenuation;
				cblit.spotAngle = light.m_spotLightAngle;
				cblit.lightType = light.m_lightType;
				cblit.status = light.m_lightStatus;

				lightMats.emplace_back(cblit);
			}
			D3D11_BUFFER_DESC bufferDesc = {};
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.ByteWidth = sizeof(CBLight) * (UINT)lightMats.size();
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.StructureByteStride = sizeof(CBLight);
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

			// 초기 데이터 설정
			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = lightMats.data();

			// 버퍼 생성
			ID3D11Buffer* structuredBuffer = nullptr;
			DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &structuredBuffer);

			// (3) Shader Resource View 생성
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.ElementWidth = sizeof(CBLight);
			srvDesc.Buffer.NumElements = (UINT)lightMats.size();

			DeviceState::g_pDevice->CreateShaderResourceView(structuredBuffer, &srvDesc, &structuredBufferSRV);

			// (4) Compute Shader에 바인딩
			DirectX11::CSSetShaderResources(2, 1, &structuredBufferSRV);
		}

		DirectX11::CSSetShaderResources(4, 1, &envMap->m_pSRV);
		//DirectX11::CSSetShaderResources(0, 1, &m_pLightmapShadowPass->m_shadowmapTextures[0]->m_pSRV);

		for (int i = 0; i < rects.size(); i++)
		{
			MeshRenderer* renderer = static_cast<MeshRenderer*>(rects[i].data);
			if (lightmapIndex != renderer->m_LightMapping.lightmapIndex) {
				lightmapIndex = renderer->m_LightMapping.lightmapIndex;
				DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[lightmapIndex]->m_pUAV, nullptr); // 타겟 텍스처
			}

			// CB 업데이트
			CBData cbData = {};
			int cSize = canvasSize;
			cbData.Offset = { rects[i].x, rects[i].y }; // 타겟 텍스처에서 시작 위치 (0~1)
			cbData.Size = { rects[i].w, rects[i].h };   // 복사할 크기 (0~1)
			cbData.useAo = renderer->m_Material->m_AOMap != nullptr;
			DirectX11::UpdateBuffer(m_Buffer.Get(), &cbData);
			DirectX11::CSSetConstantBuffer(1, 1, m_Buffer.GetAddressOf());

			// CB Transform
			CBTransform cbtr = {};
			cbtr.worldMat = rects[i].worldMat;
			DirectX11::UpdateBuffer(m_transformBuf.Get(), &cbtr);
			DirectX11::CSSetConstantBuffer(2, 1, m_transformBuf.GetAddressOf());

			if (renderer->m_Mesh == nullptr) continue;
			auto meshName = renderer->m_Mesh->GetName();
			DirectX11::CSSetShaderResources(1, 1, &m_pPositionMapPass->m_positionMapTextures[meshName]->m_pSRV);
			DirectX11::CSSetShaderResources(3, 1, &m_pPositionMapPass->m_normalMapTextures[meshName]->m_pSRV);
			if(renderer->m_Material->m_AOMap)
				DirectX11::CSSetShaderResources(5, 1, &renderer->m_Material->m_AOMap->m_pSRV);

			// 컴퓨트 셰이더 실행
			UINT numGroupsX = (UINT)ceil(canvasSize / 32.0f);
			UINT numGroupsY = (UINT)ceil(canvasSize / 32.0f);
			DeviceState::g_pDeviceContext->Dispatch(numGroupsX, numGroupsY, 1);
		}


		//for (auto& lightmap : lightmaps) {
		//	// 엣지 설정.
		//	DirectX11::CSSetShader(m_edgeComputeShader->GetShader(), nullptr, 0);
		//	DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(edgeTexture->m_pUAV, Colors::Transparent);
		//	DirectX11::CSSetUnorderedAccessViews(0, 1, &edgeTexture->m_pUAV, nullptr); // 외각선 텍스처
		//	DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
		//	DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);

		//	// 엣지로 덮어쓰기.
		//	DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
		//	DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
		//	DirectX11::CSSetShaderResources(0, 1, &edgeTexture->m_pSRV); // 외각선 텍스처
		//	DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);

		//	// 엣지 설정.
		//	DirectX11::CSSetShader(m_edgeComputeShader->GetShader(), nullptr, 0);
		//	DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(edgeTexture->m_pUAV, Colors::Transparent);
		//	DirectX11::CSSetUnorderedAccessViews(0, 1, &edgeTexture->m_pUAV, nullptr); // 외각선 텍스처
		//	DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
		//	DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);

		//	// 엣지로 덮어쓰기.
		//	DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
		//	DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
		//	DirectX11::CSSetShaderResources(0, 1, &edgeTexture->m_pSRV); // 외각선 텍스처
		//	DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
		//}

		for (auto& lightmap : lightmaps) {
			// msaa
			DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
			DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(edgeTexture->m_pUAV, Colors::Transparent);
			DirectX11::CSSetUnorderedAccessViews(0, 1, &edgeTexture->m_pUAV, nullptr); // 외각선 텍스처
			DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
			DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
			// msaa 덮어쓰기.
			DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
			DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
			DirectX11::CSSetShaderResources(0, 1, &edgeTexture->m_pSRV); // 외각선 텍스처
			DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
		}



		// 200mb ~ 300mb 잡아 먹음. 필요할때만 키기
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = canvasSize;
		desc.Height = canvasSize;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DYNAMIC;  // CPU에서 업데이트 가능
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, &imgTexture);

		// Shader Resource View (SRV) 생성 (ImGui에서 사용하기 위해 필요)
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;

		DeviceState::g_pDevice->CreateShaderResourceView(imgTexture, &srvDesc, &imgSRV);

		for (int i = 0; i < lightmaps.size(); i++) {
			DeviceState::g_pDeviceContext->CopyResource(imgTexture, lightmaps[i]->m_pTexture);

			// 텍스쳐 저장
			DirectX::ScratchImage image;
			HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, imgTexture, image);
			std::wstring filename = L"Lightmap" + std::to_wstring(i) + L".png";
			DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE,
				GUID_ContainerFormatPng, filename.data());
		}
		imgTexture->Release();
		imgTexture = nullptr;
	}

	void LightMap::CreateLightMap()
	{
		Texture* tex = Texture::Create(
			canvasSize,
			canvasSize,
			"Lightmap",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		tex->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		tex->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		lightmaps.push_back(tex);
	}

	void LightMap::ClearLightMaps()
	{
		for (auto& lightmap : lightmaps)
		{
			delete lightmap;
		}
		lightmaps.clear();
		delete edgeTexture;
		edgeTexture = nullptr;
	}


	void LightMap::Execute(RenderScene& scene, Camera& camera)
	{
	}


	void LightMap::Resize()
	{
	}

	void LightMap::GenerateLightMap(
		RenderScene* scene,
		const std::unique_ptr<LightmapShadowPass>& m_pLightmapShadowPass,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass
		//const std::unique_ptr<NormalMapPass>& m_pNormalMapPass
	)
	{
		SetScene(scene);

		Prepare();
		//TestPrepare();
		PrepareRectangles();
		CalculateRectangles();
		DrawRectangles(m_pLightmapShadowPass, m_pPositionMapPass);
	}
}

/*
 - 권용우

 추가 개선 가능 여부
 1. 해당 알고리즘으로 shadow Atlas를 생성 가능할듯.
*/