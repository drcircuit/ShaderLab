#include "ShaderLab/App/DemoPlayer.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Swapchain.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"
#include "ShaderLab/Shader/ShaderCompiler.h"
#include "ShaderLab/Runtime/RuntimeStartupPolicy.h"
#include <d3dcompiler.h>

#if SHADERLAB_TINY_PLAYER && !defined(SHADERLAB_TINY_DEMOPLAYER_BRIDGE)
#error Shared runtime DemoPlayer.cpp is non-tiny only. Use src/app/tiny/TinyDemoPlayer.cpp for tiny builds.
#endif

#if !SHADERLAB_TINY_PLAYER
#include "ShaderLab/Audio/AudioSystem.h"
#endif
#if !SHADERLAB_TINY_PLAYER
#include "ShaderLab/Core/Serializer.h"
#endif
#include "ShaderLab/Core/PackageManager.h" 
#include "stb_image.h"
#include <windows.h>
#include <cmath>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <array>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#if !SHADERLAB_TINY_PLAYER
#include <iostream>
#endif
#include <fstream>
#if !SHADERLAB_TINY_PLAYER
#include <filesystem>
#endif

#ifndef SHADERLAB_RUNTIME_IMGUI
#define SHADERLAB_RUNTIME_IMGUI 1
#endif

#ifndef SHADERLAB_RUNTIME_DEBUG_LOG
#define SHADERLAB_RUNTIME_DEBUG_LOG 0
#endif

#ifndef SHADERLAB_COMPACT_TRACK_DEBUG
#define SHADERLAB_COMPACT_TRACK_DEBUG 0
#endif

#ifndef SHADERLAB_TINY_TRACE
#define SHADERLAB_TINY_TRACE 0
#endif

#ifndef SHADERLAB_TINY_DEV_OVERLAY
#define SHADERLAB_TINY_DEV_OVERLAY 0
#endif

#ifndef SHADERLAB_TINY_RUNTIME_COMPILE
#define SHADERLAB_TINY_RUNTIME_COMPILE 0
#endif

#if SHADERLAB_RUNTIME_IMGUI
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#endif

namespace ShaderLab {

#if !SHADERLAB_TINY_PLAYER
namespace fs = std::filesystem;
#endif

static const int kPostFxHistoryCount = 4;
static const int kMaxPostFxChain = 32;
static const char* kPackedVertexShaderPath = "assets/shaders/vertex.cso";
static const char* kPackedMicroUbershaderBytecodePath = "assets/shaders/ubershader.bin";

static constexpr size_t kTransitionSlotCount = 6;
static constexpr const char* kTransitionSlotStems[kTransitionSlotCount] = {
    "crossfade",
    "dip_to_black",
    "fade_out",
    "fade_in",
    "glitch",
    "pixelate"
};

static constexpr uint32_t kFullscreenRenderPresetWidths[] = {
    0u, 4096u, 2560u, 2048u, 1920u, 1600u, 1280u, 1024u, 800u, 640u
};

static float AspectRatioValueFromPreset(RenderAspectRatioPreset preset) {
    switch (preset) {
        case RenderAspectRatioPreset::Ratio_1_1:
            return 1.0f;
        case RenderAspectRatioPreset::Ratio_16_10:
            return 16.0f / 10.0f;
        case RenderAspectRatioPreset::Ratio_4_3:
            return 4.0f / 3.0f;
        case RenderAspectRatioPreset::Ratio_16_9:
        default:
            return 16.0f / 9.0f;
    }
}

static uint32_t BaseWidthFromFullscreenPreset(FullscreenRenderResolutionPreset preset) {
    const uint32_t idx = static_cast<uint32_t>(preset);
    if (idx >= static_cast<uint32_t>(std::size(kFullscreenRenderPresetWidths))) {
        return 0u;
    }
    return kFullscreenRenderPresetWidths[idx];
}

static FullscreenRenderResolutionPreset FullscreenPresetFromCompactByte(uint8_t value) {
    const uint8_t resolutionBits = static_cast<uint8_t>(value & 0x0Fu);
    if (resolutionBits >= static_cast<uint8_t>(std::size(kFullscreenRenderPresetWidths))) {
        return FullscreenRenderResolutionPreset::Full;
    }
    return static_cast<FullscreenRenderResolutionPreset>(resolutionBits);
}

static RenderAspectRatioPreset AspectPresetFromCompactByte(uint8_t value) {
    const uint8_t aspectBits = static_cast<uint8_t>((value >> 4u) & 0x03u);
    if (aspectBits > static_cast<uint8_t>(RenderAspectRatioPreset::Ratio_4_3)) {
        return RenderAspectRatioPreset::Ratio_16_9;
    }
    return static_cast<RenderAspectRatioPreset>(aspectBits);
}

static bool ComputeFullscreenRenderResolution(const ProjectData& project,
                                              uint32_t fallbackWidth,
                                              uint32_t fallbackHeight,
                                              uint32_t& outWidth,
                                              uint32_t& outHeight) {
    const uint32_t baseWidth = BaseWidthFromFullscreenPreset(project.fullscreenRenderResolutionPreset);
    if (baseWidth == 0u) {
        outWidth = fallbackWidth;
        outHeight = fallbackHeight;
        return outWidth > 0u && outHeight > 0u;
    }

    const float aspect = AspectRatioValueFromPreset(project.renderAspectRatioPreset);
    if (aspect <= 0.0f) {
        return false;
    }

    const int computedHeight = static_cast<int>(std::lround(static_cast<double>(baseWidth) / static_cast<double>(aspect)));
    outWidth = baseWidth;
    outHeight = static_cast<uint32_t>((std::max)(1, computedHeight));
    return true;
}

static int TransitionSlotIndexFromStem(const std::string& transitionPresetStem);
static const char* GetTransitionPackedPathForStem(const std::string& transitionPresetStem);
static std::string GetTransitionShaderSourceForStem(const std::string& transitionPresetStem);

constexpr uint32_t kComputeHistorySlots = 8;
constexpr uint32_t kComputeDescriptorCount = 11; // t0 + t1..t8 + u0 + b0

#if !SHADERLAB_TINY_PLAYER
struct ComputeDispatchParams {
    float param0;
    float param1;
    float param2;
    float param3;
    float time;
    float invWidth;
    float invHeight;
    uint32_t frame;
};

struct RuntimeComputeSceneResources {
    ComPtr<ID3D12Resource> textureA;
    ComPtr<ID3D12Resource> textureB;
    uint32_t width = 0;
    uint32_t height = 0;
};

ComPtr<ID3D12RootSignature> g_runtimeComputeRootSignature;
ComPtr<ID3D12DescriptorHeap> g_runtimeComputeDescriptorHeap;
ComPtr<ID3D12Resource> g_runtimeComputeParamsBuffer;
uint8_t* g_runtimeComputeParamsMapped = nullptr;
std::unordered_map<int, RuntimeComputeSceneResources> g_runtimeComputeSceneResources;

uint32_t Align256(uint32_t value) {
    return (value + 255u) & ~255u;
}

UINT DescriptorStep(ID3D12Device* device) {
    return device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
}

bool CreateRuntimeUavTexture(Device* deviceRef, uint32_t width, uint32_t height, ComPtr<ID3D12Resource>& outTexture) {
    if (!deviceRef || width == 0 || height == 0) return false;

    D3D12_HEAP_PROPERTIES heapProps = { D3D12_HEAP_TYPE_DEFAULT };
    D3D12_RESOURCE_DESC texDesc = {};
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Width = width;
    texDesc.Height = height;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    return SUCCEEDED(deviceRef->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(outTexture.ReleaseAndGetAddressOf())));
}

bool EnsureRuntimeComputeRootSignature(Device* deviceRef) {
    if (!deviceRef) return false;
    if (g_runtimeComputeRootSignature) return true;

    D3D12_DESCRIPTOR_RANGE inputRange = {};
    inputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    inputRange.NumDescriptors = 1;
    inputRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE historyRange = {};
    historyRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    historyRange.NumDescriptors = kComputeHistorySlots;
    historyRange.BaseShaderRegister = 1;

    D3D12_DESCRIPTOR_RANGE outputRange = {};
    outputRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    outputRange.NumDescriptors = 1;
    outputRange.BaseShaderRegister = 0;

    D3D12_DESCRIPTOR_RANGE cbvRange = {};
    cbvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
    cbvRange.NumDescriptors = 1;
    cbvRange.BaseShaderRegister = 0;

    D3D12_ROOT_PARAMETER rootParams[4] = {};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[0].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[0].DescriptorTable.pDescriptorRanges = &inputRange;
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &historyRange;
    rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[2].DescriptorTable.pDescriptorRanges = &outputRange;
    rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[3].DescriptorTable.pDescriptorRanges = &cbvRange;
    rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.NumParameters = _countof(rootParams);
    rootDesc.pParameters = rootParams;
    rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    ComPtr<ID3DBlob> serialized;
    ComPtr<ID3DBlob> errors;
    if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                           serialized.GetAddressOf(), errors.GetAddressOf()))) {
        return false;
    }

    return SUCCEEDED(deviceRef->GetDevice()->CreateRootSignature(
        0,
        serialized->GetBufferPointer(),
        serialized->GetBufferSize(),
        IID_PPV_ARGS(g_runtimeComputeRootSignature.ReleaseAndGetAddressOf())));
}

bool EnsureRuntimeComputeDispatchResources(Device* deviceRef) {
    if (!deviceRef) return false;
    ID3D12Device* device = deviceRef->GetDevice();

    if (!g_runtimeComputeDescriptorHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = kComputeDescriptorCount;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(g_runtimeComputeDescriptorHeap.ReleaseAndGetAddressOf())))) {
            return false;
        }
    }

    if (!g_runtimeComputeParamsBuffer) {
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = Align256(static_cast<uint32_t>(sizeof(ComputeDispatchParams)));
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        if (FAILED(device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &bufferDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(g_runtimeComputeParamsBuffer.ReleaseAndGetAddressOf())))) {
            return false;
        }

        if (FAILED(g_runtimeComputeParamsBuffer->Map(0, nullptr, reinterpret_cast<void**>(&g_runtimeComputeParamsMapped)))) {
            g_runtimeComputeParamsBuffer.Reset();
            g_runtimeComputeParamsMapped = nullptr;
            return false;
        }
    }

    return true;
}
#endif

static std::string CanonicalTransitionStem(const std::string& transitionPresetStem) {
    std::string stem = transitionPresetStem;
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return stem;
}

static std::string GetDirectoryName(const std::string& path) {
    const size_t slash = path.find_last_of("\\/");
    if (slash == std::string::npos) {
        return {};
    }
    return path.substr(0, slash);
}

static std::string JoinPath(const std::string& base, const std::string& relativePath) {
    if (base.empty()) {
        return relativePath;
    }
    if (relativePath.empty()) {
        return base;
    }
    const bool baseHasSep = base.back() == '\\' || base.back() == '/';
    const bool relHasSep = relativePath.front() == '\\' || relativePath.front() == '/';
    if (baseHasSep && relHasSep) {
        return base + relativePath.substr(1);
    }
    if (baseHasSep || relHasSep) {
        return base + relativePath;
    }
    return base + "\\" + relativePath;
}

static std::string GetExecutableDirectory() {
    char exePath[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return GetDirectoryName(std::string(exePath, length));
}

static bool ReadFileBytesC(const std::string& path, std::vector<uint8_t>& outData) {
    outData.clear();
    if (path.empty()) {
        return false;
    }

    FILE* file = nullptr;
#if defined(_MSC_VER)
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) {
        return false;
    }
#else
    file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return false;
    }
