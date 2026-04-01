#include "ShaderLab/UI/ShaderLabIDE.h"

#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

#include "ShaderLab/UI/UIConfig.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Swapchain.h"
#include "ShaderLab/Core/DxcCompilationService.h"

#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

namespace ShaderLab {

namespace fs = std::filesystem;

bool ShaderLabIDE::Initialize(HWND hwnd, Device* device, Swapchain* swapchain) {
    if (!device || !device->IsValid() || !swapchain || !hwnd) {
        return false;
    }

    m_hwnd = hwnd;

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_context = ImGui::CreateContext();
    ImGui::SetCurrentContext(m_context);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    float dpiScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
    if (dpiScale < 1.0f) {
        dpiScale = 1.0f;
    }
    m_dpiScale = dpiScale;

    // Load custom fonts for editor
    // Path to fonts relative to executable (in editor_assets/fonts/)
    std::string fontPath = m_appRoot + "/editor_assets/fonts/";
    std::string iconFontFile;
    const std::vector<fs::path> iconFontCandidates = {
        fs::path(m_appRoot) / "third_party" / "OpenFontIcons" / UIConfig::FontFileOpenFontIcons,
        fs::path(m_appRoot) / "editor_assets" / "fonts" / UIConfig::FontFileOpenFontIcons,
        fs::path(m_appRoot) / ".." / "third_party" / "OpenFontIcons" / UIConfig::FontFileOpenFontIcons
    };
    for (const auto& candidate : iconFontCandidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && !ec) {
            iconFontFile = candidate.lexically_normal().string();
            break;
        }
    }

    // Hacked font for logo (large)
    m_fontHackedLogo = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileHacked).c_str(), UIConfig::FontLogo * dpiScale);
    if (!m_fontHackedLogo) {
        // Fallback to default if font doesn't load
        m_fontHackedLogo = io.Fonts->AddFontDefault();
    }

    // Hacked font for headings (medium)
    m_fontHackedHeading = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileHacked).c_str(), UIConfig::FontHeading * dpiScale);
    if (!m_fontHackedHeading) {
        m_fontHackedHeading = io.Fonts->AddFontDefault();
    }

    // Orbitron for regular text
    m_fontOrbitronText = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileOrbitron).c_str(), UIConfig::FontText * dpiScale);
    if (!m_fontOrbitronText) {
        m_fontOrbitronText = io.Fonts->AddFontDefault();
    }
    static const ImWchar iconRanges[] = { 0xE000, 0xE0FF, 0 };
    constexpr float iconFontScale = 1.22f;
    constexpr float iconGlyphOffsetY = 1.0f;
    ImFontConfig iconConfigText;
    iconConfigText.MergeMode = true;
    iconConfigText.PixelSnapH = true;
    iconConfigText.GlyphOffset.y = iconGlyphOffsetY;
    ImFont* iconTextMerge = nullptr;
    if (!iconFontFile.empty()) {
        iconTextMerge = io.Fonts->AddFontFromFileTTF(iconFontFile.c_str(), UIConfig::FontText * iconFontScale * dpiScale, &iconConfigText, iconRanges);
    }

    // Erbos Draco for numerical fields
    m_fontErbosDracoNumbers = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileErbosOpen).c_str(), UIConfig::FontNumeric * dpiScale);
    if (!m_fontErbosDracoNumbers) {
        m_fontErbosDracoNumbers = io.Fonts->AddFontDefault();
    }

    // Orbitron for menu (smaller)
    m_fontMenuSmall = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileOrbitron).c_str(), UIConfig::FontMenu * dpiScale);
    if (!m_fontMenuSmall) {
        m_fontMenuSmall = io.Fonts->AddFontDefault();
    }
    ImFontConfig iconConfigMenu;
    iconConfigMenu.MergeMode = true;
    iconConfigMenu.PixelSnapH = true;
    iconConfigMenu.GlyphOffset.y = iconGlyphOffsetY;
    ImFont* iconMenuMerge = nullptr;
    if (!iconFontFile.empty()) {
        iconMenuMerge = io.Fonts->AddFontFromFileTTF(iconFontFile.c_str(), UIConfig::FontMenu * iconFontScale * dpiScale, &iconConfigMenu, iconRanges);
    }

    (void)iconTextMerge;
    (void)iconMenuMerge;

    const float codeFontSizes[5] = { 11.0f, 12.0f, 13.0f, 15.0f, 17.0f };
    for (int i = 0; i < 5; ++i) {
        m_fontCodeSizes[i] = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileCode).c_str(), codeFontSizes[i] * dpiScale);
        if (!m_fontCodeSizes[i]) {
            m_fontCodeSizes[i] = io.Fonts->AddFontDefault();
        }
    }
    m_fontCode = m_fontCodeSizes[(int)CodeFontSize::M];

    m_fontCodeItalic = io.Fonts->AddFontFromFileTTF((fontPath + UIConfig::FontFileCodeItalic).c_str(), UIConfig::FontCode * dpiScale);
    if (!m_fontCodeItalic) {
        m_fontCodeItalic = m_fontCode;
    }

    // Build font atlas
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    // Set Orbitron as the default font for the UI
    if (m_fontOrbitronText) {
        io.FontDefault = m_fontOrbitronText;
    }

    if (m_fontCode) {
        m_textEditor.SetCommentFont(m_fontCode, m_fontCode->LegacySize);
        m_snippetTextEditor.SetCommentFont(m_fontCode, m_fontCode->LegacySize);
        m_tutCodeTextEditor.SetCommentFont(m_fontCode, m_fontCode->LegacySize);
        m_ubershaderTextEditor.SetCommentFont(m_fontCode, m_fontCode->LegacySize);
    }

    // Setup style
    SetupImGuiStyle();
    if (dpiScale > 1.0f) {
        ImGui::GetStyle().ScaleAllSizes(dpiScale);
    }

    // Create descriptor heap for ImGui
    CreateDescriptorHeap(device);

    // Setup platform/renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX12_Init(
        device->GetDevice(),
        Swapchain::BUFFER_COUNT,
        DXGI_FORMAT_R8G8B8A8_UNORM,
        m_srvHeap.Get(),
        m_srvHeap->GetCPUDescriptorHandleForHeapStart(),
        m_srvHeap->GetGPUDescriptorHandleForHeapStart()
    );

    // Store device reference for texture creation
    m_deviceRef = device;
    m_swapchainRef = swapchain;
    m_compilationService = std::make_unique<DxcCompilationService>();
    CreateTitlebarIconTexture();

    if (m_workspaceSelectionPromptPending) {
        m_workspaceSelectionPromptPending = false;
        ChooseWorkspaceFolder();
        if (!m_workspaceExplicitlyConfigured) {
            AppendDemoLog(std::string("[workspace] No workspace selected; using default ") + m_workspaceRootPath);
        }
    }

    m_initialized = true;
    return true;
}

void ShaderLabIDE::CreateDescriptorHeap(Device* device) {
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = 128;  // ImGui font (0), Preview (1), Thumbnails (2+)
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_srvHeap));
}

} // namespace ShaderLab
