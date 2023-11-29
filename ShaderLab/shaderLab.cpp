#include <Windows.h>
#include <windowsx.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include "timer.h"
#include <DirectXMath.h>
#include <d2d1.h>
#include <d2d1helper.h>
#include <dwrite.h>
#include <wincodec.h>
#include "ui.h"

#define D3D_DEBUG_INFO
#pragma comment(lib, "D3DCompiler.lib")
#pragma comment(lib, "d3d11.lib")

#define WWIDTH 1920
#define WHEIGHT 1080
// Global Declarations - Others
IDXGISwapChain* swapChain;             // the pointer to the swap chain interface
ID3D11Device* dev;                     // the pointer to our Direct3D device interface
ID3D11DeviceContext* devcon;           // the pointer to our Direct3D device context
ID3D11RenderTargetView* backbuffer;    // the pointer to our back buffer
ID3D11VertexShader* vertexShader = nullptr;
ID3D11PixelShader* pixelShader = nullptr;
ID3D11Buffer* vertexBuffer = nullptr;
ID3D11InputLayout* inputLayout = NULL;
ID3D11RasterizerState* rasterizerState = nullptr;
ID3D11Texture2D* pBackBuffer;
UI* ui;
Timer timer;
// Declare constant buffer pointers
ID3D11Buffer* timeConstantBuffer;
ID3D11Buffer* resolutionConstantBuffer;

float aspectRatio = 1.85;
float viewportWidth = static_cast<float>(WWIDTH);
float viewportHeight = static_cast<float>(WHEIGHT);
// Define the Vertex structure
struct Vertex {
	float x, y, z;
};

// Define the constant buffer structures
struct TimeBuffer
{
	float elapsedTime;
	float padding[3];
};

struct ResolutionBuffer
{
	DirectX::XMFLOAT2 resolution;
	float padding[2];
};

// Global array of vertices for a quad using triangle strips

Vertex vertices[] = {
	{ -1.0f, -1.0f, 0.0f }, // 0 Bottom left
	{ -1.0f,  1.0f, 0.0f }, // 1 Top left
	{  1.0f, -1.0f, 0.0f }, // 2 Bottom right
	{  1.0f,  1.0f, 0.0f }  // 3 Top right
};

// Function prototypes
void InitD3D(HWND hWnd);    // sets up and initializes Direct3D
void InitViewport(ID3D11DeviceContext* devcon);
void InitVertexShader(ID3D11Device* dev, ID3D11VertexShader** vertexShader, ID3D11InputLayout** inputLayout);
void InitPixelShader(ID3D11Device* dev, ID3D11PixelShader** pixelShader);
void InitVertexBuffer(ID3D11Device* dev, ID3D11Buffer** vertexBuffer);
void InitTimeBuffer(ID3D11Device* dev, ID3D11Buffer** timeConstantBuffer);
void InitResolutionBuffer(ID3D11Device* dev, ID3D11Buffer** resolutionConstantBuffer);
void CleanD3D(void);        // closes Direct3D and releases memory
void RenderFrame(void);     // renders a single frame



// the WindowProc function prototype
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

// the entry point for any Windows program
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {

	HWND hWnd;
	WNDCLASSEX wc;
	timer = Timer();
	HRESULT hr = CoInitializeEx(NULL, COINITBASE_MULTITHREADED);
	if (FAILED(hr)) {
		return -2;
	}

	ZeroMemory(&wc, sizeof(WNDCLASSEX));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = hInstance;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.lpszClassName = L"WindowClass1";

	RegisterClassEx(&wc);

	hWnd = CreateWindowEx(0, L"WindowClass1", L"Our First Direct3D Program",
		WS_OVERLAPPEDWINDOW, 0, 0, viewportWidth, viewportHeight, NULL, NULL, hInstance, NULL);

	ShowWindow(hWnd, nCmdShow);

	// set up and initialize Direct3D
	InitD3D(hWnd);
	// Initialize UI
	ui = new UI();
	if (!ui->Init(dev, devcon, swapChain)) {
		// Handle UI initialization failure
		delete ui;
		return -1;
	}
	ui->SetFont(L"Arial", 24.0f);
	// enter the main loop:

	MSG msg;
	timer.Start();
	while (TRUE) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);

			if (msg.message == WM_QUIT)
				break;
		}

		RenderFrame();

	}

	// clean up DirectX and COM
	delete ui;
	CleanD3D();

	CoUninitialize();
	return msg.wParam;
}

