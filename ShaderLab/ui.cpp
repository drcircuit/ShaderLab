#include "ui.h"
#include <sstream>
#define D3D_DEBUG_INFO
UI::UI() {
	textFormat = nullptr;
	brush = nullptr;
}

UI::~UI() {
	if(textFormat) textFormat->Release();
	if (brush) brush->Release();
}
bool UI::Init(ID3D11Device* device, ID3D11DeviceContext* deviceContext, IDXGISwapChain* swapChain) {
	// Store Direct3D resources
	this->device = device;
	this->deviceContext = deviceContext;
	this->swapChain = swapChain;

	D2D1_FACTORY_OPTIONS options;
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
	// Create Direct2D factory
	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), &options, &factory);
	if (FAILED(hr)) {
		return false;
	}

	// Create DirectWrite factory
	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), &dwriteFactory);
	if (FAILED(hr)) {
		return false;
	}

	ID3D11Device1* dev1 = nullptr;
	hr = device->QueryInterface(__uuidof(ID3D11Device1), (void**)&dev1);
	if (FAILED(hr)) {
		// Handle error
		return false;
	}

	IDXGIDevice* dxgiDevice = nullptr;
	hr = dev1->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice);
	if (FAILED(hr)) {
		dev1->Release();
		return false;
	}

	// Explicitly define Direct2D creation properties
	D2D1_CREATION_PROPERTIES props = {};
	props.debugLevel = D2D1_DEBUG_LEVEL_NONE;
	props.options = D2D1_DEVICE_CONTEXT_OPTIONS_NONE;
	props.threadingMode = D2D1_THREADING_MODE_SINGLE_THREADED;

	// Create the Direct2D device
	hr = D2D1CreateDevice(dxgiDevice, &props, &d2dDevice);
	if (FAILED(hr)) {
		// Output the error code for debugging purposes
		std::wstringstream ss;
		ss << L"Failed to create D2D1 device. Error: " << hr;
		OutputDebugString(ss.str().c_str());

		dxgiDevice->Release();
		dev1->Release();
		return false;
	}

	// Make sure that dxgiDevice, d2dDevice, and d2dDeviceContext are already initialized properly
	IDXGISurface* dxgiSurface = nullptr;
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&dxgiSurface));
	if (FAILED(hr)) {
		// Handle error
		OutputDebugString(L"Failed to get DXGI surface from swap chain.\n");
		// Release acquired interfaces before returning
		if (dxgiDevice) dxgiDevice->Release();
		if (dev1) dev1->Release();
		return false;
	}
	// Create the Direct2D device context
	hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dDeviceContext);
	if (FAILED(hr)) {
		// Handle error
		d2dDevice->Release();
		dxgiDevice->Release();
		dev1->Release();
		return false;
	}

	// Create rendertarget using CreateDxgiSurfaceRenderTarget ;

	hr = factory->CreateDxgiSurfaceRenderTarget(dxgiSurface, D2D1::RenderTargetProperties(
		D2D1_RENDER_TARGET_TYPE_HARDWARE,
		D2D1::PixelFormat(DXGI_FORMAT_R8G8B8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
	), &renderTarget);

	if (FAILED(hr)) {
		OutputDebugString(L"Failed to create render target.\n");
		d2dDevice->Release();
		dxgiDevice->Release();
		dev1->Release();
		return false;
	}
	// Release the DXGI surface now that we're done with it
	dxgiSurface->Release();

	// Don't forget to release the other interfaces after you're done
	dxgiDevice->Release();
	dev1->Release();
	initialized = true;
	return true;
}
void UI::SetFont(const std::wstring& fontName, float fontSize) {
	// Release any existing text format
	if (textFormat) {
		textFormat->Release();
		textFormat = nullptr;
	}

	// Create a new text format using the DirectWrite factory
	HRESULT hr = dwriteFactory->CreateTextFormat(
		fontName.c_str(),
		nullptr,
		DWRITE_FONT_WEIGHT_NORMAL,
		DWRITE_FONT_STYLE_NORMAL,
		DWRITE_FONT_STRETCH_NORMAL,
		fontSize,
		L"en-us", // or your current locale
		&textFormat
	);

	if (FAILED(hr)) {
		// Handle error
		OutputDebugString(L"Failed to create text format.\n");
	}
}

void UI::DrawText(const std::wstring& text, float x, float y, const D2D1_COLOR_F& color) {
	if (!initialized || !textFormat) return;

	// Create brush if not created yet
	if (!brush) {
		HRESULT hr = renderTarget->CreateSolidColorBrush(color, &brush);
		if (FAILED(hr)) {
			// Handle error
			return;
		}
	}
	else {
		// Update brush color
		brush->SetColor(color);
	}

	// Begin drawing using Direct2D
	renderTarget->BeginDraw();
	renderTarget->SetTransform(D2D1::Matrix3x2F::Identity());

	// Define the text rectangle
	D2D1_RECT_F rect = D2D1::RectF(x, y, x + 1000, y + 1000); // Adjust size as needed

	
	// Draw the text
	renderTarget->DrawTextW(text.c_str(), static_cast<UINT32>(text.length()), textFormat, rect, brush);

	// End drawing
	HRESULT hr = renderTarget->EndDraw();
	if (FAILED(hr)) {
		// Handle error
	}
}