#endif

    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return false;
    }
    const long size = std::ftell(file);
    if (size < 0) {
        std::fclose(file);
        return false;
    }
    if (std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return false;
    }

    outData.resize(static_cast<size_t>(size));
    if (!outData.empty()) {
        const size_t read = std::fread(outData.data(), 1, outData.size(), file);
        if (read != outData.size()) {
            outData.clear();
            std::fclose(file);
            return false;
        }
    }

    std::fclose(file);
    return !outData.empty();
}

static bool ReadFileTextC(const std::string& path, std::string& outText) {
    std::vector<uint8_t> bytes;
    if (!ReadFileBytesC(path, bytes)) {
        outText.clear();
        return false;
    }
    outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return !outText.empty();
}

static bool LoadBytecodeFromFile(const std::string& path, std::vector<uint8_t>& outData) {
    if (path.empty()) {
        return false;
    }
#if SHADERLAB_TINY_PLAYER
    return ReadFileBytesC(path, outData);
#else
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    outData.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return !outData.empty();
#endif
}

static bool LoadBytecodeFromPathCandidates(const std::string& relativeOrAbsolutePath,
                                           const std::string& manifestPath,
                                           std::vector<uint8_t>& outData,
                                           std::string* outResolvedPath = nullptr) {
    outData.clear();
    if (relativeOrAbsolutePath.empty()) {
        return false;
    }

    if (LoadBytecodeFromFile(relativeOrAbsolutePath, outData)) {
        if (outResolvedPath) *outResolvedPath = relativeOrAbsolutePath;
        return true;
    }

    const std::string manifestDir = GetDirectoryName(manifestPath);
    if (!manifestDir.empty()) {
        const std::string manifestCandidate = JoinPath(manifestDir, relativeOrAbsolutePath);
        if (LoadBytecodeFromFile(manifestCandidate, outData)) {
            if (outResolvedPath) *outResolvedPath = manifestCandidate;
            return true;
        }
    }

    const std::string exeDir = GetExecutableDirectory();
    if (!exeDir.empty()) {
        const std::string exeCandidate = JoinPath(exeDir, relativeOrAbsolutePath);
        if (LoadBytecodeFromFile(exeCandidate, outData)) {
            if (outResolvedPath) *outResolvedPath = exeCandidate;
            return true;
        }
    }

    return false;
}

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER

    outMissingPaths.clear();

    auto checkPackedPath = [&](const std::string& packedPath) {
        if (packedPath.empty()) {
            return;
        }
        if (!PackageManager::Get().HasFile(packedPath)) {
            outMissingPaths.push_back(packedPath);
        }
    };

    checkPackedPath(kPackedVertexShaderPath);

    for (const auto& scene : project.scenes) {
        checkPackedPath(scene.precompiledPath);
        for (const auto& fx : scene.postFxChain) {
            if (!fx.enabled) {
                continue;
            }
            checkPackedPath(fx.precompiledPath);
        }
    }

    bool usedTransitions[kTransitionSlotCount] = {};
    for (const auto& row : project.track.rows) {
        if (row.transitionPresetStem.empty()) {
            continue;
        }
        const int transitionIndex = TransitionSlotIndexFromStem(row.transitionPresetStem);
        if (transitionIndex >= 0 && transitionIndex < static_cast<int>(kTransitionSlotCount)) {
            usedTransitions[transitionIndex] = true;
        }
    }

    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        if (!usedTransitions[i]) {
            continue;
        }
        const char* packedPath = GetTransitionPackedPathForStem(kTransitionSlotStems[i]);
        if (packedPath && *packedPath) {
            checkPackedPath(packedPath);
        }
    }

    return outMissingPaths.empty();
}
#endif

std::string DemoPlayer::GetTransitionShader(const std::string& transitionPresetStem) {
#if SHADERLAB_TINY_PLAYER
    (void)transitionPresetStem;
    if (!m_project.scenes.empty() && !m_project.scenes[0].shaderCode.empty()) {
        return m_project.scenes[0].shaderCode;
    }
#endif
    return GetTransitionShaderSourceForStem(transitionPresetStem);
}

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
static bool DebugConsoleEnabled() {
    static int cached = -1;
    if (cached == -1) {
        char value[2] = {};
        cached = (GetEnvironmentVariableA("SHADERLAB_DEBUG_CONSOLE", value, sizeof(value)) > 0) ? 1 : 0;
    }
    return cached == 1 && GetConsoleWindow() != nullptr;
}

static void DebugLog(const std::string& message) {
    if (!DebugConsoleEnabled()) return;
    std::cout << message << std::endl;
}

static void DebugLogError(const std::string& message) {
    if (!DebugConsoleEnabled()) return;
    std::cerr << message << std::endl;
}

static std::string HResultHex(HRESULT hr) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << static_cast<unsigned int>(hr);
    return stream.str();
}

static const char* TransportStateLabel(TransportState state) {
    switch (state) {
        case TransportState::Stopped: return "Stopped";
        case TransportState::Playing: return "Playing";
        case TransportState::Paused: return "Paused";
    }
    return "Unknown";
}
#else
static void DebugLog(const std::string& message) {
    (void)message;
}

static void DebugLogError(const std::string& message) {
    (void)message;
}
#endif

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
#define SHADERLAB_RT_DEBUG_LOG(msgExpr) do { DebugLog((msgExpr)); } while (0)
#define SHADERLAB_RT_DEBUG_LOG_ERROR(msgExpr) do { DebugLogError((msgExpr)); } while (0)
#else
#define SHADERLAB_RT_DEBUG_LOG(msgExpr) do { } while (0)
#define SHADERLAB_RT_DEBUG_LOG_ERROR(msgExpr) do { } while (0)
#endif

#if SHADERLAB_TINY_TRACE
static void TinyTraceImpl(const std::string& message) {
    std::string line = "[ShaderLabTiny] " + message + "\n";
    OutputDebugStringA(line.c_str());
}
#define TinyTrace(messageExpr) do { TinyTraceImpl((messageExpr)); } while (0)
#else
#define TinyTrace(messageExpr) do { } while (0)
#endif

static inline void RuntimeErr(const char* code, const char* shortText) {
#if !SHADERLAB_TINY_PLAYER
    RuntimeStartupPolicy::EmitRuntimeError(code, shortText);
#else
    (void)code;
    (void)shortText;
#endif
}

#if SHADERLAB_TINY_DEV_OVERLAY
static std::vector<std::string>& TinyDevLogLines() {
    static std::vector<std::string> sTinyDevLogLines;
    return sTinyDevLogLines;
}

static void TinyDevLogAppend(const std::string& message) {
    auto& lines = TinyDevLogLines();
    constexpr size_t kTinyDevLogMaxLines = 256;
    if (lines.size() >= kTinyDevLogMaxLines) {
        lines.erase(lines.begin(), lines.begin() + 1);
    }
    lines.push_back(message);
}

static void TinyDevTrace(const std::string& message) {
    TinyDevLogAppend(message);
    TinyTrace(message);
}
#else
static void TinyDevTrace(const std::string& message) {
    (void)message;
    TinyTrace(message);
}
#endif

static void SetCompactTrackDecodeError(std::string& outError, const char* message) {
#if SHADERLAB_COMPACT_TRACK_DEBUG
    outError = message;
#else
    (void)message;
    outError.clear();
#endif
}

#if SHADERLAB_TINY_PLAYER
#define SHADERLAB_TRACK_ERROR(msg) "track"
#else
#define SHADERLAB_TRACK_ERROR(msg) msg
#endif

static std::string TransitionToString(const std::string& transitionPresetStem) {
    if (transitionPresetStem.empty()) {
        return "None";
    }
    return transitionPresetStem;
}

static const char* TextureTypeToString(TextureType type) {
    switch (type) {
        case TextureType::Texture2D: return "Texture2D";
        case TextureType::TextureCube: return "TextureCube";
        case TextureType::Texture3D: return "Texture3D";
        default: return "Unknown";
    }
}

static void DebugLogProjectSummary(const ProjectData& project) {
#if SHADERLAB_RUNTIME_DEBUG_LOG
    DebugLog("----- Project Summary -----");
    DebugLog("Scenes: " + std::to_string(project.scenes.size()));
    for (size_t i = 0; i < project.scenes.size(); ++i) {
        const auto& scene = project.scenes[i];
        DebugLog("  [" + std::to_string(i) + "] " + scene.name + " (" + TextureTypeToString(scene.outputType) + ")");
        DebugLog("    Bindings: " + std::to_string(scene.bindings.size()));
        DebugLog("    PostFX: " + std::to_string(scene.postFxChain.size()));
        for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
            const auto& fx = scene.postFxChain[fxIndex];
            std::string line = "      [" + std::to_string(fxIndex) + "] " + fx.name + (fx.enabled ? " (enabled)" : " (disabled)");
            if (!fx.precompiledPath.empty()) {
                line += " precompiled=" + fx.precompiledPath;
            }
            DebugLog(line);
        }
    }

    DebugLog("Track: bpm=" + std::to_string(project.track.bpm) + " len=" + std::to_string(project.track.lengthBeats) + " rows=" + std::to_string(project.track.rows.size()));
    for (const auto& row : project.track.rows) {
        DebugLog("  row=" + std::to_string(row.rowId)
            + " scene=" + std::to_string(row.sceneIndex)
            + " trans=" + TransitionToString(row.transitionPresetStem)
            + " dur=" + std::to_string(row.transitionDuration)
            + " offset=" + std::to_string(row.timeOffset)
            + " music=" + std::to_string(row.musicIndex)
            + " stop=" + std::string(row.stop ? "true" : "false"));
    }
    DebugLog("--------------------------");
#else
    (void)project;
#endif
}

