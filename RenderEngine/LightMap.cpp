#include "LightMap.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "LightProperty.h"
#include "LightController.h"
#include "RenderableComponents.h"
#include "RenderScene.h"
#include "Material.h"
#include "Core.Random.h"
#include "PositionMapPass.h"
#include "LightmapPass.h"
#include "ResourceAllocator.h"

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
		DirectX::XMFLOAT4 globalAmbient;
		float bias;
		int lightsize;
		bool32 useEnvMap;
		int pad;
	};

	struct alignas(16) indirectBuf {
		int2 Resolution;
		int triangleCount;    // indice
		int g_SampleCount;    // ray 샘플링 수
	};

	LightMap::LightMap()
	{
	}
	LightMap::~LightMap()
	{
		delete sample;
		delete pointSample;
	}

	void LightMap::Initialize()
	{
		m_computeShader = &ShaderSystem->ComputeShaders["Lightmap"];
		m_edgeComputeShader = &ShaderSystem->ComputeShaders["NeighborSampling"];
		m_edgeCoverComputeShader = &ShaderSystem->ComputeShaders["LightmapEdgeCover"];
		m_MSAAcomputeShader = &ShaderSystem->ComputeShaders["MSAA"];
		m_indirectLightShader = &ShaderSystem->ComputeShaders["IndirectLightMap"];
		m_AddTextureColor = &ShaderSystem->ComputeShaders["AddTextureColor"];

		m_Buffer = DirectX11::CreateBuffer(sizeof(CBData), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_transformBuf = DirectX11::CreateBuffer(sizeof(CBTransform), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_settingBuf = DirectX11::CreateBuffer(sizeof(CBSetting), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_indirect1 = DirectX11::CreateBuffer(sizeof(indirectBuf), D3D11_BIND_CONSTANT_BUFFER, nullptr);

		sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
		pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	}

	void LightMap::Prepare()
	{
		// 라이트맵 정보 초기화.
		ClearLightMaps();

		// 후처리를 위한 임시 텍스쳐 생성.
		tempTexture = Texture::Create(
			canvasSize,
			canvasSize,
			"tempTexture",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		tempTexture->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		tempTexture->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);
	}
	
	void LightMap::PrepareRectangles()
	{
		// rects에 각 메쉬의 해상도를 넣어줌
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

			if( canvasSize < size + padding * 2)
				Debug->LogError("The size of one object exceeds the range. : " + std::to_string(size));

			r.data = renderer;
			r.worldMat = obj->m_transform.GetWorldMatrix();
			renderer->m_LightMapping.ligthmapResolution = size;
			renderer->m_LightMapping.lightmapTiling.x = size / canvasSize;
			renderer->m_LightMapping.lightmapTiling.y = size / canvasSize;
			rects.push_back(r);
		}

		// 정렬 (높이 오름차순)
		std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) {
			return a.h < b.h;
			});

		CreateLightMap();
	}

	void LightMap::CalculateRectangles()
	{
		// 사각형 배치. 자리가 부족하면 다음 라이트맵으로 넘어감.
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
		Debug->Log("Lightmap Efficiency: " + std::to_string(efficiency) + "%");

		//std::cout << "Efficiency: " << efficiency << "%\n";
	}

	int LightMap::DrawRectangles(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass)
	{
		// vector size 초기화.
		int triangleCount = 0;
		///*m_trianglesInScene.clear();
		//m_triIndices.clear();
		//bvhNodes.clear();*/
		for (auto& mesh : m_renderscene->GetScene()->m_SceneObjects) {
			auto meshrenderer = mesh->GetComponent<MeshRenderer>();
			if (meshrenderer == nullptr) continue;
			if (!meshrenderer->IsEnabled()) continue;
			if (meshrenderer->m_Mesh == nullptr) continue;
			int face = meshrenderer->m_Mesh->GetIndices().size();
			triangleCount += face /3;
		}
		//triangleCount *= 2;
		m_trianglesInScene.reserve(triangleCount);
		m_triIndices.reserve(triangleCount);

		int index = 0; // 삼각형에 id부여.
		// 씬의 모든 삼각형을 가져옴.
		for (auto& mesh : m_renderscene->GetScene()->m_SceneObjects) {
			auto meshrenderer = mesh->GetComponent<MeshRenderer>();
			if (meshrenderer == nullptr) continue;
			if (!meshrenderer->IsEnabled()) continue;
			if (meshrenderer->m_Mesh == nullptr) continue;
			auto& m = meshrenderer->m_Mesh;
			//auto& name = m->GetName();
			auto& indices = m->GetIndices();
			auto& vertices = m->GetVertices();
			auto worldMatrix = mesh->m_transform.GetWorldMatrix();
			for (int i = 0; i < indices.size() / 3; i++) {
				Triangle t{};
				int i0 = indices[i * 3];
				int i1 = indices[i * 3 + 1];
				int i2 = indices[i * 3 + 2];
				t.v0 = XMVector4Transform(XMVectorSet(vertices[i0].position.x, vertices[i0].position.y, vertices[i0].position.z, 1.f), worldMatrix);
				t.v1 = XMVector4Transform(XMVectorSet(vertices[i1].position.x, vertices[i1].position.y, vertices[i1].position.z, 1.f), worldMatrix);
				t.v2 = XMVector4Transform(XMVectorSet(vertices[i2].position.x, vertices[i2].position.y, vertices[i2].position.z, 1.f), worldMatrix);
				t.n0 = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(vertices[i0].normal.x, vertices[i0].normal.y, vertices[i0].normal.z, 0.f), worldMatrix));
				t.n1 = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(vertices[i1].normal.x, vertices[i1].normal.y, vertices[i1].normal.z, 0.f), worldMatrix));
				t.n2 = XMVector3Normalize(XMVector3TransformNormal(XMVectorSet(vertices[i2].normal.x, vertices[i2].normal.y, vertices[i2].normal.z, 0.f), worldMatrix));
				t.v0.m128_f32[3] = 1;
				t.v1.m128_f32[3] = 1;
				t.v2.m128_f32[3] = 1;
				t.n0.m128_f32[3] = 0;
				t.n1.m128_f32[3] = 0;
				t.n2.m128_f32[3] = 0;
				auto& litmaping = meshrenderer->m_LightMapping;
				t.uv0 = (vertices[i0].uv0 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.uv1 = (vertices[i1].uv0 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.uv2 = (vertices[i2].uv0 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.lightmapUV0 = (vertices[i0].uv1 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.lightmapUV1 = (vertices[i1].uv1 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.lightmapUV2 = (vertices[i2].uv1 * litmaping.lightmapTiling) + litmaping.lightmapOffset;
				t.lightmapIndex = litmaping.lightmapIndex;
				m_trianglesInScene.push_back(t);
				m_triIndices.push_back(index++);

				//Triangle backFace{};
				//backFace.v0 = t.v2;
				//backFace.v1 = t.v1;
				//backFace.v2 = t.v0;
				//backFace.n0 = -t.n2; // 노멀 반전
				//backFace.n1 = -t.n1;
				//backFace.n2 = -t.n0;
				//backFace.uv0 = t.uv2;
				//backFace.uv1 = t.uv1;
				//backFace.uv2 = t.uv0;
				//backFace.lightmapUV0 = t.lightmapUV2;
				//backFace.lightmapUV1 = t.lightmapUV1;
				//backFace.lightmapUV2 = t.lightmapUV0;
				//backFace.lightmapIndex = t.lightmapIndex;
				//m_trianglesInScene.push_back(backFace);
				//m_triIndices.push_back(index++);
			}
		}

		int bvh = BuildBVH(m_trianglesInScene, m_triIndices, 0, m_triIndices.size() - 1);

		// triangle buffer
		{
			D3D11_BUFFER_DESC bufferDesc = {};
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.ByteWidth = sizeof(Triangle) * m_trianglesInScene.size();
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bufferDesc.StructureByteStride = sizeof(Triangle);

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = m_trianglesInScene.data();

			triangleBuffer = nullptr;
			DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &triangleBuffer);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = (UINT)m_trianglesInScene.size();

			auto hr = DeviceState::g_pDevice->CreateShaderResourceView(triangleBuffer, &srvDesc, &TriangleBufferSRV);
			if (!SUCCEEDED(hr))
				Debug->LogError("Failed to create Shader Resource View for triangle buffer.");
			else {
				Debug->Log("Triangle buffer created successfully.");
			}
		}
		// indice buffer
		{
			D3D11_BUFFER_DESC bufferDesc = {};
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.ByteWidth = sizeof(int) * m_triIndices.size();
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bufferDesc.StructureByteStride = sizeof(int);

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = m_triIndices.data();

			indiceBuffer = nullptr;
			DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &indiceBuffer);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = (UINT)m_triIndices.size();

			auto hr = DeviceState::g_pDevice->CreateShaderResourceView(indiceBuffer, &srvDesc, &TriangleIndiceBufferSRV);
			if (!SUCCEEDED(hr))
				Debug->LogError("Failed to create Shader Resource View for Indice buffer.");
			else {
				Debug->Log("Indice buffer created successfully.");
			}
		}
		// BVH buffer
		{
			D3D11_BUFFER_DESC bufferDesc = {};
			bufferDesc.Usage = D3D11_USAGE_DEFAULT;
			bufferDesc.ByteWidth = sizeof(BVHNode) * bvhNodes.size();
			bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			bufferDesc.StructureByteStride = sizeof(BVHNode);

			D3D11_SUBRESOURCE_DATA initData = {};
			initData.pSysMem = bvhNodes.data();

			bvhBuffer = nullptr;
			DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &bvhBuffer);

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.FirstElement = 0;
			srvDesc.Buffer.NumElements = (UINT)bvhNodes.size();

			auto hr = DeviceState::g_pDevice->CreateShaderResourceView(bvhBuffer, &srvDesc, &BVHBufferSRV);
			if (!SUCCEEDED(hr))
				Debug->LogError("Failed to create Shader Resource View for BVH buffer.");
			else {
				Debug->Log("BVH buffer created successfully.");
			}
		}

		DirectX11::CSSetShaderResources(10, 1, &TriangleBufferSRV);
		DirectX11::CSSetShaderResources(11, 1, &TriangleIndiceBufferSRV);
		DirectX11::CSSetShaderResources(12, 1, &BVHBufferSRV);



		int lightmapIndex = 0;

		for (auto& lightmap : lightmaps)
			DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(lightmap->m_pUAV, Colors::Transparent);

		DeviceState::g_pDeviceContext->CSSetShader(m_computeShader->GetShader(), nullptr, 0);
		DeviceState::g_pDeviceContext->CSSetSamplers(0, 1, &sample->m_SamplerState); // sampler 0
		DeviceState::g_pDeviceContext->CSSetSamplers(1, 1, &pointSample->m_SamplerState); // sampler 1

		DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[lightmapIndex]->m_pUAV, nullptr); // target texture 0

		CBSetting cbset = {};
		cbset.lightsize = MAX_LIGHTS;
		cbset.bias = bias;
		cbset.globalAmbient = m_renderscene->m_LightController->GetProperties().m_globalAmbient;
		cbset.useEnvMap = true;
		//cbset.useAo = true;
		DirectX11::UpdateBuffer(m_settingBuf.Get(), &cbset);
		DirectX11::CSSetConstantBuffer(0, 1, m_settingBuf.GetAddressOf());	// setting 0

		{
			// light matrix
			std::vector<CBLight> lightMats;
			for (int i = 0; i < MAX_LIGHTS; i++) {
				// CB light
				CBLight cblit = {};
				auto& light = m_renderscene->m_LightController->GetLight(i);
				cblit.view = light.GetLightViewMatrix();
				cblit.proj = light.GetLightProjectionMatrix(0.1f, 100.f);
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
			structuredLightBuffer = nullptr;
			DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &structuredLightBuffer);

			// (3) Shader Resource View 생성
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = DXGI_FORMAT_UNKNOWN;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			srvDesc.Buffer.ElementWidth = sizeof(CBLight);
			srvDesc.Buffer.NumElements = (UINT)lightMats.size();

			DeviceState::g_pDevice->CreateShaderResourceView(structuredLightBuffer, &srvDesc, &structuredLightBufferSRV);

			// (4) Compute Shader에 바인딩
			DirectX11::CSSetShaderResources(2, 1, &structuredLightBufferSRV);
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


		for (auto& lightmap : lightmaps) {
			for (int i = 0; i < dilateCount; i++) {
				// 엣지 설정.
				DirectX11::CSSetShader(m_edgeComputeShader->GetShader(), nullptr, 0);
				DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // 외각선 텍스처
				DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
				DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);

				// 엣지로 덮어쓰기.
				DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
				DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // 외각선 텍스처
				DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
			}
		}

		for (auto& lightmap : lightmaps) {
			ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

			for (int i = 0; i < directMSAACount; i++) {
				// msaa
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
				DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // 외각선 텍스처
				DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
				DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);


				// msaa 덮어쓰기.
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
				DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // 외각선 텍스처
				DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
			}
		}

		return triangleCount;
	}

	void LightMap::DrawIndirectLight(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass, int indices)
	{
		
		for(int i = 0; i < indirectCount; i++){
			// triangleCount, sampleCount Update
			indirectBuf ind = {};
			ind.Resolution = { canvasSize, canvasSize };
			ind.triangleCount = indices;
			ind.g_SampleCount = sampleCount;
			DirectX11::UpdateBuffer(m_indirect1.Get(), &ind);
			DirectX11::CSSetConstantBuffer(0, 1, m_indirect1.GetAddressOf());
			DirectX11::CSSetShader(m_indirectLightShader->GetShader(), nullptr, 0);
			// 라이트맵 배열
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = canvasSize;
			desc.Height = canvasSize;
			desc.MipLevels = 1;
			desc.ArraySize = lightmaps.size();
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

			ID3D11Texture2D* textureArray = nullptr;
			DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, &textureArray);

			for (UINT i = 0; i < lightmaps.size(); ++i)
			{
				DeviceState::g_pDeviceContext->CopySubresourceRegion(
					textureArray,                      // 대상 Texture2DArray
					D3D11CalcSubresource(0, i, 1),     // (MipLevel, ArraySlice, MipLevels)
					0, 0, 0,
					lightmaps[i]->m_pTexture,                // 각각의 개별 텍스처
					0,
					nullptr
				);
			}

			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			srvDesc.Texture2DArray.MipLevels = 1;
			srvDesc.Texture2DArray.ArraySize = lightmaps.size();
			srvDesc.Texture2DArray.FirstArraySlice = 0;

			ID3D11ShaderResourceView* textureArraySRV = nullptr;
			DeviceState::g_pDevice->CreateShaderResourceView(textureArray, &srvDesc, &textureArraySRV);

			DirectX11::CSSetShaderResources(1, 1, &textureArraySRV);



			int ligtmapIndex = -1;
			for (int i = 0; i < rects.size(); i++)
			{
				MeshRenderer* renderer = static_cast<MeshRenderer*>(rects[i].data);
				if (ligtmapIndex != renderer->m_LightMapping.lightmapIndex) {
					ligtmapIndex = renderer->m_LightMapping.lightmapIndex;
					DirectX11::CSSetUnorderedAccessViews(0, 1, &indirectMaps[ligtmapIndex]->m_pUAV, nullptr); // 타겟 텍스처
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
				DirectX11::CSSetShaderResources(2, 1, &m_pPositionMapPass->m_positionMapTextures[meshName]->m_pSRV);
				DirectX11::CSSetShaderResources(3, 1, &m_pPositionMapPass->m_normalMapTextures[meshName]->m_pSRV);

				// 컴퓨트 셰이더 실행
				UINT numGroupsX = (UINT)ceil(canvasSize / 32.0f);
				UINT numGroupsY = (UINT)ceil(canvasSize / 32.0f);
				DeviceState::g_pDeviceContext->Dispatch(numGroupsX, numGroupsY, 1);
			}

			for (auto& lightmap : indirectMaps) {
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

				for (int i = 0; i < indirectCount; i++) {
					// msaa
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
					DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // 외각선 텍스처
					DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // 라이트맵 텍스처
					DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);


					// msaa 덮어쓰기.
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // 라이트맵 텍스처
					DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // 외각선 텍스처
					DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
				}
			}

			DirectX11::CSSetShader(m_AddTextureColor->GetShader(), nullptr, 0);
			for (int i = 0; i < lightmaps.size(); i++) {
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

				// AddTextureColor
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[i]->m_pUAV, nullptr); // 외각선 텍스처
				DirectX11::CSSetShaderResources(0, 1, &indirectMaps[i]->m_pSRV); // 라이트맵 텍스처
				DirectX11::Dispatch(canvasSize / 32.f, canvasSize / 32.f, 1);
				//
				//DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(indirectMaps[i]->m_pUAV, Colors::Transparent);
			}

			textureArraySRV->Release();
			textureArray->Release();
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
			std::wstring filename = L"Lightmap" + std::to_wstring(i) + L".hdr";
			//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename.data());
			DirectX::SaveToHDRFile(*image.GetImage(0, 0, 0), filename.data());
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

		Texture* indirect = Texture::Create(
			canvasSize,
			canvasSize,
			"indirectMap",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		indirect->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		indirect->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		lightmaps.push_back(tex);
		indirectMaps.push_back(indirect);
	}

	void LightMap::ClearLightMaps()
	{
		for (auto& lightmap : lightmaps)
		{
			DeallocateResource(lightmap);
		}
		for (auto& indirect : indirectMaps)
		{
			DeallocateResource(indirect);
		}
		lightmaps.clear();
		indirectMaps.clear();
		DeallocateResource(tempTexture);
		tempTexture = nullptr;

		/*structuredLightBufferSRV->Release();
		BVHBufferSRV->Release();
		TriangleIndiceBufferSRV->Release();
		TriangleBufferSRV->Release();*/
		/*structuredLightBuffer->Release();
		bvhBuffer->Release();
		indiceBuffer->Release();
		triangleBuffer->Release();*/

		bvhNodes.clear();
		m_trianglesInScene.clear();
		m_triIndices.clear();
	}


	void LightMap::Execute(RenderScene& scene, Camera& camera)
	{
	}


	void LightMap::Resize()
	{
	}

	Coroutine<> LightMap::GenerateLightmapCoroutine(
		RenderScene* scene,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
		const std::unique_ptr<LightMapPass>& m_pLightMapPass)
	{

		SetScene(scene);

		Prepare();
		Debug->Log("Prepare");
		co_yield ReturnNull();
		//TestPrepare();
		PrepareRectangles();
		Debug->Log("PrepareRectangles");
		co_yield ReturnNull();
		CalculateRectangles();
		Debug->Log("CalculateRectangles");
		co_yield ReturnNull();
		int indices = DrawRectangles(m_pPositionMapPass);
		Debug->Log("DrawRectangles");
		co_yield ReturnNull();

		DrawIndirectLight(m_pPositionMapPass, indices);
		Debug->Log("Complete Bake Lightmap");

		m_pLightMapPass->Initialize(lightmaps);
	}

	void LightMap::GenerateLightMap(
		RenderScene* scene,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
		const std::unique_ptr<LightMapPass>& m_pLightMapPass
	)
	{
		StartCoroutine(GenerateLightmapCoroutine(scene, m_pPositionMapPass, m_pLightMapPass));
		//SetScene(scene);

		//Prepare();
		////TestPrepare();
		//PrepareRectangles();
		//CalculateRectangles();
		//int indices = DrawRectangles(m_pPositionMapPass);

		//DrawIndirectLight(m_pPositionMapPass, indices);
	}
}

/*
 - 권용우

 추가 개선 가능 여부
 1. 해당 알고리즘으로 shadow Atlas를 생성 가능할듯.
*/