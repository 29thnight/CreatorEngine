#pragma once
#include "IRenderPass.h"
#include "SceneObject.h"
struct UIParameters
{
	float time;
};

struct alignas(16) CanvasVertex
{
	Mathf::Vector4 Position;
	Mathf::Vector2 Size;
};

struct alignas(16) ModelConstantBuffer
{
	Mathf::Matrix world;
	Mathf::Matrix view;
	Mathf::Matrix projection;
};

class UIPass : public IRenderPass
{
public:
	UIPass();
	virtual ~UIPass() {}

	void Initialize(Texture* renderTargetView);
	void Update(float delta);

	void DrawCanvas(Mathf::Matrix world, Mathf::Matrix view, Mathf::Matrix projection);
	virtual void Excute(Scene& scene) abstract;



	CanvasVertex* mVertex;

	ID3D11Buffer* canVasVertexBuffer;

	ComPtr<ID3D11Buffer> m_ModelBuffer;
	ModelConstantBuffer m_ModelConstantBuffer{};

	Texture* m_renderTarget = nullptr;
	float m_delta;
	std::vector<SceneObject*> _2DObjects;
};

