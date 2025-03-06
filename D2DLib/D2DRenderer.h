#pragma once
#include "../Utility_Framework/DeviceResources.h"
#include "../RenderEngine/Shader.h"
#include "D2DBitmapScene.h"
#include "Canvas.h"
#include <map>

class World;

namespace DirectX11
{
	class D2DRenderer
	{
	public:
		D2DRenderer(const std::shared_ptr<DeviceResources>& deviceResources);
		~D2DRenderer();
		void SetEditorComputeShader();
		void UpdateDrawModel();
		void BeginDraw();
		void Render();
		void EndDraw();
		void LoadSceneRender(int levelIndex);

		Canvas* GetCurCanvas() { return m_CurrentCanvas; }
		void SetCurCanvas(Canvas* target) { m_CurrentCanvas = target; }

		void SetCanvasEditorStage();
		void ComputeCanvasEditorStage();
		void ImGuiRenderStage();

		void SetEditorMenu(World* pWorld)
		{
			_world = pWorld;
		}

	private:
		Canvas* m_CurrentCanvas{};
		World* _world{};
		std::shared_ptr<DeviceResources> m_DeviceResources{};
		UIObject m_LoadScene{};
		std::vector<UIObject*> m_drawBitmap;
		std::vector<TextUIObject*> m_drawText;
		uint32 m_drawBitmapSceneCount{};

		ComPtr<ID3D11Texture2D>                 _EditorInput;
		ComPtr<ID3D11Texture2D>					_EditorOutput;
		ComPtr<ID3D11UnorderedAccessView>		_EditorOutputUAV;
		ComPtr<ID3D11ShaderResourceView>        _EditorInputSRV;
		ComPtr<ID3D11ShaderResourceView>		_EditorOutputSRV;
		ComputeShader							_EditorComputeShader;
	};
}