static int TransitionSlotIndexFromStem(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionStem(transitionPresetStem);
    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        if (canonicalStem == kTransitionSlotStems[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

static const char* GetTransitionPackedPathForStem(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionStem(transitionPresetStem);
    if (canonicalStem == "crossfade" || canonicalStem == "fade_in" || canonicalStem == "fade_out") {
        return "assets/shaders/transition_fade_a_b.cso";
    }
    if (canonicalStem == "dip_to_black") {
        return "assets/shaders/transition_dip_to_black.cso";
    }
    if (canonicalStem == "glitch") {
        return "assets/shaders/transition_glitch.cso";
    }
    if (canonicalStem == "pixelate") {
        return "assets/shaders/transition_pixelate.cso";
    }
    return "";
}

struct TinyTrackMetadata {
    int sceneCount = 0;
    std::vector<int16_t> sceneModuleIndices;
    std::vector<std::vector<int16_t>> postFxModuleIndices;
    std::array<int16_t, kTransitionSlotCount> transitionModuleIndices = { -1, -1, -1, -1, -1, -1 };
    RenderAspectRatioPreset renderAspectRatioPreset = RenderAspectRatioPreset::Ratio_16_9;
    FullscreenRenderResolutionPreset fullscreenRenderResolutionPreset = FullscreenRenderResolutionPreset::Full;
};

#if SHADERLAB_TINY_PLAYER
static bool LoadCompactTrackBinaryFromBytesTiny(const std::vector<uint8_t>& bytes,
                                                DemoTrack& track,
                                                TinyTrackMetadata* outMeta,
                                                std::string& outError) {
    (void)outError;

    if (bytes.size() < 14) {
        return false;
    }

    const auto readU16 = [&bytes](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(bytes[offset]) |
               static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    };
    const auto readI16 = [&readU16](size_t offset) -> int16_t {
        return static_cast<int16_t>(readU16(offset));
    };
    const auto readI8 = [&bytes](size_t offset) -> int8_t {
        return static_cast<int8_t>(bytes[offset]);
    };

    if (readU16(0) != 0x4B54u || readU16(2) != 0x3352u) {
        return false;
    }

    const uint16_t bpmQ8 = readU16(4);
    const uint16_t lengthBeats = readU16(6);
    const uint16_t rowCount = readU16(8);
    const uint16_t sceneCount = readU16(10);

    TinyTrackMetadata decodedMeta;
    decodedMeta.sceneCount = static_cast<int>(sceneCount);
    decodedMeta.sceneModuleIndices.resize(sceneCount, -1);
    decodedMeta.postFxModuleIndices.resize(sceneCount);

    size_t offset = 14;
    if (offset + 12 > bytes.size()) {
        return false;
    }

    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        decodedMeta.transitionModuleIndices[i] = readI16(offset);
        offset += 2;
    }

    decodedMeta.fullscreenRenderResolutionPreset = FullscreenPresetFromCompactByte(bytes[13]);
    decodedMeta.renderAspectRatioPreset = AspectPresetFromCompactByte(bytes[13]);

    for (uint16_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        if (offset + 4 > bytes.size()) {
            return false;
        }

        decodedMeta.sceneModuleIndices[sceneIndex] = readI16(offset);
        offset += 2;
        const uint16_t fxCount = readU16(offset);
        offset += 2;

        auto& fxModules = decodedMeta.postFxModuleIndices[sceneIndex];
        fxModules.resize(fxCount, -1);
        if (offset + static_cast<size_t>(fxCount) * 2u > bytes.size()) {
            return false;
        }
        for (uint16_t fxIndex = 0; fxIndex < fxCount; ++fxIndex) {
            fxModules[fxIndex] = readI16(offset);
            offset += 2;
        }
    }

    if (offset + static_cast<size_t>(rowCount) * 9u > bytes.size()) {
        return false;
    }

    DemoTrack decoded;
    decoded.name = "CompactTrack";
    decoded.bpm = static_cast<float>(bpmQ8) / 256.0f;
    decoded.lengthBeats = static_cast<int>(lengthBeats);
    decoded.rows.reserve(rowCount);

    for (uint32_t i = 0; i < rowCount; ++i) {
        TrackerRow row;
        row.rowId = static_cast<int>(readI16(offset)); offset += 2;
        row.sceneIndex = static_cast<int>(readI16(offset)); offset += 2;
        const uint8_t transition = bytes[offset++];
        const uint8_t flags = bytes[offset++];
        const uint8_t transitionDurationQ4 = bytes[offset++];
        const int8_t timeOffsetQ4 = readI8(offset++);
        const int8_t musicIndex = readI8(offset++);

        row.transitionPresetStem.clear();
        if (transition < kTransitionSlotCount) {
            row.transitionPresetStem = kTransitionSlotStems[transition];
        }
        row.transitionDuration = static_cast<float>(transitionDurationQ4) / 16.0f;
        row.timeOffset = static_cast<float>(timeOffsetQ4) / 16.0f;
        row.musicIndex = static_cast<int>(musicIndex);
        row.oneShotIndex = -1;
        row.stop = (flags & 0x1u) != 0;
        row.isBeat = false;
        decoded.rows.push_back(row);
    }

    track = std::move(decoded);
    if (outMeta) {
        *outMeta = std::move(decodedMeta);
    }
    return true;
}
#endif

static bool LoadCompactTrackBinaryFromBytes(const std::vector<uint8_t>& bytes, DemoTrack& track, TinyTrackMetadata* outMeta, std::string& outError) {
#if SHADERLAB_TINY_PLAYER
    return LoadCompactTrackBinaryFromBytesTiny(bytes, track, outMeta, outError);
#else
    constexpr size_t kHeaderV2Size = 10;
    constexpr size_t kHeaderV3Size = 14;
    constexpr size_t kRowSize = 9;
    if (bytes.size() < kHeaderV2Size) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary too small."));
        return false;
    }

    const auto readU16 = [&bytes](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(bytes[offset]) |
               static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1]) << 8);
    };
    const auto readI16 = [&readU16](size_t offset) -> int16_t {
        return static_cast<int16_t>(readU16(offset));
    };
    const auto readI8 = [&bytes](size_t offset) -> int8_t {
        return static_cast<int8_t>(bytes[offset]);
    };

    const uint16_t magic0 = readU16(0);
    const uint16_t magic1 = readU16(2);
    if (magic0 != 0x4B54u || (magic1 != 0x3252u && magic1 != 0x3352u)) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary has invalid magic."));
        return false;
    }
    const bool isV3 = (magic1 == 0x3352u);

    const uint16_t bpmQ8 = readU16(4);
    const uint16_t lengthBeats = readU16(6);
    const uint16_t rowCount = readU16(8);

    TinyTrackMetadata decodedMeta;
    size_t offset = isV3 ? kHeaderV3Size : kHeaderV2Size;

    if (isV3) {
        if (bytes.size() < kHeaderV3Size) {
            SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary header truncated."));
            return false;
        }

        const uint16_t sceneCount = readU16(10);
        const uint8_t transitionSlotCount = bytes[12];
        const uint8_t compactRenderConfig = bytes[13];
        if (transitionSlotCount < 6) {
            SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary transition map is invalid."));
            return false;
        }

        decodedMeta.sceneCount = static_cast<int>(sceneCount);
        decodedMeta.sceneModuleIndices.resize(sceneCount, -1);
        decodedMeta.postFxModuleIndices.resize(sceneCount);
        decodedMeta.fullscreenRenderResolutionPreset = FullscreenPresetFromCompactByte(compactRenderConfig);
        decodedMeta.renderAspectRatioPreset = AspectPresetFromCompactByte(compactRenderConfig);

        for (int i = 0; i < 6; ++i) {
            if (offset + 2 > bytes.size()) {
                SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary transition map truncated."));
                return false;
            }
            decodedMeta.transitionModuleIndices[static_cast<size_t>(i)] = readI16(offset);
            offset += 2;
        }

        for (uint16_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
            if (offset + 4 > bytes.size()) {
                SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary scene map truncated."));
                return false;
            }

            const int16_t sceneModule = readI16(offset);
            offset += 2;
            const uint16_t fxCount = readU16(offset);
            offset += 2;

            decodedMeta.sceneModuleIndices[sceneIndex] = sceneModule;
            auto& fxModules = decodedMeta.postFxModuleIndices[sceneIndex];
            fxModules.resize(fxCount, -1);
            for (uint16_t fxIndex = 0; fxIndex < fxCount; ++fxIndex) {
                if (offset + 2 > bytes.size()) {
                    SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary post FX map truncated."));
                    return false;
                }
                fxModules[fxIndex] = readI16(offset);
                offset += 2;
            }
        }
    }

    const size_t expectedSize = offset + static_cast<size_t>(rowCount) * kRowSize;
    if (bytes.size() < expectedSize) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track binary truncated."));
        return false;
    }

    DemoTrack decoded;
    decoded.name = "CompactTrack";
    decoded.bpm = static_cast<float>(bpmQ8) / 256.0f;
    decoded.lengthBeats = static_cast<int>(lengthBeats);
    decoded.rows.reserve(rowCount);

    for (uint32_t i = 0; i < rowCount; ++i) {
        const int16_t rowId = readI16(offset); offset += 2;
        const int16_t sceneIndex = readI16(offset); offset += 2;
        const uint8_t transition = bytes[offset++];
        const uint8_t flags = bytes[offset++];
        const uint8_t transitionDurationQ4 = bytes[offset++];
        const int8_t timeOffsetQ4 = readI8(offset++);
        const int8_t musicIndex = readI8(offset++);

        TrackerRow row;
        row.rowId = static_cast<int>(rowId);
        row.sceneIndex = static_cast<int>(sceneIndex);
        row.transitionPresetStem.clear();
        if (transition < kTransitionSlotCount) {
            row.transitionPresetStem = kTransitionSlotStems[transition];
        }
        row.transitionDuration = static_cast<float>(transitionDurationQ4) / 16.0f;
        row.timeOffset = static_cast<float>(timeOffsetQ4) / 16.0f;
        row.musicIndex = static_cast<int>(musicIndex);
        row.oneShotIndex = -1;
        row.stop = (flags & 0x1u) != 0;
        row.isBeat = false;
        decoded.rows.push_back(row);
    }

    track = std::move(decoded);
    if (outMeta) {
        *outMeta = std::move(decodedMeta);
    }
    return true;
#endif
}

static bool LoadCompactTrackBinaryFromFile(const std::string& path, DemoTrack& track, TinyTrackMetadata* outMeta, std::string& outError) {
#if SHADERLAB_TINY_PLAYER
    std::vector<uint8_t> bytes;
    if (!ReadFileBytesC(path, bytes)) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Failed to open compact track binary file."));
        return false;
    }
#else
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Failed to open compact track binary file."));
        return false;
    }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
#endif
    return LoadCompactTrackBinaryFromBytes(bytes, track, outMeta, outError);
}

static bool LoadCompactTrackBinaryFromPathCandidates(const std::string& relativeOrAbsolutePath,
                                                     const std::string& manifestPath,
                                                     DemoTrack& track,
                                                     TinyTrackMetadata* outMeta,
                                                     std::string& outResolvedPath,
                                                     std::string& outError) {
#if SHADERLAB_TINY_PLAYER
    (void)relativeOrAbsolutePath;
    (void)manifestPath;
    (void)track;
    (void)outMeta;
    outResolvedPath.clear();
    outError.clear();
    return false;
#else
    outResolvedPath.clear();
    outError.clear();

    if (relativeOrAbsolutePath.empty()) {
        SetCompactTrackDecodeError(outError, SHADERLAB_TRACK_ERROR("Compact track path is empty."));
        return false;
    }

    std::string localError;
    if (LoadCompactTrackBinaryFromFile(relativeOrAbsolutePath, track, outMeta, localError)) {
        outResolvedPath = relativeOrAbsolutePath;
        return true;
    }
    if (!localError.empty()) {
        outError = localError;
    }

    const std::string manifestDir = GetDirectoryName(manifestPath);
    if (!manifestDir.empty()) {
        const std::string manifestCandidate = JoinPath(manifestDir, relativeOrAbsolutePath);
        localError.clear();
        if (LoadCompactTrackBinaryFromFile(manifestCandidate, track, outMeta, localError)) {
            outResolvedPath = manifestCandidate;
            return true;
        }
        if (!localError.empty()) {
            outError = localError;
        }
    }

    const std::string exeDir = GetExecutableDirectory();
    if (!exeDir.empty()) {
        const std::string exeCandidate = JoinPath(exeDir, relativeOrAbsolutePath);
        localError.clear();
        if (LoadCompactTrackBinaryFromFile(exeCandidate, track, outMeta, localError)) {
            outResolvedPath = exeCandidate;
            return true;
        }
        if (!localError.empty()) {
            outError = localError;
        }
    }

    return false;
#endif
}

#if !SHADERLAB_TINY_PLAYER
static bool IsTrackBinManifestPath(const std::string& manifestPath) {
    if (manifestPath.empty()) {
        return false;
    }

    std::string lowerManifest = manifestPath;
    for (char& c : lowerManifest) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    return lowerManifest.size() >= 10 &&
           lowerManifest.rfind("track.bin") == lowerManifest.size() - 9;
}