// this is the main message handler for the program
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam) {
	switch (message) {
	// exit on ESC key press
	case WM_KEYDOWN: {
		if (wParam == VK_ESCAPE) {
			DestroyWindow(hWnd);
		}
	} break;
	case WM_DESTROY: {
		PostQuitMessage(0);
		ShowCursor(true);
		return 0;
	} break;
	case WM_SIZE: {
		InitViewport(devcon);
		return 0;
	} break;
	}

	return DefWindowProc(hWnd, message, wParam, lParam);
}

// this function initializes and prepares Direct3D for use
void InitD3D(HWND hWnd) {
	// create a struct to hold information about the swap chain
	DXGI_SWAP_CHAIN_DESC scd;
	ZeroMemory(&scd, sizeof(DXGI_SWAP_CHAIN_DESC));


	// calculate height based on aspect ratio and current screen height

	int screenHeight = GetSystemMetrics(SM_CYSCREEN);
	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int height = screenWidth;
	int width = static_cast<int>(height * aspectRatio);

	scd.BufferCount = 2; // Use two or more buffers
	scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	scd.BufferDesc.Width = width;
	scd.BufferDesc.Height = height;
	scd.OutputWindow = hWnd;
	scd.SampleDesc.Count = 1;
	scd.Windowed = FALSE;
	scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Use flip-model swap effect
	scd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // If you want to allow full-screen switching
	scd.BufferDesc.Scaling = DXGI_MODE_SCALING_CENTERED;

	// hide mouse cursor 
	ShowCursor(false);
	// ... [Configure your swap chain description]

	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_11_1,
		// ... [other feature levels as necessary]
	};

	D3D_FEATURE_LEVEL selectedFeatureLevel;

	HRESULT hr = D3D11CreateDeviceAndSwapChain(
		nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG, // Important for Direct2D interop
		featureLevels, ARRAYSIZE(featureLevels),
		D3D11_SDK_VERSION, &scd, &swapChain, &dev, &selectedFeatureLevel, &devcon);

	if (FAILED(hr)) {
		// Handle error
	}
	// get the address of the back buffer

	swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);

	// use the back buffer address to create the render target
	dev->CreateRenderTargetView(pBackBuffer, NULL, &backbuffer);
	pBackBuffer->Release();

	// set the render target as the back buffer
	devcon->OMSetRenderTargets(1, &backbuffer, NULL);

	InitViewport(devcon);
	InitTimeBuffer(dev, &timeConstantBuffer);
	InitResolutionBuffer(dev, &resolutionConstantBuffer);
	// Initialize the vertex shader and input layout
	InitVertexShader(dev, &vertexShader, &inputLayout);

	// Initialize the pixel shader
	InitPixelShader(dev, &pixelShader);

	// Initialize the vertex buffer
	InitVertexBuffer(dev, &vertexBuffer);

	// Define the rasterizer state for wireframe rendering
	D3D11_RASTERIZER_DESC rasterDesc;
	ZeroMemory(&rasterDesc, sizeof(D3D11_RASTERIZER_DESC));
	rasterDesc.FillMode = D3D11_FILL_SOLID; // Set the fill mode to wireframe
	rasterDesc.CullMode = D3D11_CULL_NONE;      // Disable culling
	rasterDesc.DepthClipEnable = true;          // Enable depth clipping
	rasterDesc.FrontCounterClockwise = false;
	hr = dev->CreateRasterizerState(&rasterDesc, &rasterizerState);
	if (FAILED(hr)) {
		// Handle error
	}


}

