#pragma once
#include <d2d1.h>
#include <dwrite.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <string>
#include <d2d1_1.h>
#include <wincodec.h>
#include <dwrite_1.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#include <wrl/client.h>  // For Microsoft::WRL::ComPtr
// utility class to use DirectWrite to draw text on a DXDGI swap chain buffer, all Draw methods wrap begin and end draw


class UI {

public:
	UI();
	~UI();
	bool Init(ID3D11Device* device, ID3D11DeviceContext* deviceContext, IDXGISwapChain* swapChain);
	void SetFont(const std::wstring& fontName, float fontSize);
	void DrawText(const std::wstring& text, float x, float y, const D2D1_COLOR_F& color);
	
private:
	Microsoft::WRL::ComPtr<ID3D11Device> device;
	Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext;
	Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain;
	Microsoft::WRL::ComPtr<ID2D1Factory> factory;
	Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice;
	Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dDeviceContext;
	Microsoft::WRL::ComPtr<IDXGISurface> dxgiSurface;
	Microsoft::WRL::ComPtr<ID2D1RenderTarget> renderTarget;
	Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
	std::wstring fontName = L"";
	float fontSize = 10.0f;
	bool initialized = false;
	IDWriteTextFormat* textFormat;
	ID2D1SolidColorBrush* brush;
};