static bool LoadCompactTrackFromManifestOrFallback(const std::string& manifestPath,
                                                   DemoTrack& outTrack,
                                                   TinyTrackMetadata* outMeta,
                                                   std::string& outResolvedTrackPath,
                                                   std::string& outError) {
    outResolvedTrackPath.clear();
    outError.clear();

    bool trackLoaded = LoadCompactTrackBinaryFromPathCandidates(
        "assets/track.bin", manifestPath, outTrack, outMeta, outResolvedTrackPath, outError);

    if (!trackLoaded && IsTrackBinManifestPath(manifestPath)) {
        std::string localError;
        if (LoadCompactTrackBinaryFromFile(manifestPath, outTrack, outMeta, localError)) {
            trackLoaded = true;
            outResolvedTrackPath = manifestPath;
        } else if (!localError.empty()) {
            outError = localError;
        }
    }

    return trackLoaded;
}
#endif

static bool TryLoadMicroUbershaderBytecodeBlob(bool packedAssets, std::vector<uint8_t>& outBlob) {
    outBlob.clear();

    if (packedAssets && PackageManager::Get().HasFile(kPackedMicroUbershaderBytecodePath)) {
        outBlob = PackageManager::Get().GetFile(kPackedMicroUbershaderBytecodePath);
        return !outBlob.empty();
    }

#if SHADERLAB_TINY_PLAYER
    return false;
#else
    const char* candidates[] = {
        "assets/shaders/ubershader.bin",
        "shaders/ubershader.bin"
    };
    for (const char* candidate : candidates) {
        std::ifstream file(candidate, std::ios::binary);
        if (!file.is_open()) continue;
        outBlob.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (!outBlob.empty()) {
            return true;
        }
    }
    return false;
#endif
}

static bool ParseMicroUbershaderBytecodeBlob(const std::vector<uint8_t>& blob,
                                             std::vector<std::vector<uint8_t>>& outModules) {
    outModules.clear();
    if (blob.size() < 8) {
        return false;
    }

    const auto readU16 = [&blob](size_t offset) -> uint16_t {
        return static_cast<uint16_t>(blob[offset]) |
               static_cast<uint16_t>(static_cast<uint16_t>(blob[offset + 1]) << 8);
    };
    const auto readU32 = [&blob](size_t offset) -> uint32_t {
        return static_cast<uint32_t>(blob[offset]) |
               (static_cast<uint32_t>(blob[offset + 1]) << 8) |
               (static_cast<uint32_t>(blob[offset + 2]) << 16) |
               (static_cast<uint32_t>(blob[offset + 3]) << 24);
    };

    const uint32_t magic = readU32(0);
    const uint16_t version = readU16(4);
    const uint16_t moduleCount = readU16(6);
    if (magic != 0x30425553u || version != 1) {
        return false;
    }

    const size_t tableStart = 8;
    const size_t tableSize = static_cast<size_t>(moduleCount) * 8u;
    if (blob.size() < tableStart + tableSize) {
        return false;
    }

    outModules.resize(moduleCount);
    for (uint16_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
        const size_t row = tableStart + static_cast<size_t>(moduleIndex) * 8u;
        const uint32_t offset = readU32(row);
        const uint32_t size = readU32(row + 4u);
#if SHADERLAB_TINY_PLAYER
        auto& module = outModules[moduleIndex];
        module.assign(blob.begin() + static_cast<size_t>(offset),
                      blob.begin() + static_cast<size_t>(offset) + static_cast<size_t>(size));
#else
        if (size == 0) {
            continue;
        }
        if (offset > blob.size() || size > blob.size() || static_cast<size_t>(offset) + static_cast<size_t>(size) > blob.size()) {
            return false;
        }

        auto& module = outModules[moduleIndex];
        module.assign(blob.begin() + static_cast<size_t>(offset),
                      blob.begin() + static_cast<size_t>(offset) + static_cast<size_t>(size));
#endif
    }

    return true;
}

static bool BuildTinyProjectFromAssets(ProjectData& project,
                                       const TinyTrackMetadata& trackMeta,
                                       std::vector<int16_t>& outSceneModuleIds,
                                       std::vector<std::vector<int16_t>>& outPostFxModuleIds,
                                       std::array<int16_t, kTransitionSlotCount>& outTransitionModuleIds) {
    project = {};
    project.transport.bpm = 120.0f;
    project.renderAspectRatioPreset = trackMeta.renderAspectRatioPreset;
    project.fullscreenRenderResolutionPreset = trackMeta.fullscreenRenderResolutionPreset;
    outSceneModuleIds.clear();
    outPostFxModuleIds.clear();
    outTransitionModuleIds.fill(-1);

    if (trackMeta.sceneCount <= 0 ||
        trackMeta.sceneModuleIndices.size() < static_cast<size_t>(trackMeta.sceneCount) ||
        trackMeta.postFxModuleIndices.size() < static_cast<size_t>(trackMeta.sceneCount)) {
        return false;
    }

    outSceneModuleIds.resize(static_cast<size_t>(trackMeta.sceneCount), -1);
    outPostFxModuleIds.resize(static_cast<size_t>(trackMeta.sceneCount));

    for (int sceneIndex = 0; sceneIndex < trackMeta.sceneCount; ++sceneIndex) {
        Scene scene;
        scene.name = "S" + std::to_string(sceneIndex);
        scene.shaderCode.clear();
        scene.precompiledPath.clear();

        const int16_t sceneModule = trackMeta.sceneModuleIndices[static_cast<size_t>(sceneIndex)];
        outSceneModuleIds[static_cast<size_t>(sceneIndex)] = sceneModule;

        const auto& fxModules = trackMeta.postFxModuleIndices[static_cast<size_t>(sceneIndex)];
        auto& fxModuleIds = outPostFxModuleIds[static_cast<size_t>(sceneIndex)];
        fxModuleIds.clear();

        for (size_t fxIndex = 0; fxIndex < fxModules.size(); ++fxIndex) {
            const int16_t fxModule = fxModules[fxIndex];
            if (fxModule < 0) continue;

            fxModuleIds.push_back(fxModule);
            Scene::PostFXEffect fx;
            fx.enabled = true;
            fx.name = "fx" + std::to_string(fxIndex);
            fx.shaderCode.clear();
            fx.precompiledPath.clear();
            scene.postFxChain.push_back(std::move(fx));
        }

        project.scenes.push_back(std::move(scene));
    }

    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        outTransitionModuleIds[i] = trackMeta.transitionModuleIndices[i];
    }

    project.track.name = "CompactTrack";
    project.track.bpm = 120.0f;
    project.track.lengthBeats = 1;
    project.track.rows.clear();
    return true;
}

static const TrackerRow* FindNextSceneRow(const DemoTrack& track, int afterBeat) {
    const TrackerRow* bestRow = nullptr;
    int bestBeat = INT_MAX;
    for (const auto& row : track.rows) {
        if (row.sceneIndex >= 0 && row.rowId > afterBeat && row.rowId < bestBeat) {
            bestBeat = row.rowId;
            bestRow = &row;
        }
    }
    return bestRow;
}

static int FindNextSceneIndex(const DemoTrack& track, int afterBeat) {
    const TrackerRow* row = FindNextSceneRow(track, afterBeat);
    return row ? row->sceneIndex : -1;
}

static void ComputeShaderMusicalTiming(const Transport& transport,
                                       float& outIBeat,
                                       float& outIBar,
                                       float& outFBeat,
                                       float& outFBarBeat,
                                       float& outFBarBeat16) {
    constexpr float kBeatsPerBar = 4.0f;
    constexpr float kSixteenthPerBeat = 4.0f;
    const float beatsPerSecond = transport.bpm / 60.0f;
    float exactBeat = 0.0f;
    if (beatsPerSecond > 0.0f) {
        exactBeat = static_cast<float>(transport.timeSeconds * static_cast<double>(beatsPerSecond));
        if (exactBeat < 0.0f) {
            exactBeat = 0.0f;
        }
    }
    const float beat = std::floor(exactBeat);
    const float bar = std::floor(beat / kBeatsPerBar);
    const float beatInBar = exactBeat - std::floor(exactBeat / kBeatsPerBar) * kBeatsPerBar;
    float barBeat16 = std::floor(beatInBar * kSixteenthPerBeat);
    if (barBeat16 < 0.0f) {
        barBeat16 = 0.0f;
    }
    if (barBeat16 > 15.0f) {
        barBeat16 = 15.0f;
    }

    outIBeat = beat;
    outIBar = bar;
    outFBeat = exactBeat;
    outFBarBeat = beatInBar;
    outFBarBeat16 = barBeat16;
}

static double BeatSeconds(float bpm) {
    if (bpm <= 0.0f) return 0.0;
    return 60.0 / static_cast<double>(bpm);
}

static double SceneTimeSeconds(double exactBeat, double startBeat, float offsetBeats, float bpm) {
    const double beatSeconds = BeatSeconds(bpm);
    if (beatSeconds <= 0.0) return 0.0;
    const double sceneBeats = exactBeat - startBeat + static_cast<double>(offsetBeats);
    return sceneBeats * beatSeconds;
}

static std::string GetTransitionShaderSourceForStem(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionStem(transitionPresetStem);
    std::string common = R"(
float4 main(float2 fragCoord, float2 iResolution, float iTime) {
int2 dims = max(int2(iResolution) - int2(1, 1), int2(0, 0));
int2 pixel = clamp(int2(fragCoord), int2(0, 0), dims);
float t = saturate(iTime);
float4 colA = iChannel0.Load(int3(pixel, 0));
float4 colB = iChannel1.Load(int3(pixel, 0)); 
)";
    if (canonicalStem == "crossfade" || canonicalStem == "fade_in" || canonicalStem == "fade_out") {
        return common + R"(
return lerp(colA, colB, t);
}
)";
    }
    if (canonicalStem == "dip_to_black") {
        return common + R"(
return (t < 0.5) ? lerp(colA, float4(0,0,0,1), t*2.0) : lerp(float4(0,0,0,1), colB, (t-0.5)*2.0);
}
)";
    }
    if (canonicalStem == "glitch") {
        return common + R"(
    float2 uv = (float2(pixel) + 0.5) / iResolution;
float offset = iTime * 10.0;
float noise = frac(sin(dot(float2(floor(uv.y * 20.0) + offset, offset), float2(12.9898, 78.233))) * 43758.5453);
float disp = (noise - 0.5) * 0.1 * sin(t * 3.14159);
float2 uv2 = uv + float2(disp, 0);
colA = iChannel0.Sample(iSampler0, uv2);
colB = iChannel1.Sample(iSampler1, uv2);
return lerp(colA, colB, t);
}
)";
    }
    if (canonicalStem == "pixelate") {
        return common + R"(
float2 uv = (float2(pixel) + 0.5) / iResolution;
float p = sin(t * 3.14159);
float n = 50.0 * (1.0 - p) + 1.0; 
float2 uvP = floor(uv * n) / n;
colA = iChannel0.Sample(iSampler0, uvP);
colB = iChannel1.Sample(iSampler1, uvP);
return lerp(colA, colB, t);
}
)";
    }
    return common + " return colB; }";
}

DemoPlayer::DemoPlayer() {}
DemoPlayer::~DemoPlayer() { Shutdown(); }