void InitTimeBuffer(ID3D11Device* dev, ID3D11Buffer** timeBuffer) {
	// Create constant buffer for time
	D3D11_BUFFER_DESC timeBufferDesc;
	ZeroMemory(&timeBufferDesc, sizeof(timeBufferDesc));
	timeBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	timeBufferDesc.ByteWidth = sizeof(TimeBuffer);
	timeBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	HRESULT hr = dev->CreateBuffer(&timeBufferDesc, nullptr, timeBuffer);
	if (FAILED(hr)) {
		// Handle error or add a breakpoint here for debugging
		if (hr == E_INVALIDARG) {
			// Print additional information about the invalid argument
			OutputDebugStringA("Error: Invalid argument in CreateBuffer\n");

			// Print information about the buffer description
			OutputDebugStringA("Buffer Description:\n");
			char buffer[256];
			sprintf_s(buffer, "Usage: %d\n", timeBufferDesc.Usage);
			OutputDebugStringA(buffer);
			sprintf_s(buffer, "ByteWidth: %d\n", timeBufferDesc.ByteWidth);
			OutputDebugStringA(buffer);
			sprintf_s(buffer, "BindFlags: %d\n", timeBufferDesc.BindFlags);
			OutputDebugStringA(buffer);
		}
		else {
			// Print a generic error message
			OutputDebugStringA("Error creating time constant buffer\n");
		}
	}
}

void InitResolutionBuffer(ID3D11Device* dev, ID3D11Buffer** resolutionBuffer) {
	// Create constant buffer for resolution
	D3D11_BUFFER_DESC resolutionBufferDesc;
	ZeroMemory(&resolutionBufferDesc, sizeof(resolutionBufferDesc));
	resolutionBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	resolutionBufferDesc.ByteWidth = sizeof(ResolutionBuffer);
	resolutionBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

	HRESULT hr = dev->CreateBuffer(&resolutionBufferDesc, nullptr, resolutionBuffer);
	if (FAILED(hr)) {
		// Handle error or add a breakpoint here for debugging
		if (hr == E_INVALIDARG) {
			// Print additional information about the invalid argument
			OutputDebugStringA("Error: Invalid argument in CreateBuffer\n");

			// Print information about the buffer description
			OutputDebugStringA("Buffer Description:\n");
			char buffer[256];
			sprintf_s(buffer, "Usage: %d\n", resolutionBufferDesc.Usage);
			OutputDebugStringA(buffer);
			sprintf_s(buffer, "ByteWidth: %d\n", resolutionBufferDesc.ByteWidth);
			OutputDebugStringA(buffer);
			sprintf_s(buffer, "BindFlags: %d\n", resolutionBufferDesc.BindFlags);
			OutputDebugStringA(buffer);
		}
		else {
			// Print a generic error message
			OutputDebugStringA("Error creating resolution constant buffer\n");
		}
	}
}

void InitViewport(ID3D11DeviceContext* devcon) {
	if (!devcon) return;
	// Set the viewport
	D3D11_TEXTURE2D_DESC backbufferDesc;
	pBackBuffer->GetDesc(&backbufferDesc);

	D3D11_VIEWPORT viewport;
	ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));
	float newWidth = backbufferDesc.Width * aspectRatio;
	float offset = (backbufferDesc.Width - newWidth) / 2;
	viewport.TopLeftX = offset;
	viewport.TopLeftY = 0;
	viewport.Width = newWidth;  // Use static_cast to avoid potential issues
	viewport.Height = backbufferDesc.Height; // Use static_cast to avoid potential issues
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	devcon->RSSetViewports(1, &viewport);

	viewportWidth = viewport.Width;
	viewportHeight = viewport.Height;
}

void InitVertexShader(ID3D11Device* dev, ID3D11VertexShader** vertexShader, ID3D11InputLayout** inputLayout) {
	// Load and compile the vertex shader
	ID3DBlob* vertexShaderBuffer = nullptr;
	D3DCompileFromFile(L"VertexShader.hlsl", nullptr, nullptr, "main", "vs_5_0", 0, 0, &vertexShaderBuffer, nullptr);
	dev->CreateVertexShader(vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), nullptr, vertexShader);

	// Define the input layout
	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	dev->CreateInputLayout(layout, ARRAYSIZE(layout), vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), inputLayout);

	// Clean up
	vertexShaderBuffer->Release();
}

