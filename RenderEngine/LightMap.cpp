#ifndef DYNAMICCPP_EXPORTS
#include "LightMap.h"
#include "ShaderSystem.h"
#include "Scene.h"
#include "Mesh.h"
#include "LightProperty.h"
#include "LightController.h"
#include "RenderableComponents.h"
#include "RenderScene.h"
#include "SceneManager.h"
#include "Material.h"
#include "Core.Random.h"
#include "PositionMapPass.h"
#include "LightmapPass.h"
#include "ProgressWindow.h"

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
		Mathf::Vector4 position;
		Mathf::Vector4 direction;
		Mathf::Color4  color;

		float constantAtt;
		float linearAtt;
		float quadAtt;
		float spotAngle;

		int lightType;
		int status;
		float range;
		float intencity;
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
		int indirectSampleCount;
	};

	struct alignas(16) Hammersley_sample {
		float2 xi;
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
		m_NormalizeTextureColor = &ShaderSystem->ComputeShaders["NormalizeTexture"];

		m_Buffer = DirectX11::CreateBuffer(sizeof(CBData), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_transformBuf = DirectX11::CreateBuffer(sizeof(CBTransform), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_settingBuf = DirectX11::CreateBuffer(sizeof(CBSetting), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_indirect1 = DirectX11::CreateBuffer(sizeof(indirectBuf), D3D11_BIND_CONSTANT_BUFFER, nullptr);
		m_CBHammersley = DirectX11::CreateBuffer(sizeof(Hammersley_sample), D3D11_BIND_CONSTANT_BUFFER, nullptr);

		sample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
		pointSample = new Sampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
	}

	void LightMap::Prepare()
	{
		// ����Ʈ�� ���� �ʱ�ȭ.
		ClearLightMaps();

		// ��ó���� ���� �ӽ� �ؽ��� ����.
		tempTexture = Texture::Create(
			canvasSize,
			canvasSize,
			"tempTexture",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		tempTexture->m_textureType = TextureType::ImageTexture;
		tempTexture->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		tempTexture->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);
	}
	
	void LightMap::PrepareRectangles()
	{
		// rects�� �� �޽��� �ػ󵵸� �־���
		rects.clear();

		for (auto& renderer : SceneManagers->GetAllMeshRenderers()) {
			if (renderer == nullptr) continue;
			if (!renderer->IsEnabled()) continue;
			if (renderer->m_Mesh == nullptr)continue;

			// �ػ� push.
			Rect r;
			float size = rectSize * renderer->m_LightMapping.lightmapScale;
			r.w = size;
			r.h = size;

			if( canvasSize < size + padding * 2)
				Debug->LogError("The size of one object exceeds the range. : " + std::to_string(size));

			r.data = renderer;
			r.worldMat = renderer->GetOwner()->m_transform.GetWorldMatrix();
			renderer->m_LightMapping.ligthmapResolution = size;
			renderer->m_LightMapping.lightmapTiling.x = size / canvasSize;
			renderer->m_LightMapping.lightmapTiling.y = size / canvasSize;
			rects.push_back(r);
		}

		// ���� (���� ��������)
		std::sort(rects.begin(), rects.end(), [](const Rect& a, const Rect& b) {
			return a.h < b.h;
			});

		CreateLightMap();
	}

	bool LightMap::CalculateRectangles()
	{
		// Shelf First-Fit Bin Packing
		// �簢�� ��ġ. �ڸ��� �����ϸ� ���� ����Ʈ������ �Ѿ.
		int useSpace = 0;
		Rect nextPoint = { 0, 0, 0, 0 };
		int maxHeight = 0;
		int i = rects.size();
		int j = 0;
		int lightmapIndex = 0;

		while (i > 0) {
			if (rects[j].w + padding > canvasSize) {
				Debug->Log("Rect size is too large. : " + std::to_string(rects[j].w));
				return false;
			}
			if (rects[j].w + padding <= canvasSize - nextPoint.w) {
				rects[j].x = nextPoint.w;   // ���� x ��ġ
				rects[j].y = nextPoint.h;  // ���� y ��ġ

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

			// �߰��� rect�� size�� �ʰ��ϸ� ���ο� lightmap�� ����� �ٽ� ��ġ.
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
		return true;
		//std::cout << "Efficiency: " << efficiency << "%\n";
	}

	void LightMap::PrepareTriangles()
	{
		// vector size �ʱ�ȭ.
		int triangleCount = 0;
		///*m_trianglesInScene.clear();
		//m_triIndices.clear();
		//bvhNodes.clear();*/
		for (auto& meshrenderer : SceneManagers->GetAllMeshRenderers()) {
			//auto meshrenderer = mesh->GetComponent<MeshRenderer>();
			if (meshrenderer == nullptr) continue;
			if (!meshrenderer->IsEnabled()) continue;
			if (meshrenderer->m_Mesh == nullptr) continue;
			int face = meshrenderer->m_Mesh->GetIndices().size();
			triangleCount += face / 3;
		}
		//triangleCount *= 2;
		m_trianglesInScene.reserve(triangleCount);
		m_triIndices.reserve(triangleCount);
	}

	void LightMap::DrawRectangles(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass)
	{

		
	}

	void LightMap::DilateLightMap()
	{
	}

	void LightMap::DirectBlur()
	{
	}

	void LightMap::DrawIndirectLight(const std::unique_ptr<PositionMapPass>& m_pPositionMapPass)
	{
		
		


		


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
		tex->m_textureType = TextureType::ImageTexture;
		tex->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		tex->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		Texture* indirect = Texture::Create(
			canvasSize,
			canvasSize,
			"IndirectMap",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		indirect->m_textureType = TextureType::ImageTexture;
		indirect->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		indirect->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		Texture* environment = Texture::Create(
			canvasSize,
			canvasSize,
			"Environment",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		environment->m_textureType = TextureType::ImageTexture;
		environment->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		environment->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		Texture* directional = Texture::Create(
			canvasSize,
			canvasSize,
			"Directional",
			DXGI_FORMAT_R32G32B32A32_FLOAT,
			D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE
		);
		directional->m_textureType = TextureType::ImageTexture;
		directional->CreateUAV(DXGI_FORMAT_R32G32B32A32_FLOAT);
		directional->CreateSRV(DXGI_FORMAT_R32G32B32A32_FLOAT);

		lightmaps.push_back(tex);
		indirectMaps.push_back(indirect);
		environmentMaps.push_back(environment);
		directionalMaps.push_back(directional);
	}

	void LightMap::ClearLightMaps()
	{
		for (auto& lightmap : lightmaps)
		{
			delete lightmap;
		}
		for (auto& indirect : indirectMaps)
		{
			delete indirect;
		}
		for (auto& env : environmentMaps)
		{
			delete env;
		}
		for (auto& dir : directionalMaps)
		{
			delete dir;
		}
		lightmaps.clear();
		indirectMaps.clear();
		environmentMaps.clear();
		directionalMaps.clear();
		delete tempTexture;
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


	void lm::LightMap::Resize(uint32_t width, uint32_t height)
	{
	}

	Coroutine<> LightMap::GenerateLightmapCoroutine(
		RenderScene* scene,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
		const std::unique_ptr<LightMapPass>& m_pLightMapPass)
	{
		g_progressWindow->Launch();
		g_progressWindow->SetStatusText(L"LightMap Baking...");
		g_progressWindow->SetProgress(10);
		SetScene(scene);

		g_progressWindow->SetStatusText(L"Prepare...");
		Prepare();
		co_yield OnRender();
		//TestPrepare();
		g_progressWindow->SetStatusText(L"PrepareRectangles...");
		PrepareRectangles();
		co_yield OnRender();
		g_progressWindow->SetStatusText(L"CalculateRectangles...");
		bool calculRect = CalculateRectangles();
		co_yield OnRender();
		g_progressWindow->SetStatusText(L"PrepareTriangles...");
		PrepareTriangles();

		int index = 0; // �ﰢ���� id�ο�.
		// ���� ��� �ﰢ���� ������.
		for (auto& meshrenderer : SceneManagers->GetAllMeshRenderers()) {
			//auto meshrenderer = mesh->GetComponent<MeshRenderer>();
			if (meshrenderer == nullptr) continue;
			if (!meshrenderer->IsEnabled()) continue;
			if (meshrenderer->m_Mesh == nullptr) continue;
			auto& m = meshrenderer->m_Mesh;
			//auto& name = m->GetName();
			auto& indices = m->GetIndices();
			auto& vertices = m->GetVertices();
			auto worldMatrix = meshrenderer->GetOwner()->m_transform.GetWorldMatrix();
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
			}
			co_yield OnRender();
		}
		if (index > 0 && calculRect) {
			g_progressWindow->SetProgress(20);
			g_progressWindow->SetStatusText(L"BuildBVH...");
			int bvh = BuildBVH(m_trianglesInScene, m_triIndices, 0, m_triIndices.size());
			co_yield OnRender();
			g_progressWindow->SetProgress(30);
			g_progressWindow->SetStatusText(L"Update Buffer...");
			//DrawRectangles(m_pPositionMapPass);
			// triangle buffer
			{
				D3D11_BUFFER_DESC bufferDesc = {};
				bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
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
					g_progressWindow->SetStatusText(L"Triangle buffer created successfully.");
				}
			}
			// indice buffer
			{
				D3D11_BUFFER_DESC bufferDesc = {};
				bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
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
					g_progressWindow->SetStatusText(L"Indice buffer created successfully.");
				}
			}
			// BVH buffer
			{
				D3D11_BUFFER_DESC bufferDesc = {};
				bufferDesc.Usage = D3D11_USAGE_IMMUTABLE;
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
					g_progressWindow->SetStatusText(L"BVH buffer created successfully.");
				}
			}

			DirectX11::CSSetShaderResources(10, 1, &TriangleBufferSRV);
			DirectX11::CSSetShaderResources(11, 1, &TriangleIndiceBufferSRV);
			DirectX11::CSSetShaderResources(12, 1, &BVHBufferSRV);

			int lightmapIndex = -1;

			for (auto& lightmap : lightmaps)
				DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(lightmap->m_pUAV, Colors::Transparent);
			for (auto& env : environmentMaps)
				DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(env->m_pUAV, Colors::Transparent);
			for (auto& dir : directionalMaps)
				DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(dir->m_pUAV, Colors::Transparent);


			//DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[lightmapIndex]->m_pUAV, nullptr); // target texture 0
			//DirectX11::CSSetUnorderedAccessViews(7, 1, &environmentMaps[lightmapIndex]->m_pUAV, nullptr); // target texture 1
			g_progressWindow->SetProgress(40);
			g_progressWindow->SetStatusText(L"Set Lights...");

			CBSetting cbset = {};
			cbset.lightsize = MAX_LIGHTS;
			cbset.bias = bias;
			cbset.globalAmbient = m_renderscene->m_LightController->GetProperties().m_globalAmbient;
			cbset.useEnvMap = true;
			//cbset.useAo = true;
			DirectX11::UpdateBuffer(m_settingBuf.Get(), &cbset);

			{
				// light matrix
				std::vector<CBLight> lightMats;
				for (int i = 0; i < m_renderscene->m_LightController->m_lightCount; i++) {
					// CB light
					CBLight cblit = {};
					auto& light = m_renderscene->m_LightController->GetLight(i);
					cblit.position = light.m_position;
					cblit.direction = light.m_direction;
					cblit.color = light.m_color;
					cblit.constantAtt = light.m_constantAttenuation;
					cblit.linearAtt = light.m_linearAttenuation;
					cblit.quadAtt = light.m_quadraticAttenuation;
					cblit.spotAngle = light.m_spotLightAngle;
					cblit.lightType = light.m_lightType;
					cblit.status = light.m_lightStatus;
					cblit.range = light.m_range;
					cblit.intencity = light.m_intencity;

					lightMats.emplace_back(cblit);
				}
				D3D11_BUFFER_DESC bufferDesc = {};
				bufferDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				bufferDesc.ByteWidth = sizeof(CBLight) * (UINT)lightMats.size();
				bufferDesc.Usage = D3D11_USAGE_DEFAULT;
				bufferDesc.StructureByteStride = sizeof(CBLight);
				bufferDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

				// �ʱ� ������ ����
				D3D11_SUBRESOURCE_DATA initData = {};
				initData.pSysMem = lightMats.data();

				// ���� ����
				structuredLightBuffer = nullptr;
				DeviceState::g_pDevice->CreateBuffer(&bufferDesc, &initData, &structuredLightBuffer);

				// (3) Shader Resource View ����
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				srvDesc.Buffer.ElementWidth = sizeof(CBLight);
				srvDesc.Buffer.NumElements = (UINT)lightMats.size();

				DeviceState::g_pDevice->CreateShaderResourceView(structuredLightBuffer, &srvDesc, &structuredLightBufferSRV);

				// (4) Compute Shader�� ���ε�
			}

			//DirectX11::CSSetShaderResources(0, 1, &m_pLightmapShadowPass->m_shadowmapTextures[0]->m_pSRV);
			g_progressWindow->SetProgress(50);
			g_progressWindow->SetStatusText(L"Baking direct...");

			ID3D11UnorderedAccessView* nullUAV[3] = { nullptr, nullptr, nullptr };
			for (int i = 0; i < rects.size(); i++)
			{
				UnBindLightmapPS();
				DirectX11::CSSetUnorderedAccessViews(0, 3, nullUAV, nullptr);
				DeviceState::g_pDeviceContext->CSSetShader(m_computeShader->GetShader(), nullptr, 0);
				DeviceState::g_pDeviceContext->CSSetSamplers(0, 1, &sample->m_SamplerState); // sampler 0
				DeviceState::g_pDeviceContext->CSSetSamplers(1, 1, &pointSample->m_SamplerState); // sampler 1
				DirectX11::CSSetConstantBuffer(0, 1, m_settingBuf.GetAddressOf());	// setting 0
				DirectX11::CSSetShaderResources(2, 1, &structuredLightBufferSRV);
				DirectX11::CSSetShaderResources(4, 1, &envMap->m_pSRV);

				MeshRenderer* renderer = static_cast<MeshRenderer*>(rects[i].data);
				if (lightmapIndex != renderer->m_LightMapping.lightmapIndex) {
					lightmapIndex = renderer->m_LightMapping.lightmapIndex;
				}
				DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[lightmapIndex]->m_pUAV, nullptr); // Ÿ�� �ؽ�ó
				DirectX11::CSSetUnorderedAccessViews(1, 1, &environmentMaps[lightmapIndex]->m_pUAV, nullptr); // target texture 1
				DirectX11::CSSetUnorderedAccessViews(2, 1, &directionalMaps[lightmapIndex]->m_pUAV, nullptr); // target texture 2

				// CB ������Ʈ
				CBData cbData = {};
				int cSize = canvasSize;
				cbData.Offset = { rects[i].x, rects[i].y }; // Ÿ�� �ؽ�ó���� ���� ��ġ (0~1)
				cbData.Size = { rects[i].w, rects[i].h };   // ������ ũ�� (0~1)
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
				if (renderer->m_Material->m_AOMap)
					DirectX11::CSSetShaderResources(5, 1, &renderer->m_Material->m_AOMap->m_pSRV);

				// ��ǻƮ ���̴� ����
				UINT numGroupsX = (UINT)ceil(canvasSize / 16.0f);
				UINT numGroupsY = (UINT)ceil(canvasSize / 16.0f);
				DeviceState::g_pDeviceContext->Dispatch(numGroupsX, numGroupsY, 1);
				DirectX11::CSSetUnorderedAccessViews(0, 3, nullUAV, nullptr);
				m_pLightMapPass->Initialize(lightmaps, directionalMaps);
				co_yield OnRender();
			}

			DirectX11::CSSetUnorderedAccessViews(0, 3, nullUAV, nullptr);
			co_yield OnRender();// ReturnNull();

			//DilateLightMap();
			//for (auto& lightmap : lightmaps) {
			//	for (int i = 0; i < dilateCount; i++) {
			//		// ���� ����.
			//		DirectX11::CSSetShader(m_edgeComputeShader->GetShader(), nullptr, 0);
			//		DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
			//		DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
			//		DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // ����Ʈ�� �ؽ�ó
			//		DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
			//		co_yield ReturnNull();

			//		DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
			//		m_pLightMapPass->Initialize(lightmaps);
			//		co_yield ReturnNull();
			//		// ������ �����.
			//		DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
			//		DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // ����Ʈ�� �ؽ�ó
			//		DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // �ܰ��� �ؽ�ó
			//		DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
			//		co_yield ReturnNull();

			//		DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
			//		m_pLightMapPass->Initialize(lightmaps);
			//		co_yield ReturnNull();
			//	}
			//}
			//co_yield ReturnNull();
			g_progressWindow->SetProgress(60);
			g_progressWindow->SetStatusText(L"Direct Blur...");

			//DirectBlur();
			for (auto& lightmap : lightmaps) {
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

				for (int i = 0; i < directMSAACount; i++) {
					UnBindLightmapPS();
					// msaa
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
					DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // ����Ʈ�� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					co_yield OnRender();

					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();

					UnBindLightmapPS();
					// msaa �����.
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // ����Ʈ�� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // �ܰ��� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					co_yield OnRender();

					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();
				}
			}

			for (auto& lightmap : environmentMaps) {
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

				for (int i = 0; i < directMSAACount; i++) {
					UnBindLightmapPS();
					// msaa
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
					DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // ����Ʈ�� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					co_yield OnRender();

					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();

					UnBindLightmapPS();
					// msaa �����.
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // ����Ʈ�� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // �ܰ��� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					co_yield OnRender();

					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();
				}
			}

			co_yield OnRender();
			g_progressWindow->SetProgress(70);
			g_progressWindow->SetStatusText(L"Baking indirect...");
			//DrawIndirectLight(m_pPositionMapPass);
			for (int i = 0; i < indirectCount; i++) {
				UnBindLightmapPS();

				// triangleCount, sampleCount Update
				indirectBuf ind = {};
				ind.Resolution = { canvasSize, canvasSize };
				ind.triangleCount = m_trianglesInScene.size();
				ind.indirectSampleCount = indirectSampleCount;
				DirectX11::UpdateBuffer(m_indirect1.Get(), &ind);

				

				// ����Ʈ�� �迭
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

				for (UINT j = 0; j < lightmaps.size(); ++j)
				{
					DeviceState::g_pDeviceContext->CopySubresourceRegion(
						textureArray,                      // ��� Texture2DArray
						D3D11CalcSubresource(0, j, 1),     // (MipLevel, ArraySlice, MipLevels)
						0, 0, 0,
						lightmaps[j]->m_pTexture,                // ������ ���� �ؽ�ó
						0,
						nullptr
					);
				}

				//// indirectCount�� 1�� ���� ����Ʈ���� ����ϰ� ���� ��µ� �������� ����ؼ� ����.
				//if (indirectCount == 1) {
				//	for (UINT j = 0; j < lightmaps.size(); ++j)
				//	{
				//		DeviceState::g_pDeviceContext->CopySubresourceRegion(
				//			textureArray,                      // ��� Texture2DArray
				//			D3D11CalcSubresource(0, j, 1),     // (MipLevel, ArraySlice, MipLevels)
				//			0, 0, 0,
				//			lightmaps[j]->m_pTexture,                // ������ ���� �ؽ�ó
				//			0,
				//			nullptr
				//		);
				//	}
				//}
				//else {
				//	for (UINT j = 0; j < indirectMaps.size(); ++j)
				//	{
				//		DeviceState::g_pDeviceContext->CopySubresourceRegion(
				//			textureArray,                      // ��� Texture2DArray
				//			D3D11CalcSubresource(0, j, 1),     // (MipLevel, ArraySlice, MipLevels)
				//			0, 0, 0,
				//			indirectMaps[j]->m_pTexture,                // ������ ���� �ؽ�ó
				//			0,
				//			nullptr
				//		);
				//	}
				//}

				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
				srvDesc.Format = desc.Format;
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
				srvDesc.Texture2DArray.MipLevels = 1;
				srvDesc.Texture2DArray.ArraySize = lightmaps.size();
				srvDesc.Texture2DArray.FirstArraySlice = 0;

				textureArraySRV = nullptr;
				DeviceState::g_pDevice->CreateShaderResourceView(textureArray, &srvDesc, &textureArraySRV);

				int indirectProgressCount = 0;
				for (int k = 0; k < indirectSampleCount; k++) {
					Hammersley_sample cbHSSample = {};
					cbHSSample.xi = HammersleySample(k, indirectSampleCount);
					DirectX11::UpdateBuffer(m_CBHammersley.Get(), &cbHSSample);

					int ligtmapIndex = -1;
					for (int j = 0; j < rects.size(); j++)
					{
						UnBindLightmapPS();
						MeshRenderer* renderer = static_cast<MeshRenderer*>(rects[j].data);
						if (ligtmapIndex != renderer->m_LightMapping.lightmapIndex) {
							ligtmapIndex = renderer->m_LightMapping.lightmapIndex;
						}
						DirectX11::CSSetShader(m_indirectLightShader->GetShader(), nullptr, 0);
						DirectX11::CSSetConstantBuffer(0, 1, m_indirect1.GetAddressOf());
						DirectX11::CSSetUnorderedAccessViews(0, 1, &indirectMaps[ligtmapIndex]->m_pUAV, nullptr); // Ÿ�� �ؽ�ó
						DirectX11::CSSetUnorderedAccessViews(1, 1, &directionalMaps[ligtmapIndex]->m_pUAV, nullptr); // Ÿ�� �ؽ�ó
						DirectX11::CSSetShaderResources(0, 1, &textureArraySRV);

						// CB ������Ʈ
						CBData cbData = {};
						int cSize = canvasSize;
						cbData.Offset = { rects[j].x, rects[j].y }; // Ÿ�� �ؽ�ó���� ���� ��ġ (0~1)
						cbData.Size = { rects[j].w, rects[j].h };   // ������ ũ�� (0~1)
						cbData.useAo = renderer->m_Material->m_AOMap != nullptr;
						DirectX11::UpdateBuffer(m_Buffer.Get(), &cbData);
						DirectX11::CSSetConstantBuffer(1, 1, m_Buffer.GetAddressOf());

						// CB Transform
						CBTransform cbtr = {};
						cbtr.worldMat = rects[j].worldMat;
						DirectX11::UpdateBuffer(m_transformBuf.Get(), &cbtr);
						DirectX11::CSSetConstantBuffer(2, 1, m_transformBuf.GetAddressOf());

						DirectX11::CSSetConstantBuffer(3, 1, m_CBHammersley.GetAddressOf());

						if (renderer->m_Mesh == nullptr) continue;
						auto meshName = renderer->m_Mesh->GetName();
						DirectX11::CSSetShaderResources(1, 1, &m_pPositionMapPass->m_positionMapTextures[meshName]->m_pSRV);
						DirectX11::CSSetShaderResources(2, 1, &m_pPositionMapPass->m_normalMapTextures[meshName]->m_pSRV);

						// ��ǻƮ ���̴� ����
						UINT numGroupsX = (UINT)ceil(canvasSize / 32.0f);
						UINT numGroupsY = (UINT)ceil(canvasSize / 32.0f);
						DeviceState::g_pDeviceContext->Dispatch(numGroupsX, numGroupsY, 1);
						DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
						ID3D11ShaderResourceView* nullsrv3[3] = { nullptr, nullptr, nullptr };
						DirectX11::CSSetShaderResources(0, 3, nullsrv3);
						co_yield OnRender();

						std::wstring progressText = L"baked indirect... ";
						progressText += L"(" + std::to_wstring(i + 1) + L"/" + std::to_wstring(indirectCount) + L") ";
						progressText += std::to_wstring((++indirectProgressCount * 100.f) / (rects.size() * indirectSampleCount)) + L"%";
						g_progressWindow->SetStatusText(progressText);
						g_progressWindow->SetProgress(70 + (indirectProgressCount * 20.f) / (rects.size() * indirectSampleCount));
						m_pLightMapPass->Initialize(lightmaps, directionalMaps);
						co_yield OnRender();
					}
				}
				for (auto& lightmap : indirectMaps) {
					ID3D11ShaderResourceView* nullSRV[1] = { nullptr };

					for (int j = 0; j < indirectMSAACount; j++) {
						UnBindLightmapPS();
						// msaa
						DirectX11::CSSetShaderResources(0, 1, nullSRV);
						DirectX11::CSSetShader(m_MSAAcomputeShader->GetShader(), nullptr, 0);
						DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(tempTexture->m_pUAV, Colors::Transparent);
						DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
						DirectX11::CSSetShaderResources(0, 1, &lightmap->m_pSRV); // ����Ʈ�� �ؽ�ó
						DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
						co_yield OnRender();

						DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
						m_pLightMapPass->Initialize(lightmaps, directionalMaps);
						co_yield OnRender();

						UnBindLightmapPS();
						// msaa �����.
						DirectX11::CSSetShaderResources(0, 1, nullSRV);
						DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
						DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmap->m_pUAV, nullptr); // ����Ʈ�� �ؽ�ó
						DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV); // �ܰ��� �ؽ�ó
						DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
						co_yield OnRender();

						DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
						m_pLightMapPass->Initialize(lightmaps, directionalMaps);
						co_yield OnRender();
					}
				}

				for (int j = 0; j < lightmaps.size(); j++) {
					ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
					UnBindLightmapPS();
					// AddTextureColor
					DirectX11::CSSetShader(m_AddTextureColor->GetShader(), nullptr, 0);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[j]->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &indirectMaps[j]->m_pSRV); // ����Ʈ�� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					co_yield OnRender();

					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();
					//
					//DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(indirectMaps[i]->m_pUAV, Colors::Transparent);
				}

				textureArraySRV->Release();
				textureArray->Release();
			}
			g_progressWindow->SetProgress(90);
			g_progressWindow->SetStatusText(L"Complete Bake Lightmap...");
			co_yield OnRender();

			for (int i = 0; i < directionalMaps.size(); i++) {
				ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
				UnBindLightmapPS();
				// �б�� ����.
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetShader(m_edgeCoverComputeShader->GetShader(), nullptr, 0);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &tempTexture->m_pUAV, nullptr);
				DirectX11::CSSetShaderResources(0, 1, &directionalMaps[i]->m_pSRV);
				DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
				co_yield OnRender();

				UnBindLightmapPS();
				DirectX11::CSSetShader(m_NormalizeTextureColor->GetShader(), nullptr, 0);

				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetUnorderedAccessViews(0, 1, &directionalMaps[i]->m_pUAV, nullptr);
				DirectX11::CSSetShaderResources(0, 1, &tempTexture->m_pSRV);
				DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
				DirectX11::CSSetShaderResources(0, 1, nullSRV);
				DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
				co_yield OnRender();

				m_pLightMapPass->Initialize(lightmaps, directionalMaps);
				co_yield OnRender();
			}

			if (useEnvironmentMap) {
				for (int i = 0; i < lightmaps.size(); i++) {
					ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
					UnBindLightmapPS();
					DirectX11::CSSetShader(m_AddTextureColor->GetShader(), nullptr, 0);
					// AddTextureColor
					DirectX11::CSSetShaderResources(0, 1, nullSRV);
					DirectX11::CSSetUnorderedAccessViews(0, 1, &lightmaps[i]->m_pUAV, nullptr); // �ܰ��� �ؽ�ó
					DirectX11::CSSetShaderResources(0, 1, &environmentMaps[i]->m_pSRV); // ����Ʈ�� �ؽ�ó
					DirectX11::Dispatch(canvasSize / 16.f, canvasSize / 16.f, 1);
					co_yield OnRender();

					DirectX11::CSSetUnorderedAccessViews(0, 2, nullUAV, nullptr);
					m_pLightMapPass->Initialize(lightmaps, directionalMaps);
					co_yield OnRender();
					//
					//DeviceState::g_pDeviceContext->ClearUnorderedAccessViewFloat(indirectMaps[i]->m_pUAV, Colors::Transparent);
				}
			}
			co_yield OnRender();
			g_progressWindow->SetProgress(100);
			g_progressWindow->SetStatusText(L"Save Lightmap Image...");
			// 200mb ~ 300mb ��� ����. �ʿ��Ҷ��� Ű��
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = canvasSize;
			desc.Height = canvasSize;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DYNAMIC;  // CPU���� ������Ʈ ����
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			DeviceState::g_pDevice->CreateTexture2D(&desc, nullptr, &imgTexture);

			// Shader Resource View (SRV) ���� (ImGui���� ����ϱ� ���� �ʿ�)
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = desc.Format;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MipLevels = 1;

			DeviceState::g_pDevice->CreateShaderResourceView(imgTexture, &srvDesc, &imgSRV);

			for (int i = 0; i < lightmaps.size(); i++) {
				DeviceState::g_pDeviceContext->CopyResource(imgTexture, lightmaps[i]->m_pTexture);

				// �ؽ��� ����
				DirectX::ScratchImage image;
				HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, imgTexture, image);

				//std::wstring sceneName = m_renderscene->GetScene().name;

				file::path filename = scene->GetScene()->GetSceneName().ToString();
				filename += std::to_wstring(i) + L".hdr";
				//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename.data());
				DirectX::SaveToHDRFile(*image.GetImage(0, 0, 0), filename.c_str());
			}
			for (int i = 0; i < indirectMaps.size(); i++) {
				DeviceState::g_pDeviceContext->CopyResource(imgTexture, indirectMaps[i]->m_pTexture);

				// �ؽ��� ����
				DirectX::ScratchImage image;
				HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, imgTexture, image);
				std::wstring filename = L"Indirect" + std::to_wstring(i) + L".hdr";
				//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename.data());
				DirectX::SaveToHDRFile(*image.GetImage(0, 0, 0), filename.data());
			}
			for (int i = 0; i < environmentMaps.size(); i++) {
				DeviceState::g_pDeviceContext->CopyResource(imgTexture, environmentMaps[i]->m_pTexture);

				// �ؽ��� ����
				DirectX::ScratchImage image;
				HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, imgTexture, image);
				std::wstring filename = L"Env" + std::to_wstring(i) + L".hdr";
				//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename.data());
				DirectX::SaveToHDRFile(*image.GetImage(0, 0, 0), filename.data());
			}
			for (int i = 0; i < directionalMaps.size(); i++) {
				DeviceState::g_pDeviceContext->CopyResource(imgTexture, directionalMaps[i]->m_pTexture);
				// �ؽ��� ����
				DirectX::ScratchImage image;
				HRESULT hr = DirectX::CaptureTexture(DeviceState::g_pDevice, DeviceState::g_pDeviceContext, imgTexture, image);
				file::path filename = L"Dir_";
				filename += scene->GetScene()->GetSceneName().ToString();
				filename += std::to_wstring(i) + L".hdr";
				//DirectX::SaveToWICFile(*image.GetImage(0, 0, 0), DirectX::WIC_FLAGS_NONE, GUID_ContainerFormatPng, filename.data());
				DirectX::SaveToHDRFile(*image.GetImage(0, 0, 0), filename.c_str());
			}
			imgTexture->Release();
			imgTexture = nullptr;
		}
		else {
			g_progressWindow->SetStatusText(L"Triangle could not be found.");
			g_progressWindow->SetProgress(100);
		}

		m_pLightMapPass->Initialize(lightmaps, directionalMaps);
		g_progressWindow->Close();
		co_return;
	}

	void LightMap::UnBindLightmapPS()
	{
		ID3D11ShaderResourceView* psLightmap = nullptr;
		DirectX11::PSSetShaderResources(14, 1, &psLightmap);
		DirectX11::PSSetShaderResources(15, 1, &psLightmap);
	}

	void LightMap::GenerateLightMap(
		RenderScene* scene,
		const std::unique_ptr<PositionMapPass>& m_pPositionMapPass,
		const std::unique_ptr<LightMapPass>& m_pLightMapPass
	)
	{
		StartCoroutine(GenerateLightmapCoroutine(scene, m_pPositionMapPass, m_pLightMapPass));
	}
}
#endif // !DYNAMICCPP_EXPORTS
/*
 - �ǿ��

 �߰� ���� ���� ����
 1. �ش� �˰������� shadow Atlas�� ���� �����ҵ�.
*/