void DemoPlayer::Shutdown() {
    // Shutdown ImGui
#if SHADERLAB_RUNTIME_IMGUI
    if (ImGui::GetCurrentContext()) {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
    }
#endif

#if !SHADERLAB_TINY_PLAYER
    if (m_audio) { m_audio->Shutdown(); delete m_audio; m_audio = nullptr; }
#else
    m_audio = nullptr;
#endif
    if (m_renderer) { m_renderer->Shutdown(); delete m_renderer; m_renderer = nullptr; }
#if !SHADERLAB_TINY_PLAYER
    if (m_compiler) { m_compiler->Shutdown(); delete m_compiler; m_compiler = nullptr; }
#else
    m_compiler = nullptr;
    m_compilerReady = false;
#endif
    // Device/Swapchain/Renderer are owned externally (except Renderer/Compiler now)
}

bool DemoPlayer::Initialize(HWND hwnd, Device* device, Swapchain* swapchain, int width, int height) {
    m_device = device;
    m_swapchain = swapchain;

    PackageManager::Get().Initialize();
    if (PackageManager::Get().HasFile(kPackedVertexShaderPath)) {
        m_precompiledVertexShader = PackageManager::Get().GetFile(kPackedVertexShaderPath);
    } else {
        if (!PackageManager::Get().IsPacked()) {
            std::string resolvedPath;
            LoadBytecodeFromPathCandidates(kPackedVertexShaderPath, m_manifestPath, m_precompiledVertexShader, &resolvedPath);
        }
    }
    
#if !SHADERLAB_TINY_PLAYER
    m_compiler = nullptr;
    m_compilerReady = false;
#else
    #if SHADERLAB_TINY_RUNTIME_COMPILE
    m_compiler = new ShaderCompiler();
    m_compilerReady = m_compiler->Initialize();
    TinyTrace(m_compilerReady ? "Tiny compiler init OK" : "Tiny compiler init FAILED");
    #else
    m_compiler = nullptr;
    m_compilerReady = false;
    TinyTrace("Tiny compiler disabled by build flag");
    #endif
#endif

    // Init Renderer
    m_renderer = new PreviewRenderer();
    ShaderCompiler* compiler = nullptr;
    const std::vector<uint8_t>* precompiledVs = m_precompiledVertexShader.empty() ? nullptr : &m_precompiledVertexShader;
    m_rendererReady = m_renderer->Initialize(m_device, compiler, DXGI_FORMAT_R8G8B8A8_UNORM, precompiledVs);
    if (!m_rendererReady) {
        // Only fatal if initialization logic in Renderer is strict.
        // We modified Renderer to handle null m_compiler if needed, or if generic init fails it returns false.
        // PreviewRenderer::Initialize fails if compiler is invalid. 
        // We need to fix PreviewRenderer::Initialize to accept null compiler if we want this to work without DLL.
        // For now, let's assume if it fails, we log it.
    #if !SHADERLAB_TINY_PLAYER
        RuntimeErr("E201", "renderer init failed");
    #endif
    }
    
    // Create local Audio System
#if !SHADERLAB_TINY_PLAYER
    m_audio = new AudioSystem();
    if (!m_audio->Initialize()) {
        RuntimeErr("E202", "audio init failed");
    }
#else
    m_audio = nullptr;
#endif

    m_width = width;
    m_height = height;

    // Dummy Texture (Black)
    {
        D3D12_HEAP_PROPERTIES heapProps = {D3D12_HEAP_TYPE_DEFAULT};
        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = 1; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE clearValue = {};
        clearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        clearValue.Color[0] = 0.0f;
        clearValue.Color[1] = 0.0f;
        clearValue.Color[2] = 0.0f;
        clearValue.Color[3] = 1.0f;
        
        m_device->GetDevice()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, 
            D3D12_RESOURCE_STATE_RENDER_TARGET, &clearValue, 
            IID_PPV_ARGS(&m_dummyTexture));

        m_dummyTextureInitialized = false;

        // Creating SRV Heap
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
        heapDesc.NumDescriptors = 1; 
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_dummySrvHeap));

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        m_device->GetDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_dummyRtvHeap));

        if (m_dummyRtvHeap) {
            m_device->GetDevice()->CreateRenderTargetView(m_dummyTexture.Get(), nullptr, m_dummyRtvHeap->GetCPUDescriptorHandleForHeapStart());
        }
        
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        
        m_device->GetDevice()->CreateShaderResourceView(m_dummyTexture.Get(), &srvDesc, m_dummySrvHeap->GetCPUDescriptorHandleForHeapStart());
    }

    // Initialize ImGui
#if SHADERLAB_RUNTIME_IMGUI
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        
        ImGui::StyleColorsDark();
        
        // Build font atlas manually to ensure ImGui doesn't assert before backend is ready
        unsigned char* pixels;
        int atlasWidth, atlasHeight;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &atlasWidth, &atlasHeight);

        // Descriptor Heap for ImGui
        D3D12_DESCRIPTOR_HEAP_DESC desc = {};
        desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        desc.NumDescriptors = 1;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        m_device->GetDevice()->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&m_imguiSrvHeap));

        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX12_Init(m_device->GetDevice(), 2,
            DXGI_FORMAT_R8G8B8A8_UNORM, m_imguiSrvHeap.Get(),
            m_imguiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
            m_imguiSrvHeap->GetGPUDescriptorHandleForHeapStart());
    }
    #endif

    return true;
}

void DemoPlayer::LoadProject(const std::string& manifestPath) {
    m_manifestPath = manifestPath;
    m_loadingFailed = false;

    if (!m_rendererReady && m_renderer) {
        if (m_precompiledVertexShader.empty() && !PackageManager::Get().IsPacked()) {
            std::string resolvedPath;
            LoadBytecodeFromPathCandidates(kPackedVertexShaderPath, m_manifestPath, m_precompiledVertexShader, &resolvedPath);
        }
        ShaderCompiler* compiler = nullptr;
        const std::vector<uint8_t>* precompiledVs = m_precompiledVertexShader.empty() ? nullptr : &m_precompiledVertexShader;
        m_rendererReady = m_renderer->Initialize(m_device, compiler, DXGI_FORMAT_R8G8B8A8_UNORM, precompiledVs);
#if !SHADERLAB_TINY_PLAYER
        if (!m_rendererReady) {
            RuntimeErr("E203", "renderer reinit failed after manifest load");
        }
#endif
    }

    m_loadingStage = LoadingStage::LoadingManifest;
    m_loadingStatus = "Loading manifest";
    TinyTrace("LoadProject: " + manifestPath);
}

bool DemoPlayer::ConsumePendingRenderResolution(uint32_t& outWidth, uint32_t& outHeight) {
    if (!m_pendingRenderResolutionUpdate || m_pendingRenderWidth == 0u || m_pendingRenderHeight == 0u) {
        return false;
    }

    outWidth = m_pendingRenderWidth;
    outHeight = m_pendingRenderHeight;
    m_pendingRenderResolutionUpdate = false;
    m_pendingRenderWidth = 0u;
    m_pendingRenderHeight = 0u;
    return true;
}

bool DemoPlayer::EnsureTransitionPipeline(const std::string& transitionPresetStem) {
    if (!m_renderer || !m_rendererReady) {
        return false;
    }

    const std::string canonicalStem = CanonicalTransitionStem(transitionPresetStem);
    if (canonicalStem.empty()) {
        return false;
    }

    auto psoIt = m_transitionPsoCache.find(canonicalStem);
    if (psoIt != m_transitionPsoCache.end() && psoIt->second) {
        m_transitionPSO = psoIt->second;
        m_compiledTransitionStem = canonicalStem;
        return true;
    }

    auto loadTransitionBytecode = [&](const std::string& stem) -> const std::vector<uint8_t>* {
        auto bytecodeIt = m_transitionBytecode.find(stem);
        if (bytecodeIt != m_transitionBytecode.end() && !bytecodeIt->second.empty()) {
            return &bytecodeIt->second;
        }

        const char* packedPath = GetTransitionPackedPathForStem(stem);
        if (packedPath && *packedPath) {
            if (PackageManager::Get().IsPacked()) {
                if (PackageManager::Get().HasFile(packedPath)) {
                    auto& cache = m_transitionBytecode[stem];
                    cache = PackageManager::Get().GetFile(packedPath);
                    if (!cache.empty()) {
                        return &cache;
                    }
                }
            } else {
#if !SHADERLAB_TINY_PLAYER
                fs::path diskPath = fs::path(m_manifestPath).parent_path() / packedPath;
                std::ifstream file(diskPath, std::ios::binary);
                if (file.is_open()) {
                    auto& cache = m_transitionBytecode[stem];
                    cache.assign(
                        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
                    if (!cache.empty()) {
                        return &cache;
                    }
                }
#endif
            }
        }
        return nullptr;
    };

    const std::vector<uint8_t>* bytecode = loadTransitionBytecode(canonicalStem);
#if SHADERLAB_TINY_PLAYER
    const int idx = TransitionSlotIndexFromStem(canonicalStem);
    if ((!bytecode || bytecode->empty()) && idx >= 0 && idx < static_cast<int>(m_microTransitionModuleIds.size())) {
        const int16_t moduleId = m_microTransitionModuleIds[static_cast<size_t>(idx)];
        if (moduleId >= 0 && moduleId < static_cast<int>(m_microModuleBytecode.size())) {
            const auto& moduleBytecode = m_microModuleBytecode[static_cast<size_t>(moduleId)];
            if (!moduleBytecode.empty()) {
                bytecode = &moduleBytecode;
            }
        }
    }
#endif

    ComPtr<ID3D12PipelineState> pipeline;
    if (bytecode && !bytecode->empty()) {
        pipeline = m_renderer->CreatePSOFromBytecode(*bytecode);
    }

#if !SHADERLAB_TINY_PLAYER
    if (!pipeline) {
        std::string source = GetTransitionShaderSourceForStem(canonicalStem);
        if (!source.empty()) {
            std::vector<std::string> errorOut;
            std::vector<PreviewRenderer::TextureDecl> textures;
            textures.push_back({0, "Texture2D"});
            textures.push_back({1, "Texture2D"});
            pipeline = m_renderer->CompileShader(source, textures, errorOut);
            if (!pipeline) {
                std::string errStr;
                for (const auto& e : errorOut) errStr += e + "\n";
                SHADERLAB_RT_DEBUG_LOG_ERROR("Failed to runtime-compile transition shader '" + canonicalStem + "': " + errStr);
            }
        }
    }
#endif

    if (!pipeline) {
        return false;
    }

    m_transitionPsoCache[canonicalStem] = pipeline;
    m_transitionPSO = pipeline;
    m_compiledTransitionStem = canonicalStem;
    return true;
}

void DemoPlayer::PrimeRuntimeResources() {
    for (int sceneIndex = 0; sceneIndex < static_cast<int>(m_project.scenes.size()); ++sceneIndex) {
        EnsureSceneTexture(sceneIndex);
        auto& scene = m_project.scenes[static_cast<size_t>(sceneIndex)];
        if (!scene.postFxChain.empty()) {
            EnsurePostFxResources(scene);
        }
    }

    bool usedTransitions[kTransitionSlotCount] = {};
    for (const auto& row : m_project.track.rows) {
        if (row.transitionPresetStem.empty()) {
            continue;
        }
        const int transitionIndex = TransitionSlotIndexFromStem(row.transitionPresetStem);
        if (transitionIndex >= 0 && transitionIndex < static_cast<int>(kTransitionSlotCount)) {
            usedTransitions[transitionIndex] = true;
        }
    }

    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        if (!usedTransitions[i]) {
            continue;
        }
        EnsureTransitionPipeline(kTransitionSlotStems[i]);
    }
}