void InitPixelShader(ID3D11Device* dev, ID3D11PixelShader** pixelShader) {
	// Load and compile the pixel shader
	ID3DBlob* pixelShaderBuffer = nullptr;
	D3DCompileFromFile(L"PixelShader.hlsl", nullptr, nullptr, "main", "ps_5_0", 0, 0, &pixelShaderBuffer, nullptr);
	dev->CreatePixelShader(pixelShaderBuffer->GetBufferPointer(), pixelShaderBuffer->GetBufferSize(), nullptr, pixelShader);

	// Clean up
	pixelShaderBuffer->Release();
}

void InitVertexBuffer(ID3D11Device* dev, ID3D11Buffer** vertexBuffer) {
	// Create the vertex buffer description
	D3D11_BUFFER_DESC vertexBufferDesc;
	ZeroMemory(&vertexBufferDesc, sizeof(vertexBufferDesc));
	vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	vertexBufferDesc.ByteWidth = sizeof(vertices);
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;

	// Define the resource data
	D3D11_SUBRESOURCE_DATA vertexBufferData;
	ZeroMemory(&vertexBufferData, sizeof(vertexBufferData));
	vertexBufferData.pSysMem = vertices;

	// Create the vertex buffer
	HRESULT hr = dev->CreateBuffer(&vertexBufferDesc, &vertexBufferData, vertexBuffer);
	if (FAILED(hr)) {
		// Handle error
	}
}

// this is the function used to render a single frame
void RenderFrame(void) {
	// Calculate elapsed time
	auto elapsedTime = timer.GetMilisecondsElapsed();
	// Update time constant buffer
	devcon->UpdateSubresource(timeConstantBuffer, 0, NULL, &elapsedTime, 0, 0);
	devcon->PSSetConstantBuffers(0, 1, &timeConstantBuffer);
	ResolutionBuffer resolutionData;
	resolutionData.resolution.x = viewportWidth;
	resolutionData.resolution.y = viewportHeight;
	devcon->UpdateSubresource(resolutionConstantBuffer, 0, NULL, &resolutionData, 0, 0);
	devcon->PSSetConstantBuffers(1, 1, &resolutionConstantBuffer);
	// clear the back buffer to a deep blue
	devcon->OMSetRenderTargets(1, &backbuffer, nullptr);

	float clearColor[4] = { 0.0f, 0.2f, 0.4f, 1.0f }; // RGBA values
	devcon->ClearRenderTargetView(backbuffer, clearColor);
	devcon->RSSetState(rasterizerState);
	// do 3D rendering on the back buffer here
	// Set the vertex buffer
	UINT stride = sizeof(Vertex);
	UINT offset = 0;
	UINT numVertices = 4;
	devcon->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);

	// Set the primitive topology for the input assembler stage

	// Bind the shaders to the context
	devcon->VSSetShader(vertexShader, nullptr, 0);
	devcon->PSSetShader(pixelShader, nullptr, 0);
	devcon->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
	devcon->IASetInputLayout(inputLayout);
	devcon->Draw(numVertices, 0);
	devcon->OMSetRenderTargets(1, &backbuffer, nullptr);

	// draw FPS counter on top to the left

	// draw text

	// Draw FPS counter
	int fps = timer.GetFPS();
	std::wstring fpsString = std::to_wstring(fps) + L" FPS";
	ui->DrawText(fpsString, 10.0f, 10.0f, D2D1::ColorF(1.0f, 1.0f, 1.0f));
	swapChain->Present(0, 0);
	timer.Tick();
}

// this is the function that cleans up Direct3D and COM
void CleanD3D(void) {
	// close and release all existing COM objects
	swapChain->Release();
	backbuffer->Release();
	dev->Release();
	devcon->Release();
	if (vertexBuffer) vertexBuffer->Release();
	if (vertexShader) vertexShader->Release();
	if (pixelShader) pixelShader->Release();
	if (inputLayout) inputLayout->Release();
	if (timeConstantBuffer) timeConstantBuffer->Release();
	if (resolutionConstantBuffer) resolutionConstantBuffer->Release();
}