// Minimal helpers
static ComPtr<ID3D12Resource> LoadTexture(Device* dev, const std::string& path) {
    (void)dev;
    (void)path;
    // ... Implementation omitted for brevity
    return nullptr;
}


void DemoPlayer::Update(double wallTime, float dt) {
    if (m_loadingFailed) {
        return;
    }

#if !SHADERLAB_TINY_PLAYER
    auto loadAudioClip = [&](const auto& clip, bool packedAssets) {
        if (!m_audio) {
            return false;
        }

        std::vector<std::string> packageCandidates;
        packageCandidates.push_back(clip.path);

        std::string normalizedPath = clip.path;
        std::replace(normalizedPath.begin(), normalizedPath.end(), '\\', '/');
        if (normalizedPath != clip.path) {
            packageCandidates.push_back(normalizedPath);
        }

        std::filesystem::path clipPathFs(clip.path);
        const std::string clipFileName = clipPathFs.filename().string();
        if (!clipFileName.empty()) {
            packageCandidates.push_back("assets/audio/" + clipFileName);
            packageCandidates.push_back("audio/" + clipFileName);
        }

        if (packedAssets) {
            for (const auto& candidate : packageCandidates) {
                if (!PackageManager::Get().HasFile(candidate)) {
                    continue;
                }

                auto audioData = PackageManager::Get().GetFile(candidate);
                if (!audioData.empty() && m_audio->LoadAudioFromMemory(audioData.data(), audioData.size())) {
                    return true;
                }
            }
        }

        std::vector<std::string> diskCandidates;
        diskCandidates.push_back(clip.path);

        if (!normalizedPath.empty() && normalizedPath != clip.path) {
            diskCandidates.push_back(normalizedPath);
        }

        if (!m_manifestPath.empty()) {
            const std::filesystem::path manifestDir = std::filesystem::path(m_manifestPath).parent_path();
            if (!manifestDir.empty()) {
                diskCandidates.push_back((manifestDir / clip.path).string());
                if (!clipFileName.empty()) {
                    diskCandidates.push_back((manifestDir / "assets" / "audio" / clipFileName).string());
                    diskCandidates.push_back((manifestDir / "audio" / clipFileName).string());
                }
            }
        }

        for (const auto& candidate : diskCandidates) {
            if (candidate.empty()) {
                continue;
            }
            if (m_audio->LoadAudio(candidate)) {
                return true;
            }
        }

        return false;
    };
#endif

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
    if (m_loadingStage != m_debugLastLoadingStage) {
        SHADERLAB_RT_DEBUG_LOG(
            "Loading stage -> " + std::to_string(static_cast<int>(m_loadingStage))
            + " | status=" + m_loadingStatus);
        m_debugLastLoadingStage = m_loadingStage;
    }
#endif

    if (m_loadingStage != LoadingStage::Ready) {
        if (m_loadingStage == LoadingStage::LoadingManifest) {
            // 1. Initialize Package System
            bool packed = PackageManager::Get().IsPacked();
            if (!packed) {
                packed = PackageManager::Get().Initialize();
            }
            m_loadingStatus = packed ? "Loading packed project assets" : "Loading project assets";

            bool loaded = false;
#if SHADERLAB_TINY_PLAYER
            if (!packed) {
                packed = PackageManager::Get().Initialize();
            }

            if (packed) {
                const auto tinyUbershaderBlob = PackageManager::Get().GetFile(kPackedMicroUbershaderBytecodePath);
                std::vector<std::vector<uint8_t>> tinyModuleBytecode;
                const bool hasTinyUbershaderBlob = ParseMicroUbershaderBytecodeBlob(tinyUbershaderBlob, tinyModuleBytecode);

                DemoTrack decodedTrack;
                TinyTrackMetadata decodedMeta;
                std::string trackError;
                const auto trackData = PackageManager::Get().GetFile("assets/track.bin");
                const bool trackLoaded = LoadCompactTrackBinaryFromBytes(trackData, decodedTrack, &decodedMeta, trackError);

                const bool tinyProjectReady = hasTinyUbershaderBlob && trackLoaded && BuildTinyProjectFromAssets(
                    m_project,
                    decodedMeta,
                    m_microSceneModuleIds,
                    m_microPostFxModuleIds,
                    m_microTransitionModuleIds);

                loaded = tinyProjectReady;
                if (loaded) {
                    m_project.track = std::move(decodedTrack);
                    m_project.transport.bpm = m_project.track.bpm;
                    m_microModuleBytecode = std::move(tinyModuleBytecode);
                    m_loadingStatus = "Manifest loaded";
                } else {
                    m_microSceneModuleIds.clear();
                    m_microPostFxModuleIds.clear();
                    m_microTransitionModuleIds.fill(-1);
                    m_microModuleBytecode.clear();
                    m_loadingStatus = "Tiny load failed";
                }
            } else {
                m_loadingStatus = "Tiny load failed";
            }
#else
            if (packed) {
                std::cout << "Packed build detected. Loading project.json from executable." << std::endl;
                if (PackageManager::Get().HasFile("project.json")) {
                    auto data = PackageManager::Get().GetFile("project.json");
                    std::string jsonStr(data.begin(), data.end());
                    loaded = Serializer::LoadProjectFromJson(jsonStr, m_project);

                    if (loaded && PackageManager::Get().HasFile("assets/track.bin")) {
                        auto trackData = PackageManager::Get().GetFile("assets/track.bin");
                        std::string trackError;
                        if (!LoadCompactTrackBinaryFromBytes(trackData, m_project.track, nullptr, trackError)) {
#if SHADERLAB_COMPACT_TRACK_DEBUG
                            SHADERLAB_RT_DEBUG_LOG_ERROR("Failed to load compact track binary: " + trackError);
#endif
                        }
#if SHADERLAB_COMPACT_TRACK_DEBUG
                        else {
                            SHADERLAB_RT_DEBUG_LOG("Loaded compact track binary from packed executable.");
                        }
#endif
                    }
                } else {
                    RuntimeErr("E206", "packed build missing project.json");
                }
            } else {
                std::cout << "No pack detected. Loading project from disk: " << m_manifestPath << std::endl;
                loaded = Serializer::LoadProject(m_manifestPath, m_project);

                if (loaded) {
                    DemoTrack decodedTrack;
                    std::string trackError;

                    std::string resolvedTrackPath;
                    const bool trackLoaded = LoadCompactTrackFromManifestOrFallback(
                        m_manifestPath, decodedTrack, nullptr, resolvedTrackPath, trackError);

                    if (trackLoaded) {
                        m_project.track = std::move(decodedTrack);
                        std::cout << "Loaded compact track binary from disk: " << resolvedTrackPath << std::endl;
#if SHADERLAB_COMPACT_TRACK_DEBUG
                        SHADERLAB_RT_DEBUG_LOG("Loaded compact track binary from disk.");
#endif
                    } else {
                        std::cout << "Compact track binary not loaded from disk (manifest=" << m_manifestPath
                                  << ", reason=" << (trackError.empty() ? "not found" : trackError) << ")"
                                  << std::endl;
                    }
                }
            }
#endif

            if (loaded) {
                if (m_project.track.bpm > 0.0f) {
                    m_project.transport.bpm = m_project.track.bpm;
                } else if (m_project.transport.bpm <= 0.0f) {
                    m_project.transport.bpm = 120.0f;
                }

                uint32_t currentWidth = 1u;
                if (m_width > 0) {
                    currentWidth = static_cast<uint32_t>(m_width);
                }

                uint32_t currentHeight = 1u;
                if (m_height > 0) {
                    currentHeight = static_cast<uint32_t>(m_height);
                }
                uint32_t preferredWidth = 0u;
                uint32_t preferredHeight = 0u;
                if (ComputeFullscreenRenderResolution(
                        m_project,
                        currentWidth,
                        currentHeight,
                        preferredWidth,
                        preferredHeight)) {
                    if (preferredWidth != currentWidth || preferredHeight != currentHeight) {
                        m_pendingRenderResolutionUpdate = true;
                        m_pendingRenderWidth = preferredWidth;
                        m_pendingRenderHeight = preferredHeight;
                    }
                }

                DebugLogProjectSummary(m_project);

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
                if (packed) {
                    std::vector<std::string> missingShaderPaths;
                    if (!ValidatePackedShaderAssets(m_project, missingShaderPaths)) {
                        m_loadingFailed = true;
                        m_loadingStatus = "Packed shader validation failed (missing embedded assets):\n";
                        const size_t maxShown = (std::min)(missingShaderPaths.size(), static_cast<size_t>(16));
                        for (size_t i = 0; i < maxShown; ++i) {
                            m_loadingStatus += " - " + missingShaderPaths[i] + "\n";
                        }
                        if (missingShaderPaths.size() > maxShown) {
                            m_loadingStatus += " - ... and " + std::to_string(missingShaderPaths.size() - maxShown) + " more\n";
                        }
                        RuntimeErr("E204", "packed shader validation failed");
                        return;
                    }
                }
#endif

                m_loadingStage = LoadingStage::LoadingAssets;
                m_loadingStatus = "Loading runtime assets";
                TinyTrace("Project load OK");
            } else {
                m_loadingFailed = true;
                if (m_loadingStatus.empty()) {
                    m_loadingStatus = "Project load failed";
                }
                TinyTrace("Project load FAILED");
                {
                    RuntimeErr("E205", "project load failed");
                }
                // Nothing to load
            }
            return;
        }

        if (m_loadingStage == LoadingStage::LoadingAssets) {
            m_loadingStatus = "Preparing runtime assets";
    #if !SHADERLAB_TINY_PLAYER
            bool packed = PackageManager::Get().IsPacked();
            for(auto& clip : m_project.audioLibrary) {
                loadAudioClip(clip, packed);
            }
    #endif
            m_compilationIndex = 0;

            m_loadingStage = LoadingStage::CompilingShaders;
            m_loadingStatus = "Compiling shaders";
            return;
        }

        if (m_loadingStage == LoadingStage::CompilingShaders) {
            if (m_compilationIndex < (int)m_project.scenes.size()) {
                m_loadingStatus = "Compiling scene " + std::to_string(m_compilationIndex + 1) + "/" + std::to_string(m_project.scenes.size());
                const bool ok = CompileScene(m_compilationIndex);
                if (!ok) {
                    TinyTrace("CompileScene failed at index " + std::to_string(m_compilationIndex));
#if !SHADERLAB_TINY_PLAYER
                    RuntimeErr("E200", "scene compile failed");
#endif
                    m_loadingStatus = "Compile failed at scene " + std::to_string(m_compilationIndex);
                }
                m_compilationIndex++;
            } else {
                PrimeRuntimeResources();
                m_transport = m_project.transport;
                m_transport.state = TransportState::Playing;
                m_transport.timeSeconds = 0.0;
                m_project.track.currentBeat = 0;
                m_project.track.lastTriggeredBeat = -1;
                m_lastFrameTime = wallTime;
                m_loadingStage = LoadingStage::Ready;
                m_loadingStatus = "Ready";
                TinyTrace("LoadingStage READY");
            }
            return;
        }
        return;
    }

    if (m_transport.state == TransportState::Playing) {
        m_transport.timeSeconds += dt;
        
        // Input Handling for Debug Overlay (Alt+D)
#if SHADERLAB_RUNTIME_IMGUI
        bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        bool d = (GetAsyncKeyState('D') & 0x8000) != 0;
        if (alt && d && !m_altPressed) {
            m_showDebug = !m_showDebug;
            m_altPressed = true;
        } else if (!d) {
            m_altPressed = false;
        }
#endif
        
        // Sync Audio
        // if (m_audio) m_audio->Update();

        // Track Logic
        float beatsPerSec = m_transport.bpm / 60.0f;
        float exactBeat = (float)(m_transport.timeSeconds * beatsPerSec);
        m_project.track.currentBeat = (int)std::floor(exactBeat);

        if (m_transitionActive) {
            double transitionEndBeat = m_transitionStartBeat + m_transitionDurationBeats;
            if (exactBeat >= transitionEndBeat) {
                m_transitionActive = false;
                if (m_pendingActiveScene != -2) {
                    SetActiveScene(m_pendingActiveScene);
                    m_activeSceneStartBeat = m_transitionToStartBeat;
                    m_activeSceneOffset = (m_pendingActiveScene >= 0) ? m_transitionToOffset : 0.0f;
                    m_pendingActiveScene = -2;
                    m_transitionJustCompletedBeat = m_project.track.currentBeat;
                }
            }
        }
        
        auto& track = m_project.track;
        if (track.lengthBeats > 0 && track.currentBeat >= track.lengthBeats) {
            if (m_loopPlayback) {
                m_transport.timeSeconds = 0;
                m_project.track.currentBeat = 0;
                m_project.track.lastTriggeredBeat = -1;
            } else {
                m_transport.state = TransportState::Stopped;
#if !SHADERLAB_TINY_PLAYER
                if (m_audio) {
                    m_audio->Stop();
                }
#endif
                m_project.track.currentBeat = track.lengthBeats;
            }
        }

        if (track.currentBeat > track.lastTriggeredBeat) {
             for (int b = track.lastTriggeredBeat + 1; b <= track.currentBeat; ++b) {
                 for(auto& row : track.rows) {
                     if (row.rowId == b) {
                         // Scene
                         if (!row.transitionPresetStem.empty() && row.transitionDuration > 0) {
                            m_transitionActive = true;
                            m_transitionFromIndex = m_activeSceneIndex;
                            m_transitionFromOffset = m_activeSceneOffset;
                            m_transitionFromStartBeat = m_activeSceneStartBeat;
                            m_transitionToStartBeat = static_cast<double>(b);
                            int target = row.sceneIndex;
                            float targetOffset = row.timeOffset;

                            if (target == -1) {
                                const TrackerRow* nextRow = FindNextSceneRow(track, b);
                                if (nextRow) {
                                    target = nextRow->sceneIndex;
                                    targetOffset = nextRow->timeOffset;
                                    m_transitionToStartBeat = static_cast<double>(nextRow->rowId);
                                }
                            }

                            if (target == -1) {
                                targetOffset = 0.0f;
                                m_transitionToStartBeat = static_cast<double>(b);
                            } else if (target == m_activeSceneIndex) {
                                targetOffset = m_activeSceneOffset;
                                m_transitionToStartBeat = m_activeSceneStartBeat;
                            }
                            m_transitionToIndex = target;
                            m_transitionToOffset = targetOffset;
                            m_transitionStartBeat = (double)b;
                            m_transitionDurationBeats = (double)row.transitionDuration;
                            m_currentTransitionStem = row.transitionPresetStem;
                            m_pendingActiveScene = target;
                         } else if (row.sceneIndex >= 0) {
                             if (m_transitionJustCompletedBeat == row.rowId &&
                                 row.sceneIndex == m_activeSceneIndex) {
                                 continue;
                             }
                             if (m_transitionActive && row.sceneIndex == m_pendingActiveScene) {
                                 continue;
                             }
                             m_transitionActive = false;
                             SetActiveScene(row.sceneIndex);
                             m_activeSceneStartBeat = static_cast<double>(b);
                             m_activeSceneOffset = row.timeOffset;
                         }
                         
#if !SHADERLAB_TINY_PLAYER
                         // Audio
                         if (row.musicIndex >= 0 && row.musicIndex < (int)m_project.audioLibrary.size() && m_audio) {
                             auto& clip = m_project.audioLibrary[row.musicIndex];
                             if (loadAudioClip(clip, PackageManager::Get().IsPacked())) {
                                 m_audio->Play();
                             }
                             if(clip.bpm > 0) m_transport.bpm = clip.bpm;
                         }
#endif
                         // Stop
                         if (row.stop) {
                             m_transport.state = TransportState::Stopped;
#if !SHADERLAB_TINY_PLAYER
                             if (m_audio) {
                                 m_audio->Stop();
                             }
#endif
                         }
                     }
                 }
             }
             track.lastTriggeredBeat = track.currentBeat;
        }
        m_transitionJustCompletedBeat = -1;
    }

#if SHADERLAB_RUNTIME_DEBUG_LOG && !SHADERLAB_TINY_PLAYER
    if (m_transport.state != m_debugLastTransportState) {
        SHADERLAB_RT_DEBUG_LOG(
            std::string("Transport state -> ") + TransportStateLabel(m_transport.state)
            + " | time=" + std::to_string(m_transport.timeSeconds)
            + " | beat=" + std::to_string(m_project.track.currentBeat));
        m_debugLastTransportState = m_transport.state;
    }
#endif
}

void DemoPlayer::SetActiveScene(int index) {
    if (index == -1) {
        m_activeSceneIndex = -1;
        return;
    }
    if (index >= 0 && index < (int)m_project.scenes.size()) {
        m_activeSceneIndex = index;
    } else {
        m_activeSceneIndex = -1; // Black
    }
}

bool DemoPlayer::CompileScene(int sceneIndex) {
    if (sceneIndex < 0 || sceneIndex >= (int)m_project.scenes.size()) return false;
    auto& scene = m_project.scenes[sceneIndex];
    if (!m_renderer || !m_rendererReady) {
#if !SHADERLAB_TINY_PLAYER
        RuntimeErr("E207", "renderer unavailable for scene compile");
#endif
        return false;
    }

    bool sceneReady = false;

#if SHADERLAB_TINY_PLAYER
    int16_t moduleId = -1;
    if (sceneIndex >= 0 && sceneIndex < static_cast<int>(m_microSceneModuleIds.size())) {
        moduleId = m_microSceneModuleIds[static_cast<size_t>(sceneIndex)];
    }
    if (moduleId >= 0 && moduleId < static_cast<int>(m_microModuleBytecode.size())) {
        const auto& bytecode = m_microModuleBytecode[static_cast<size_t>(moduleId)];
        if (!bytecode.empty()) {
            scene.pipelineState = m_renderer->CreatePSOFromBytecode(bytecode);
            sceneReady = scene.pipelineState != nullptr;
        }
    }

    for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
        auto& fx = scene.postFxChain[fxIndex];
        CompilePostFxEffect(fx, sceneIndex, static_cast<int>(fxIndex));
    }

    return sceneReady;
#else

    if (!scene.precompiledPath.empty()) {
        std::vector<uint8_t> data;
        if (PackageManager::Get().IsPacked()) {
            if (PackageManager::Get().HasFile(scene.precompiledPath)) {
                data = PackageManager::Get().GetFile(scene.precompiledPath);
                SHADERLAB_RT_DEBUG_LOG("Loaded packed scene shader: " + scene.precompiledPath + " (" + std::to_string(data.size()) + " bytes)");
            } else {
                SHADERLAB_RT_DEBUG_LOG_ERROR("Packed scene shader is missing: " + scene.precompiledPath);
            }
        } else {
            std::string resolvedPath;
            LoadBytecodeFromPathCandidates(scene.precompiledPath, m_manifestPath, data, &resolvedPath);
            if (!data.empty()) {
                SHADERLAB_RT_DEBUG_LOG("Loaded disk scene shader: " + resolvedPath + " (" + std::to_string(data.size()) + " bytes)");
            }
        }

        if (!data.empty()) {
            scene.pipelineState = m_renderer->CreatePSOFromBytecode(data);
            if (scene.pipelineState) {
                SHADERLAB_RT_DEBUG_LOG("Scene PSO created from precompiled shader: " + scene.name);
                sceneReady = true;
            } else {
                SHADERLAB_RT_DEBUG_LOG_ERROR("Failed to create PSO from precompiled shader for scene " + scene.name);
#if !SHADERLAB_TINY_PLAYER
                RuntimeErr("E208", "precompiled scene pso create failed");
#endif
            }
        }
        if (data.empty()) {
            SHADERLAB_RT_DEBUG_LOG_ERROR("No precompiled scene shader data for: " + scene.name);
        }
    }

    if (!sceneReady) {
        SHADERLAB_RT_DEBUG_LOG_ERROR("Missing precompiled scene shader for scene " + scene.name);
    }

    for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
        auto& fx = scene.postFxChain[fxIndex];
        if (!CompilePostFxEffect(fx, sceneIndex, static_cast<int>(fxIndex))) {
            SHADERLAB_RT_DEBUG_LOG_ERROR("Failed to compile post fx for scene " + scene.name + " (" + fx.name + ")");
        }
    }

#if !SHADERLAB_TINY_PLAYER
    for (size_t computeIndex = 0; computeIndex < scene.computeEffectChain.size(); ++computeIndex) {
        auto& effect = scene.computeEffectChain[computeIndex];
        if (!CompileComputeEffect(effect, sceneIndex, static_cast<int>(computeIndex))) {
            SHADERLAB_RT_DEBUG_LOG_ERROR("Failed to compile compute fx for scene " + scene.name + " (" + effect.name + ")");
        }
    }
#endif
    return sceneReady;
#endif
}


bool DemoPlayer::CompilePostFxEffect(Scene::PostFXEffect& effect, int sceneIndex, int fxIndex) {
    if (!m_renderer || !m_rendererReady) return false;

#if SHADERLAB_TINY_PLAYER
    int16_t moduleId = -1;
    if (sceneIndex >= 0 && sceneIndex < static_cast<int>(m_microPostFxModuleIds.size())) {
        const auto& sceneFxModuleIds = m_microPostFxModuleIds[static_cast<size_t>(sceneIndex)];
        if (fxIndex >= 0 && fxIndex < static_cast<int>(sceneFxModuleIds.size())) {
            moduleId = sceneFxModuleIds[static_cast<size_t>(fxIndex)];
        }
    }
    if (moduleId >= 0 && moduleId < static_cast<int>(m_microModuleBytecode.size())) {
        const auto& bytecode = m_microModuleBytecode[static_cast<size_t>(moduleId)];
        if (!bytecode.empty()) {
            effect.pipelineState = m_renderer->CreatePSOFromBytecode(bytecode);
        }
    }
    if (effect.pipelineState) {
        effect.isDirty = false;
        effect.lastCompiledCode = effect.shaderCode;
        return true;
    }
    return false;
#else
    (void)sceneIndex;
    (void)fxIndex;

    if (!effect.precompiledPath.empty()) {
        std::vector<uint8_t> data;
        if (PackageManager::Get().IsPacked()) {
            if (PackageManager::Get().HasFile(effect.precompiledPath)) {
                data = PackageManager::Get().GetFile(effect.precompiledPath);
                SHADERLAB_RT_DEBUG_LOG("Loaded packed post FX shader: " + effect.precompiledPath + " (" + std::to_string(data.size()) + " bytes)");
            } else {
                SHADERLAB_RT_DEBUG_LOG_ERROR("Packed post FX shader is missing: " + effect.precompiledPath);
            }
        } else {
            std::string resolvedPath;
            LoadBytecodeFromPathCandidates(effect.precompiledPath, m_manifestPath, data, &resolvedPath);
            if (!data.empty()) {
                SHADERLAB_RT_DEBUG_LOG("Loaded disk post FX shader: " + resolvedPath + " (" + std::to_string(data.size()) + " bytes)");
            }
        }

        if (!data.empty()) {
            effect.pipelineState = m_renderer->CreatePSOFromBytecode(data);
            if (effect.pipelineState) {
                effect.isDirty = false;
                effect.lastCompiledCode = effect.shaderCode;
                SHADERLAB_RT_DEBUG_LOG("Post FX PSO created from precompiled shader: " + effect.name);
                return true;
            }
        }
        if (data.empty()) {
            SHADERLAB_RT_DEBUG_LOG_ERROR("No precompiled post FX shader data for: " + effect.name);
        }
    }

    SHADERLAB_RT_DEBUG_LOG_ERROR("Missing precompiled post FX shader for: " + effect.name);

    return false;
#endif
}

bool DemoPlayer::CompileComputeEffect(Scene::ComputeEffect& effect, int sceneIndex, int computeIndex) {
#if SHADERLAB_TINY_PLAYER
    (void)effect;
    (void)sceneIndex;
    (void)computeIndex;
    return false;
#else
    (void)sceneIndex;
    (void)computeIndex;
    if (!m_device) return false;
    if (!EnsureRuntimeComputeRootSignature(m_device)) return false;

    std::vector<uint8_t> bytecode;

    if (!effect.precompiledPath.empty()) {
        if (PackageManager::Get().IsPacked()) {
            if (PackageManager::Get().HasFile(effect.precompiledPath)) {
                bytecode = PackageManager::Get().GetFile(effect.precompiledPath);
            }
        } else {
            LoadBytecodeFromPathCandidates(effect.precompiledPath, m_manifestPath, bytecode);
        }
    }

    if (bytecode.empty()) {
        if (effect.shaderCode.empty()) {
            return false;
        }
        const std::string entryPoint = effect.entryPoint.empty() ? "main" : effect.entryPoint;
        ComPtr<ID3DBlob> shaderBlob;
        ComPtr<ID3DBlob> errorBlob;
        if (FAILED(D3DCompile(effect.shaderCode.c_str(),
                              effect.shaderCode.size(),
                              "compute_runtime.hlsl",
                              nullptr,
                              nullptr,
                              entryPoint.c_str(),
                              "cs_5_0",
                              D3DCOMPILE_ENABLE_STRICTNESS,
                              0,
                              shaderBlob.GetAddressOf(),
                              errorBlob.GetAddressOf()))) {
            return false;
        }
        if (!shaderBlob) {
            return false;
        }
        const uint8_t* begin = static_cast<const uint8_t*>(shaderBlob->GetBufferPointer());
        bytecode.assign(begin, begin + shaderBlob->GetBufferSize());
    }

    if (bytecode.empty()) {
        return false;
    }

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {};
    desc.pRootSignature = g_runtimeComputeRootSignature.Get();
    desc.CS = { bytecode.data(), bytecode.size() };

    ComPtr<ID3D12PipelineState> pso;
    if (FAILED(m_device->GetDevice()->CreateComputePipelineState(&desc, IID_PPV_ARGS(pso.GetAddressOf())))) {
        return false;
    }

    effect.pipelineState = pso;
    effect.compiledShaderBytes = bytecode.size();
    effect.isDirty = false;
    effect.lastCompiledCode = effect.shaderCode;
    return true;
#endif
}


ID3D12Resource* DemoPlayer::ApplyComputeChain(ID3D12GraphicsCommandList* commandList,
                                             int sceneIndex,
                                             std::vector<Scene::ComputeEffect>& chain,
                                             ID3D12Resource* inputTexture,
                                             double timeSeconds) {
#if SHADERLAB_TINY_PLAYER
    (void)commandList;
    (void)sceneIndex;
    (void)chain;
    (void)timeSeconds;
    return inputTexture;
#else
    if (!commandList || !inputTexture || !m_device || chain.empty()) return inputTexture;
    if (!EnsureRuntimeComputeRootSignature(m_device) || !EnsureRuntimeComputeDispatchResources(m_device)) {
        return inputTexture;
    }

    bool anyEnabled = false;
    for (const auto& effect : chain) {
        if (effect.enabled) {
            anyEnabled = true;
            break;
        }
    }
    if (!anyEnabled) return inputTexture;

    auto& resources = g_runtimeComputeSceneResources[sceneIndex];
    const bool recreate = !resources.textureA || !resources.textureB || resources.width != m_width || resources.height != m_height;
    if (recreate) {
        resources = {};
        if (!CreateRuntimeUavTexture(m_device, m_width, m_height, resources.textureA) ||
            !CreateRuntimeUavTexture(m_device, m_width, m_height, resources.textureB)) {
            resources = {};
            return inputTexture;
        }
        resources.width = m_width;
        resources.height = m_height;
    }

    ID3D12Device* device = m_device->GetDevice();
    const UINT step = DescriptorStep(device);
    const auto heapCpu = g_runtimeComputeDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    const auto heapGpu = g_runtimeComputeDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

    ID3D12Resource* currentInput = inputTexture;
    ID3D12Resource* outputA = resources.textureA.Get();
    ID3D12Resource* outputB = resources.textureB.Get();
    ID3D12Resource* currentOutput = outputA;

    for (auto& effect : chain) {
        if (!effect.enabled) continue;
        if (effect.isDirty || !effect.pipelineState) {
            if (!CompileComputeEffect(effect, sceneIndex, -1)) {
                continue;
            }
        }

        EnsureComputeHistory(effect);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;

        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        D3D12_CPU_DESCRIPTOR_HANDLE inputCpu = heapCpu;
        device->CreateShaderResourceView(currentInput, &srvDesc, inputCpu);

        for (uint32_t i = 0; i < kComputeHistorySlots; ++i) {
            D3D12_CPU_DESCRIPTOR_HANDLE histCpu = heapCpu;
            histCpu.ptr += static_cast<SIZE_T>(step) * (1 + i);
            ID3D12Resource* historyRes = nullptr;
            const int historyCount = static_cast<int>(effect.historyTextures.size());
            if (historyCount > 0) {
                int readIndex = effect.historyIndex - static_cast<int>(i);
                while (readIndex < 0) readIndex += historyCount;
                readIndex %= historyCount;
                historyRes = effect.historyTextures[static_cast<size_t>(readIndex)].Get();
            }
            if (!historyRes) historyRes = currentInput;
            device->CreateShaderResourceView(historyRes, &srvDesc, histCpu);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE outputCpu = heapCpu;
        outputCpu.ptr += static_cast<SIZE_T>(step) * 9;
        device->CreateUnorderedAccessView(currentOutput, nullptr, &uavDesc, outputCpu);

        if (!g_runtimeComputeParamsMapped || !g_runtimeComputeParamsBuffer) {
            return currentInput;
        }

        ComputeDispatchParams params{};
        params.param0 = effect.param0;
        params.param1 = effect.param1;
        params.param2 = effect.param2;
        params.param3 = effect.param3;
        params.time = static_cast<float>(timeSeconds);
        params.invWidth = m_width > 0 ? 1.0f / static_cast<float>(m_width) : 0.0f;
        params.invHeight = m_height > 0 ? 1.0f / static_cast<float>(m_height) : 0.0f;
        params.frame = static_cast<uint32_t>(m_transport.timeSeconds * 60.0);
        std::memcpy(g_runtimeComputeParamsMapped, &params, sizeof(params));

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_runtimeComputeParamsBuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = Align256(static_cast<uint32_t>(sizeof(ComputeDispatchParams)));
        D3D12_CPU_DESCRIPTOR_HANDLE cbvCpu = heapCpu;
        cbvCpu.ptr += static_cast<SIZE_T>(step) * 10;
        device->CreateConstantBufferView(&cbvDesc, cbvCpu);

        D3D12_RESOURCE_BARRIER beginBarriers[2] = {};
        beginBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        beginBarriers[0].Transition.pResource = currentInput;
        beginBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        beginBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        beginBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        beginBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        beginBarriers[1].Transition.pResource = currentOutput;
        beginBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        beginBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        beginBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(2, beginBarriers);

        ID3D12DescriptorHeap* heaps[] = { g_runtimeComputeDescriptorHeap.Get() };
        commandList->SetDescriptorHeaps(1, heaps);
        commandList->SetComputeRootSignature(g_runtimeComputeRootSignature.Get());
        commandList->SetPipelineState(effect.pipelineState.Get());

        D3D12_GPU_DESCRIPTOR_HANDLE inputGpu = heapGpu;
        D3D12_GPU_DESCRIPTOR_HANDLE historyGpu = heapGpu;
        historyGpu.ptr += static_cast<UINT64>(step) * 1;
        D3D12_GPU_DESCRIPTOR_HANDLE outputGpu = heapGpu;
        outputGpu.ptr += static_cast<UINT64>(step) * 9;
        D3D12_GPU_DESCRIPTOR_HANDLE cbvGpu = heapGpu;
        cbvGpu.ptr += static_cast<UINT64>(step) * 10;

        commandList->SetComputeRootDescriptorTable(0, inputGpu);
        commandList->SetComputeRootDescriptorTable(1, historyGpu);
        commandList->SetComputeRootDescriptorTable(2, outputGpu);
        commandList->SetComputeRootDescriptorTable(3, cbvGpu);

        const uint32_t tgx = (std::max)(1u, effect.threadGroupX);
        const uint32_t tgy = (std::max)(1u, effect.threadGroupY);
        const uint32_t tgz = (std::max)(1u, effect.threadGroupZ);
        const uint32_t groupsX = (m_width + tgx - 1u) / tgx;
        const uint32_t groupsY = (m_height + tgy - 1u) / tgy;
        const uint32_t groupsZ = (1u + tgz - 1u) / tgz;
        commandList->Dispatch(groupsX, groupsY, groupsZ);

        D3D12_RESOURCE_BARRIER uavBarrier = {};
        uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        uavBarrier.UAV.pResource = currentOutput;
        commandList->ResourceBarrier(1, &uavBarrier);

        D3D12_RESOURCE_BARRIER endBarriers[2] = {};
        endBarriers[0] = beginBarriers[0];
        endBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        endBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        endBarriers[1] = beginBarriers[1];
        endBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        endBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        commandList->ResourceBarrier(2, endBarriers);

        if (!effect.historyTextures.empty()) {
            const int historyCount = static_cast<int>(effect.historyTextures.size());
            const int writeIndex = (effect.historyIndex + 1) % historyCount;

            D3D12_RESOURCE_BARRIER preCopy[2] = {};
            preCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopy[0].Transition.pResource = currentOutput;
            preCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            preCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

            preCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopy[1].Transition.pResource = effect.historyTextures[static_cast<size_t>(writeIndex)].Get();
            preCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            preCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            commandList->ResourceBarrier(2, preCopy);

            commandList->CopyResource(effect.historyTextures[static_cast<size_t>(writeIndex)].Get(), currentOutput);

            preCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            preCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            preCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            preCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            commandList->ResourceBarrier(2, preCopy);

            effect.historyIndex = writeIndex;
            effect.historyInitialized = true;
        }

        currentInput = currentOutput;
        currentOutput = (currentOutput == outputA) ? outputB : outputA;
    }

    return currentInput;
#endif
}

}
