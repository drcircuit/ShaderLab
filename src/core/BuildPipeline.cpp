#include "ShaderLab/DevKit/BuildPipeline.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ShaderLab/Core/Serializer.h"
#include "ShaderLab/Core/ShaderLabData.h"
#include "ShaderLab/Shader/ShaderBaseBuild.h"
#include "ShaderLab/Shader/ShaderBaseVertex.h"
#include "ShaderLab/Shader/ShaderCompiler.h"

namespace ShaderLab {

namespace fs = std::filesystem;

namespace {
const char* BuildModeName(BuildMode mode) {
    return mode == BuildMode::ReleaseCrinkled ? "Release Crinkled" : "Release";
}

const char* SizePresetName(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64: return "64K";
        case SizeTargetPreset::K128: return "128K";
        case SizeTargetPreset::K256: return "256K";
        case SizeTargetPreset::K512: return "512K";
        case SizeTargetPreset::K1024: return "1024K";
        default: return "None";
    }
}

const char* BuildTargetName(BuildTargetKind target) {
    switch (target) {
        case BuildTargetKind::PackagedDemo: return "Packaged Demo";
        case BuildTargetKind::SelfContainedDemo: return "Self-Contained Demo";
        case BuildTargetKind::SelfContainedScreenSaver: return "Self-Contained Screensaver";
        case BuildTargetKind::MicroDemo: return "Micro-Demo";
        default: return "Self-Contained Demo";
    }
}

uint64_t SizePresetToBytes(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64: return 64ull * 1024ull;
        case SizeTargetPreset::K128: return 128ull * 1024ull;
        case SizeTargetPreset::K256: return 256ull * 1024ull;
        case SizeTargetPreset::K512: return 512ull * 1024ull;
        case SizeTargetPreset::K1024: return 1024ull * 1024ull;
        default: return 0ull;
    }
}

bool IsTinySizeTarget(SizeTargetPreset preset) {
    switch (preset) {
        case SizeTargetPreset::K64:
        case SizeTargetPreset::K128:
        case SizeTargetPreset::K256:
        case SizeTargetPreset::K512:
        case SizeTargetPreset::K1024:
            return true;
        default:
            return false;
    }
}

constexpr size_t kTransitionSlotCount = 6;
constexpr const char* kTransitionSlotStems[kTransitionSlotCount] = {
    "crossfade",
    "dip_to_black",
    "fade_out",
    "fade_in",
    "glitch",
    "pixelate"
};

std::string CanonicalTransitionStem(const std::string& transitionPresetStem) {
    std::string stem = transitionPresetStem;
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return stem;
}

int TransitionSlotIndexFromStem(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionStem(transitionPresetStem);
    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        if (canonicalStem == kTransitionSlotStems[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string CanonicalTransitionShaderStem(const std::string& transitionPresetStem) {
    return CanonicalTransitionStem(transitionPresetStem);
}

std::string TrimString(const std::string& value) {
    const char* whitespace = " \t\r\n";
    size_t start = value.find_first_not_of(whitespace);
    if (start == std::string::npos) return "";
    size_t end = value.find_last_not_of(whitespace);
    return value.substr(start, end - start + 1);
}

bool PathContainsTokenCaseInsensitive(const fs::path& path, const std::string& token) {
    std::string value = path.string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    std::string tokenLower = token;
    std::transform(tokenLower.begin(), tokenLower.end(), tokenLower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return value.find(tokenLower) != std::string::npos;
}

bool FileExists(const fs::path& path) {
    std::error_code ec;
    return fs::exists(path, ec);
}

bool HasStandaloneSourceTree(const fs::path& root) {
    if (root.empty()) {
        return false;
    }

    return
        FileExists(root / "templates" / "Standalone_CMakeLists.txt") &&
        FileExists(root / "include") &&
        FileExists(root / "src") &&
        FileExists(root / "third_party");
}

bool HasDevKitSourceTree(const fs::path& root, bool requireMicroSources) {
    if (root.empty()) {
        return false;
    }

    const bool basicTree =
        FileExists(root / "CMakeLists.txt") &&
        FileExists(root / "include") &&
        FileExists(root / "src") &&
        FileExists(root / "third_party");

    if (!basicTree) {
        return false;
    }

    if (requireMicroSources && !FileExists(root / "src" / "app" / "tiny")) {
        return false;
    }

    return true;
}

fs::path GetExecutableDirectory() {
    char exePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return fs::path(std::string(exePath, length)).parent_path();
}

bool IsUsableAppRoot(const fs::path& root) {
    if (root.empty()) {
        return false;
    }

    const bool hasDevKit = HasDevKitSourceTree(root / "dev_kit", false);
    const bool hasSourceTree = HasStandaloneSourceTree(root);

    return hasDevKit || hasSourceTree;
}

fs::path ResolveStandaloneSourceRoot(const fs::path& appRoot) {
    std::vector<fs::path> candidates;
    if (!appRoot.empty()) {
        candidates.push_back(appRoot);

        fs::path current = appRoot;
        for (int i = 0; i < 5; ++i) {
            current = current.parent_path();
            if (current.empty()) {
                break;
            }
            candidates.push_back(current);
        }
    }

    for (const fs::path& candidate : candidates) {
        if (HasStandaloneSourceTree(candidate)) {
            return candidate;
        }
    }

    return {};
}

fs::path ResolveBuildAppRoot(const fs::path& requestedRoot) {
    std::vector<fs::path> candidates;
    if (!requestedRoot.empty()) {
        candidates.push_back(requestedRoot);
    }

    const fs::path exeDir = GetExecutableDirectory();
    if (!exeDir.empty()) {
        candidates.push_back(exeDir);
        candidates.push_back(exeDir.parent_path());
    }

    std::error_code ec;
    const fs::path cwd = fs::current_path(ec);
    if (!ec && !cwd.empty()) {
        candidates.push_back(cwd);
    }

    for (const fs::path& candidate : candidates) {
        if (IsUsableAppRoot(candidate)) {
            return candidate;
        }
    }

    if (!requestedRoot.empty()) {
        return requestedRoot;
    }
    if (!exeDir.empty()) {
        return exeDir;
    }
    return cwd;
}

bool HasCleanSolutionSourceTree(const fs::path& root) {
    if (root.empty()) {
        return false;
    }

    return
        FileExists(root / "include") &&
        FileExists(root / "src" / "app" / "runtime") &&
        FileExists(root / "src" / "audio") &&
        FileExists(root / "src" / "core") &&
        FileExists(root / "src" / "graphics") &&
        FileExists(root / "src" / "shader") &&
        FileExists(root / "third_party");
}

fs::path ResolveCleanSolutionSourceRoot(const fs::path& appRoot) {
    std::vector<fs::path> candidates;
    if (!appRoot.empty()) {
        candidates.push_back(appRoot);
        candidates.push_back(appRoot / "dev_kit");

        fs::path current = appRoot;
        for (int i = 0; i < 4; ++i) {
            current = current.parent_path();
            if (current.empty()) {
                break;
            }
            candidates.push_back(current);
            candidates.push_back(current / "dev_kit");
        }
    }

    for (const fs::path& candidate : candidates) {
        if (HasCleanSolutionSourceTree(candidate)) {
            return candidate;
        }
    }

    return {};
}

bool FindOnPath(const std::string& exeName) {
    DWORD size = GetEnvironmentVariableA("PATH", nullptr, 0);
    if (size == 0) return false;
    std::string buffer(size, '\0');
    GetEnvironmentVariableA("PATH", buffer.data(), size);

    size_t start = 0;
    while (start < buffer.size()) {
        size_t end = buffer.find(';', start);
        if (end == std::string::npos) end = buffer.size();
        std::string dir = buffer.substr(start, end - start);
        dir = TrimString(dir);
        if (!dir.empty()) {
            fs::path candidate = fs::path(dir) / exeName;
            if (FileExists(candidate)) return true;
        }
        start = end + 1;
    }
    return false;
}

bool FindOnPathFull(const std::string& exeName, std::string& outPath) {
    DWORD size = GetEnvironmentVariableA("PATH", nullptr, 0);
    if (size == 0) return false;
    std::string buffer(size, '\0');
    GetEnvironmentVariableA("PATH", buffer.data(), size);

    size_t start = 0;
    while (start < buffer.size()) {
        size_t end = buffer.find(';', start);
        if (end == std::string::npos) end = buffer.size();
        std::string dir = buffer.substr(start, end - start);
        dir = TrimString(dir);
        if (!dir.empty()) {
            fs::path candidate = fs::path(dir) / exeName;
            if (FileExists(candidate)) {
                outPath = candidate.string();
                return true;
            }
        }
        start = end + 1;
    }
    return false;
}

bool FindVcVarsScript(const char* scriptName, std::string& outPath) {
    const char* knownPaths[] = {
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional",
        "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise",
        "C:\\Program Files (x86)\\Microsoft Visual Studio\\2022\\BuildTools"
    };

    for (const char* base : knownPaths) {
        fs::path vcvars = fs::path(base) / "VC" / "Auxiliary" / "Build" / scriptName;
        if (FileExists(vcvars)) {
            outPath = vcvars.string();
            return true;
        }
    }

    fs::path vswhere = fs::path("C:\\Program Files (x86)\\Microsoft Visual Studio\\Installer\\vswhere.exe");
    if (!FileExists(vswhere)) return false;

    std::string cmd = "\"" + vswhere.string() + "\" -latest -property installationPath";
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return false;

    char buffer[512];
    std::string output;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        output += buffer;
    }
    _pclose(pipe);

    output = TrimString(output);
    if (output.empty()) return false;

    fs::path vcvars = fs::path(output) / "VC" / "Auxiliary" / "Build" / scriptName;
    if (FileExists(vcvars)) {
        outPath = vcvars.string();
        return true;
    }

    return false;
}

bool FindVcVars(std::string& outPath) {
    return FindVcVarsScript("vcvars64.bat", outPath);
}

bool FindVcVars32(std::string& outPath) {
    return FindVcVarsScript("vcvars32.bat", outPath);
}

bool FindVcVarsAll(std::string& outPath) {
    return FindVcVarsScript("vcvarsall.bat", outPath);
}

bool HasMsvcToolsetPrefix(const fs::path& vcvarsPath, const std::string& prefix) {
    std::error_code ec;
    fs::path vcDir = vcvarsPath.parent_path().parent_path().parent_path();
    fs::path toolsDir = vcDir / "Tools" / "MSVC";
    if (!fs::exists(toolsDir, ec) || ec) return false;

    for (const auto& entry : fs::directory_iterator(toolsDir, ec)) {
        if (ec || !entry.is_directory()) continue;
        std::string name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            return true;
        }
    }
    return false;
}

bool HasWindowsSdk(std::string& outPath) {
    fs::path base = "C:\\Program Files (x86)\\Windows Kits\\10\\Include";
    if (!FileExists(base)) return false;

    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(base, ec)) {
        if (ec || !entry.is_directory()) continue;
        fs::path header = entry.path() / "um" / "d3d12.h";
        if (FileExists(header)) {
            outPath = entry.path().string();
            return true;
        }
    }
    return false;
}

struct BundledWindowsSdkInfo {
    bool found = false;
    fs::path root;
    fs::path includeVersionDir;
    fs::path libVersionDir;
    fs::path binVersionDir;
    std::string versionTag;
};

std::vector<fs::path> BundledWindowsSdkRoots(const fs::path& baseRoot) {
    return {
        baseRoot / "third_party" / "windows_sdk_bundle",
        baseRoot / "windows_sdk_bundle"
    };
}

bool ResolveBundledWindowsSdk(const fs::path& baseRoot, BundledWindowsSdkInfo& outInfo) {
    std::error_code ec;
    for (const auto& bundleRoot : BundledWindowsSdkRoots(baseRoot)) {
        fs::path includeRoot = bundleRoot / "Include";
        fs::path libRoot = bundleRoot / "Lib";
        fs::path binRoot = bundleRoot / "bin";
        if (!fs::exists(includeRoot, ec) || ec || !fs::exists(libRoot, ec) || ec || !fs::exists(binRoot, ec) || ec) {
            continue;
        }

        std::vector<fs::path> versions;
        for (const auto& entry : fs::directory_iterator(includeRoot, ec)) {
            if (ec || !entry.is_directory()) continue;
            versions.push_back(entry.path().filename());
        }
        if (versions.empty()) {
            continue;
        }

        std::sort(versions.begin(), versions.end(), [](const fs::path& a, const fs::path& b) {
            return a.string() > b.string();
        });

        for (const auto& versionName : versions) {
            fs::path includeVersionDir = includeRoot / versionName;
            fs::path libVersionDir = libRoot / versionName;
            fs::path binVersionDir = binRoot / versionName;

            bool hasHeaders = FileExists(includeVersionDir / "um" / "d3d12.h");
            bool hasLibs = FileExists(libVersionDir / "um" / "x86" / "kernel32.lib")
                && FileExists(libVersionDir / "um" / "x64" / "kernel32.lib")
                && FileExists(libVersionDir / "ucrt" / "x86" / "ucrt.lib")
                && FileExists(libVersionDir / "ucrt" / "x64" / "ucrt.lib");
            bool hasTools = FileExists(binVersionDir / "x86" / "rc.exe")
                && FileExists(binVersionDir / "x64" / "rc.exe");

            if (hasHeaders && hasLibs && hasTools) {
                outInfo.found = true;
                outInfo.root = bundleRoot;
                outInfo.includeVersionDir = includeVersionDir;
                outInfo.libVersionDir = libVersionDir;
                outInfo.binVersionDir = binVersionDir;
                outInfo.versionTag = versionName.string();
                return true;
            }
        }
    }

    return false;
}

bool HasBundledWindowsSdk(const fs::path& baseRoot, std::string& outPath) {
    BundledWindowsSdkInfo info;
    if (!ResolveBundledWindowsSdk(baseRoot, info)) {
        return false;
    }
    outPath = info.root.string();
    return true;
}

bool ResolveCrinklerPath(const fs::path& appRoot, std::string& outPath) {
    auto toAbsolutePathString = [](const fs::path& path) -> std::string {
        std::error_code ec;
        fs::path absolutePath = fs::absolute(path, ec);
        if (!ec && !absolutePath.empty()) {
            return absolutePath.string();
        }
        return path.string();
    };

    auto checkCandidates = [&](const std::vector<fs::path>& candidates) -> bool {
        for (const auto& candidate : candidates) {
            if (FileExists(candidate)) {
                outPath = toAbsolutePathString(candidate);
                return true;
            }
        }
        return false;
    };

    std::vector<fs::path> preferredBundledCandidates;

    auto appendCrinklerCandidatesForRoot = [&](const fs::path& root) {
        if (root.empty()) {
            return;
        }

        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "crinkler" / "Win32" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "Crinkler" / "Win32" / "Crinkler.exe");
        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "crinkler" / "Win64" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "Crinkler" / "Win64" / "Crinkler.exe");
        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "dev_kit" / "third_party" / "Crinkler.exe");

        preferredBundledCandidates.push_back(root / "third_party" / "crinkler" / "Win32" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "third_party" / "Crinkler" / "Win32" / "Crinkler.exe");
        preferredBundledCandidates.push_back(root / "third_party" / "crinkler" / "Win64" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "third_party" / "Crinkler" / "Win64" / "Crinkler.exe");
        preferredBundledCandidates.push_back(root / "third_party" / "crinkler.exe");
        preferredBundledCandidates.push_back(root / "third_party" / "Crinkler.exe");
    };

    appendCrinklerCandidatesForRoot(appRoot);
    fs::path current = appRoot;
    for (int i = 0; i < 6; ++i) {
        current = current.parent_path();
        if (current.empty()) {
            break;
        }
        appendCrinklerCandidatesForRoot(current);
    }

    if (checkCandidates(preferredBundledCandidates)) {
        return true;
    }

    auto envToPath = [&](const char* varName) -> bool {
        char buffer[MAX_PATH] = {};
        DWORD len = GetEnvironmentVariableA(varName, buffer, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) return false;
        std::string value = TrimString(buffer);
        if (value.empty()) return false;

        fs::path candidate(value);
        std::error_code ec;
        if (fs::is_directory(candidate, ec) && !ec) {
            fs::path exePath = candidate / "crinkler.exe";
            if (FileExists(exePath)) {
                outPath = toAbsolutePathString(exePath);
                return true;
            }
        } else if (FileExists(candidate)) {
            outPath = toAbsolutePathString(candidate);
            return true;
        }
        return false;
    };

    if (envToPath("SHADERLAB_CRINKLER")) return true;
    if (envToPath("CRINKLER_PATH")) return true;

    if (FindOnPathFull("crinkler.exe", outPath)) return true;

    if (checkCandidates(preferredBundledCandidates)) {
        return true;
    }

    const char* knownPaths[] = {
        "C:\\Program Files\\Crinkler\\crinkler.exe",
        "C:\\Program Files (x86)\\Crinkler\\crinkler.exe"
    };
    for (const char* path : knownPaths) {
        if (FileExists(path)) {
            outPath = path;
            return true;
        }
    }
    return false;
}

bool IsTruthyEnvVar(const char* name) {
    char buffer[32] = {};
    DWORD len = GetEnvironmentVariableA(name, buffer, static_cast<DWORD>(sizeof(buffer)));
    if (len == 0 || len >= sizeof(buffer)) return false;
    std::string value = TrimString(buffer);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

bool HasDxcRuntime(const fs::path& appRoot) {
    const char* dllName = "dxcompiler.dll";
    if (FindOnPath(dllName)) {
        return true;
    }

    const fs::path roots[] = {
        appRoot,
        appRoot / "build" / "bin",
        appRoot / "bin",
        appRoot / "dev_kit" / "bin"
    };

    for (const auto& root : roots) {
        if (FileExists(root / dllName)) {
            return true;
        }
    }

    return false;
}

std::string BuildPixelShaderSource(const std::string& shaderSource,
                                   const std::vector<ShaderBase::TextureBindingDecl>& textureDecls,
                                   bool flipFragCoord = false,
                                   const std::string& shaderEntryPoint = "main") {
    return ShaderBase::BuildBuildPixelShaderTemplate(shaderSource, textureDecls, flipFragCoord, shaderEntryPoint);
}

bool WriteBinaryFile(const fs::path& path, const std::vector<uint8_t>& data, std::string& outError) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        outError = "Failed to write: " + path.string();
        return false;
    }
    file.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!file.good()) {
        outError = "Failed to write: " + path.string();
        return false;
    }
    return true;
}

bool EnsureOutputArtifactWritable(const fs::path& outputPath, std::string& outError) {
    std::error_code ec;
    if (!outputPath.parent_path().empty()) {
        fs::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            outError = "Failed to create output directory: " + outputPath.parent_path().string();
            return false;
        }
    }

    if (!fs::exists(outputPath, ec) || ec) {
        return true;
    }

    const std::string outputPathString = outputPath.string();
    HANDLE outputHandle = CreateFileA(outputPathString.c_str(),
                                      GENERIC_WRITE,
                                      FILE_SHARE_READ,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_ATTRIBUTE_NORMAL,
                                      nullptr);
    if (outputHandle == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION || error == ERROR_ACCESS_DENIED) {
            outError = "Output artifact is in use or write-protected: " + outputPathString +
                       ". Close any running instance before rebuilding.";
        } else {
            outError = "Unable to open output artifact for overwrite: " + outputPathString +
                       " (Win32 error " + std::to_string(static_cast<unsigned int>(error)) + ").";
        }
        return false;
    }

    CloseHandle(outputHandle);
    return true;
}

bool WriteTextFile(const fs::path& path, const std::string& content, std::string& outError) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        outError = "Failed to write: " + path.string();
        return false;
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file.good()) {
        outError = "Failed to write: " + path.string();
        return false;
    }
    return true;
}

bool WriteMicroUbershaderBytecodeBlob(const std::vector<std::vector<uint8_t>>& modules,
                                      const fs::path& outputPath,
                                      std::string& outError) {
    std::vector<uint8_t> blob;
    const uint32_t magic = 0x30425553u; // 'SUB0'
    const uint16_t version = 1;
    const uint16_t moduleCount = static_cast<uint16_t>((std::min)(modules.size(), static_cast<size_t>(65535)));

    auto appendU16 = [&blob](uint16_t value) {
        blob.push_back(static_cast<uint8_t>(value & 0xFFu));
        blob.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
    };
    auto appendU32 = [&blob](uint32_t value) {
        blob.push_back(static_cast<uint8_t>(value & 0xFFu));
        blob.push_back(static_cast<uint8_t>((value >> 8) & 0xFFu));
        blob.push_back(static_cast<uint8_t>((value >> 16) & 0xFFu));
        blob.push_back(static_cast<uint8_t>((value >> 24) & 0xFFu));
    };

    appendU32(magic);
    appendU16(version);
    appendU16(moduleCount);

    const size_t tableOffset = blob.size();
    blob.resize(blob.size() + static_cast<size_t>(moduleCount) * 8u, 0u);

    auto writeU32At = [&blob](size_t pos, uint32_t value) {
        blob[pos + 0] = static_cast<uint8_t>(value & 0xFFu);
        blob[pos + 1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
        blob[pos + 2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
        blob[pos + 3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    };

    for (uint16_t moduleIndex = 0; moduleIndex < moduleCount; ++moduleIndex) {
        const auto& bytecode = modules[moduleIndex];
        const uint32_t offset = static_cast<uint32_t>(blob.size());
        const uint32_t size = static_cast<uint32_t>(bytecode.size());
        const size_t rowPos = tableOffset + static_cast<size_t>(moduleIndex) * 8u;
        writeU32At(rowPos, offset);
        writeU32At(rowPos + 4u, size);
        blob.insert(blob.end(), bytecode.begin(), bytecode.end());
    }

    return WriteBinaryFile(outputPath, blob, outError);
}

constexpr uint32_t kEmbeddedTrackMagic = 0x4B52544Du; // 'MTRK'

struct EmbeddedTrackFooter {
    uint32_t magic = kEmbeddedTrackMagic;
    uint32_t payloadSize = 0;
    uint32_t version = 1;
};

template<typename T>
void AppendValue(std::vector<uint8_t>& output, const T& value) {
    const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
    output.insert(output.end(), ptr, ptr + sizeof(T));
}

std::string GetTransitionShaderSourceForBuild(const std::string& transitionPresetStem);
std::string MinifyShaderTextForPack(const std::string& input);

struct TinyModuleMap {
    std::vector<std::string> modules;
    std::vector<std::string> moduleEntrypoints;
    std::vector<std::string> moduleLabels;
    std::vector<int16_t> sceneModuleIndices;
    std::vector<std::vector<int16_t>> postFxModuleIndices;
    int16_t transitionModuleIndices[kTransitionSlotCount] = { -1, -1, -1, -1, -1, -1 };
};

struct ParsedFunctionDecl {
    std::string signatureKey;
    std::string signatureDisplay;
    std::string functionName;
    size_t signatureStart = 0;
    size_t bodyStart = 0;
    size_t bodyEnd = 0;
};

struct ModuleConflictBinding {
    int moduleIndex = -1;
    std::string moduleEntrypoint;
    std::string moduleLabel;
    std::string signature;
    std::string snippet;
    std::string functionName;
    size_t signatureStart = 0;
    size_t bodyEnd = 0;
};

bool IsIdentifierStart(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool IsIdentifierChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

std::string ReadIdentifier(const std::string& source, size_t& pos) {
    if (pos >= source.size() || !IsIdentifierStart(source[pos])) {
        return {};
    }
    const size_t start = pos;
    ++pos;
    while (pos < source.size() && IsIdentifierChar(source[pos])) {
        ++pos;
    }
    return source.substr(start, pos - start);
}

void SkipWhitespace(const std::string& source, size_t& pos) {
    while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos])) != 0) {
        ++pos;
    }
}

size_t FindMatchingPair(const std::string& source, size_t openPos, char open, char close) {
    if (openPos >= source.size() || source[openPos] != open) {
        return std::string::npos;
    }
    int depth = 0;
    for (size_t i = openPos; i < source.size(); ++i) {
        if (source[i] == open) {
            ++depth;
        } else if (source[i] == close) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::vector<ParsedFunctionDecl> ParseTopLevelFunctions(const std::string& source) {
    std::vector<ParsedFunctionDecl> out;

    size_t scan = 0;
    while (scan < source.size()) {
        size_t cursor = scan;
        const size_t signatureStart = cursor;
        const std::string returnType = ReadIdentifier(source, cursor);
        if (returnType.empty()) {
            ++scan;
            continue;
        }

        SkipWhitespace(source, cursor);
        const std::string functionName = ReadIdentifier(source, cursor);
        if (functionName.empty()) {
            scan = cursor;
            continue;
        }

        SkipWhitespace(source, cursor);
        if (cursor >= source.size() || source[cursor] != '(') {
            scan = cursor;
            continue;
        }

        const size_t closeParen = FindMatchingPair(source, cursor, '(', ')');
        if (closeParen == std::string::npos) {
            break;
        }

        size_t bodyCursor = closeParen + 1;
        SkipWhitespace(source, bodyCursor);
        if (bodyCursor >= source.size() || source[bodyCursor] != '{') {
            scan = cursor + 1;
            continue;
        }

        const size_t bodyEnd = FindMatchingPair(source, bodyCursor, '{', '}');
        if (bodyEnd == std::string::npos) {
            break;
        }

        const std::string params = source.substr(cursor + 1, closeParen - cursor - 1);
        ParsedFunctionDecl decl;
        decl.signatureKey = returnType + " " + functionName + "(" + params + ")";
        decl.signatureDisplay = returnType + " " + functionName + "(" + params + ")";
        decl.functionName = functionName;
        decl.signatureStart = signatureStart;
        decl.bodyStart = bodyCursor;
        decl.bodyEnd = bodyEnd;
        out.push_back(std::move(decl));

        scan = bodyEnd + 1;
    }

    return out;
}

std::unordered_map<std::string, std::vector<ModuleConflictBinding>> BuildMicroConflictBindings(const TinyModuleMap& map) {
    std::unordered_map<std::string, std::vector<ModuleConflictBinding>> grouped;
    for (size_t moduleIndex = 0; moduleIndex < map.modules.size(); ++moduleIndex) {
        const std::string& moduleCode = map.modules[moduleIndex];
        const std::string moduleEntrypoint =
            (moduleIndex < map.moduleEntrypoints.size()) ? map.moduleEntrypoints[moduleIndex] : std::string();
        const std::string moduleLabel =
            (moduleIndex < map.moduleLabels.size()) ? map.moduleLabels[moduleIndex] : std::string();

        const auto functions = ParseTopLevelFunctions(moduleCode);
        for (const auto& fn : functions) {
            if (fn.functionName.empty() || fn.functionName == moduleEntrypoint) {
                continue;
            }

            ModuleConflictBinding binding;
            binding.moduleIndex = static_cast<int>(moduleIndex);
            binding.moduleEntrypoint = moduleEntrypoint;
            binding.moduleLabel = moduleLabel;
            binding.signature = fn.signatureDisplay;
            binding.functionName = fn.functionName;
            binding.signatureStart = fn.signatureStart;
            binding.bodyEnd = fn.bodyEnd;

            const size_t snippetEnd = (std::min)(fn.bodyEnd + 1, fn.signatureStart + static_cast<size_t>(320));
            binding.snippet = moduleCode.substr(fn.signatureStart, snippetEnd - fn.signatureStart);

            grouped[fn.signatureKey].push_back(std::move(binding));
        }
    }
    return grouped;
}

std::string ScopeLocalFunctionsForModule(
    const std::string& shaderCode,
    const std::string& entrypointName,
    const std::unordered_set<std::string>* preserveGlobalNames = nullptr) {
    auto isIdentStart = [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
    };
    auto isIdentChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };

    auto readIdent = [&](const std::string& src, size_t& pos) -> std::string {
        if (pos >= src.size() || !isIdentStart(src[pos])) {
            return {};
        }
        const size_t start = pos;
        ++pos;
        while (pos < src.size() && isIdentChar(src[pos])) {
            ++pos;
        }
        return src.substr(start, pos - start);
    };

    auto skipWhitespace = [](const std::string& src, size_t& pos) {
        while (pos < src.size() && std::isspace(static_cast<unsigned char>(src[pos])) != 0) {
            ++pos;
        }
    };

    auto findMatchingParen = [](const std::string& src, size_t openPos) -> size_t {
        if (openPos >= src.size() || src[openPos] != '(') {
            return std::string::npos;
        }
        int depth = 0;
        for (size_t i = openPos; i < src.size(); ++i) {
            if (src[i] == '(') {
                ++depth;
            } else if (src[i] == ')') {
                --depth;
                if (depth == 0) {
                    return i;
                }
            }
        }
        return std::string::npos;
    };

    std::unordered_map<std::string, std::string> renameMap;
    size_t scan = 0;
    while (scan < shaderCode.size()) {
        size_t cursor = scan;
        std::string returnType = readIdent(shaderCode, cursor);
        if (returnType.empty()) {
            ++scan;
            continue;
        }

        skipWhitespace(shaderCode, cursor);
        std::string funcName = readIdent(shaderCode, cursor);
        if (funcName.empty() || 
            funcName == "if" || funcName == "while" || funcName == "for" || funcName == "switch" || funcName == "return" || funcName == "else") {
            scan = cursor;
            continue;
        }

        skipWhitespace(shaderCode, cursor);
        if (cursor >= shaderCode.size() || shaderCode[cursor] != '(') {
            scan = cursor;
            continue;
        }

        const size_t closeParen = findMatchingParen(shaderCode, cursor);
        if (closeParen == std::string::npos) {
            break;
        }

        size_t bodyCursor = closeParen + 1;
        skipWhitespace(shaderCode, bodyCursor);
        if (bodyCursor < shaderCode.size() && shaderCode[bodyCursor] == '{') {
            if (funcName != entrypointName && (!preserveGlobalNames || preserveGlobalNames->find(funcName) == preserveGlobalNames->end())) {
                renameMap[funcName] = "__" + entrypointName + "_" + funcName;
            }
            scan = bodyCursor + 1;
            continue;
        }

        scan = cursor + 1;
    }

    if (renameMap.empty()) {
        return shaderCode;
    }

    std::string out;
    out.reserve(shaderCode.size() + renameMap.size() * 16);

    size_t pos = 0;
    while (pos < shaderCode.size()) {
        if (!isIdentStart(shaderCode[pos])) {
            out.push_back(shaderCode[pos]);
            ++pos;
            continue;
        }

        size_t tokenStart = pos;
        ++pos;
        while (pos < shaderCode.size() && isIdentChar(shaderCode[pos])) {
            ++pos;
        }
        const std::string token = shaderCode.substr(tokenStart, pos - tokenStart);

        size_t lookahead = pos;
        while (lookahead < shaderCode.size() && std::isspace(static_cast<unsigned char>(shaderCode[lookahead])) != 0) {
            ++lookahead;
        }

        const auto it = renameMap.find(token);
        if (it != renameMap.end() && lookahead < shaderCode.size() && shaderCode[lookahead] == '(') {
            out += it->second;
        } else {
            out += token;
        }
    }

    return out;
}

std::string RemapMainToEntrypoint(const std::string& shaderCode, const std::string& entrypointName) {
    auto isIdentChar = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
    };

    auto hasDeclaredFunction = [&](const std::string& source, const std::string& name) -> bool {
        static const char* returnTypes[] = {"float4", "half4", "fixed4"};
        for (const char* returnType : returnTypes) {
            const std::string signatureA = std::string(returnType) + " " + name + "(";
            const std::string signatureB = std::string(returnType) + " " + name + " (";
            if (source.find(signatureA) != std::string::npos || source.find(signatureB) != std::string::npos) {
                return true;
            }
        }
        return false;
    };

    auto appendAdapter = [&](std::string& source, const std::string& fromName) {
        source += "\nfloat4 " + entrypointName + "(float2 fragCoord, float2 iResolution, float iTime){ return " +
                  fromName + "(fragCoord, iResolution, iTime); }\n";
    };

    auto replaceFunctionDeclName = [&](std::string& source, const std::string& oldName) -> bool {
        static const char* returnTypes[] = {"float4", "half4", "fixed4"};
        for (const char* returnType : returnTypes) {
            const std::string prefix = std::string(returnType) + " ";
            const std::string compact = prefix + oldName + "(";
            size_t pos = source.find(compact);
            if (pos != std::string::npos) {
                source.replace(pos + prefix.size(), oldName.size(), entrypointName);
                return true;
            }

            const std::string spaced = prefix + oldName + " (";
            pos = source.find(spaced);
            if (pos != std::string::npos) {
                source.replace(pos + prefix.size(), oldName.size(), entrypointName);
                return true;
            }
        }
        return false;
    };

    std::string out = shaderCode;
    if (hasDeclaredFunction(out, entrypointName)) {
        return out;
    }

    if (replaceFunctionDeclName(out, "main")) {
        return out;
    }

    static const char* friendlyEntrypoints[] = {
        "mainImage",
        "sceneMain",
        "postFxMain",
        "transitionMain",
        "scene",
        "postfx",
        "transition",
        "fxMain",
        "effectMain",
        "render"
    };

    for (const char* candidate : friendlyEntrypoints) {
        const std::string name(candidate);
        if (hasDeclaredFunction(out, name)) {
            appendAdapter(out, name);
            return out;
        }
    }

    size_t scan = 0;
    while (true) {
        scan = out.find("float4", scan);
        if (scan == std::string::npos) {
            break;
        }
        size_t i = scan + 6;
        while (i < out.size() && std::isspace(static_cast<unsigned char>(out[i])) != 0) {
            ++i;
        }
        size_t nameStart = i;
        while (i < out.size() && isIdentChar(out[i])) {
            ++i;
        }
        if (i <= nameStart) {
            scan += 6;
            continue;
        }
        const std::string fnName = out.substr(nameStart, i - nameStart);
        while (i < out.size() && std::isspace(static_cast<unsigned char>(out[i])) != 0) {
            ++i;
        }
        if (i < out.size() && out[i] == '(' && fnName != entrypointName) {
            appendAdapter(out, fnName);
            return out;
        }
        scan += 6;
    }

    out += "\nfloat4 " + entrypointName + "(float2 fragCoord, float2 iResolution, float iTime){ return float4(1.0, 0.0, 1.0, 1.0); }\n";
    return out;
}

TinyModuleMap BuildTinyModuleMap(const ProjectData& project, bool scopeLocalFunctionsForUbershader) {
    TinyModuleMap map;
    map.sceneModuleIndices.resize(project.scenes.size(), -1);
    map.postFxModuleIndices.resize(project.scenes.size());

    int postFxCounter = 0;
    int transitionCounter = 0;

    bool usedTransitions[kTransitionSlotCount] = {};
    for (const auto& row : project.track.rows) {
        if (row.transitionPresetStem.empty()) continue;
        const int idx = TransitionSlotIndexFromStem(row.transitionPresetStem);
        if (idx >= 0 && idx < static_cast<int>(kTransitionSlotCount)) usedTransitions[idx] = true;
    }

    auto appendModule = [&](const std::string& shaderCode, const std::string& entrypointName, const std::string& moduleLabel) -> int16_t {
        const int16_t moduleId = static_cast<int16_t>((std::min)(static_cast<size_t>(32767), map.modules.size()));
        const std::string remapped = RemapMainToEntrypoint(shaderCode, entrypointName);
        const std::string scoped = scopeLocalFunctionsForUbershader
            ? ScopeLocalFunctionsForModule(remapped, entrypointName)
            : remapped;
        const std::string compact = MinifyShaderTextForPack(scoped);
        if (compact.empty()) {
            return -1;
        }
        map.modules.push_back(compact);
        map.moduleEntrypoints.push_back(entrypointName);
        map.moduleLabels.push_back(moduleLabel);
        return moduleId;
    };

    for (size_t i = 0; i < kTransitionSlotCount; ++i) {
        if (!usedTransitions[i]) continue;
        const std::string entrypoint = "t" + std::to_string(transitionCounter++);
        map.transitionModuleIndices[i] = appendModule(GetTransitionShaderSourceForBuild(kTransitionSlotStems[i]), entrypoint, "Transition " + std::to_string(i));
    }

    for (size_t sceneIndex = 0; sceneIndex < project.scenes.size(); ++sceneIndex) {
        const auto& scene = project.scenes[sceneIndex];
        map.sceneModuleIndices[sceneIndex] = appendModule(scene.shaderCode, "s" + std::to_string(sceneIndex), "Scene " + std::to_string(sceneIndex));

        auto& fxModules = map.postFxModuleIndices[sceneIndex];
        fxModules.resize(scene.postFxChain.size(), -1);
        for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
            const auto& fx = scene.postFxChain[fxIndex];
            if (!fx.enabled) continue;
            fxModules[fxIndex] = appendModule(fx.shaderCode, "p" + std::to_string(postFxCounter++), "Scene " + std::to_string(sceneIndex) + " PostFX " + std::to_string(fxIndex));
        }
    }

    return map;
}

bool BuildCompactTrackBinary(const ProjectData& project, std::vector<uint8_t>& outData) {
    const DemoTrack& track = project.track;
    const TinyModuleMap moduleMap = BuildTinyModuleMap(project, false);

    outData.clear();
    size_t fxMappingCount = 0;
    for (const auto& sceneFx : moduleMap.postFxModuleIndices) {
        fxMappingCount += sceneFx.size();
    }
    outData.reserve(24 + (project.scenes.size() * 6) + (fxMappingCount * 2) + track.rows.size() * 9);

    const auto appendU8 = [&outData](uint8_t value) {
        outData.push_back(value);
    };
    const auto appendI8 = [&outData](int8_t value) {
        outData.push_back(static_cast<uint8_t>(value));
    };
    const auto appendU16 = [&appendU8](uint16_t value) {
        appendU8(static_cast<uint8_t>(value & 0xFFu));
        appendU8(static_cast<uint8_t>((value >> 8) & 0xFFu));
    };
    const auto appendI16 = [&appendU16](int16_t value) {
        appendU16(static_cast<uint16_t>(value));
    };

    const uint16_t bpmQ8 = static_cast<uint16_t>(
        (std::max)(0.0f, (std::min)(65535.0f, track.bpm * 256.0f)));
    const uint16_t lengthBeats = static_cast<uint16_t>(
        (std::max)(0, (std::min)(65535, track.lengthBeats)));
    const uint16_t rowCount = static_cast<uint16_t>(
        (std::min)(static_cast<size_t>(65535), track.rows.size()));
    const uint16_t sceneCount = static_cast<uint16_t>(
        (std::min)(static_cast<size_t>(65535), project.scenes.size()));

    // Header v3 (14 bytes): magic('TKR3'), bpmQ8, lengthBeats, rowCount, sceneCount, transitionSlotCount(6), renderConfig
    appendU16(0x4B54u); // 'TK'
    appendU16(0x3352u); // 'R3'
    appendU16(bpmQ8);
    appendU16(lengthBeats);
    appendU16(rowCount);
    appendU16(sceneCount);
    appendU8(6u);

    const uint8_t compactResolutionPreset = static_cast<uint8_t>(project.fullscreenRenderResolutionPreset);
    const uint8_t compactAspectPreset = static_cast<uint8_t>(project.renderAspectRatioPreset);
    const uint8_t compactRenderConfig = static_cast<uint8_t>((compactResolutionPreset & 0x0Fu) | ((compactAspectPreset & 0x03u) << 4u));
    appendU8(compactRenderConfig);

    for (int i = 0; i < 6; ++i) {
        appendI16(moduleMap.transitionModuleIndices[i]);
    }

    for (uint16_t sceneIndex = 0; sceneIndex < sceneCount; ++sceneIndex) {
        int16_t sceneModule = -1;
        if (sceneIndex < moduleMap.sceneModuleIndices.size()) {
            sceneModule = moduleMap.sceneModuleIndices[sceneIndex];
        }
        appendI16(sceneModule);

        const std::vector<int16_t>* fxModulesPtr = nullptr;
        if (sceneIndex < moduleMap.postFxModuleIndices.size()) {
            fxModulesPtr = &moduleMap.postFxModuleIndices[sceneIndex];
        }
        const size_t fxModuleCount = fxModulesPtr ? fxModulesPtr->size() : 0;
        const uint16_t fxCountU16 = static_cast<uint16_t>((std::min)(static_cast<size_t>(65535), fxModuleCount));
        appendU16(fxCountU16);
        for (uint16_t fxIndex = 0; fxIndex < fxCountU16; ++fxIndex) {
            appendI16((*fxModulesPtr)[fxIndex]);
        }
    }

    for (size_t i = 0; i < rowCount; ++i) {
        const auto& src = track.rows[i];
        const int16_t rowId = static_cast<int16_t>((std::max)(-32768, (std::min)(32767, src.rowId)));
        const int16_t sceneIndex = static_cast<int16_t>((std::max)(-1, (std::min)(32767, src.sceneIndex)));
        const int transitionSlot = TransitionSlotIndexFromStem(src.transitionPresetStem);
        const uint8_t transition = (transitionSlot >= 0 && transitionSlot < static_cast<int>(kTransitionSlotCount))
            ? static_cast<uint8_t>(transitionSlot)
            : static_cast<uint8_t>(255);
        uint8_t flags = 0;
        if (src.stop) flags |= 0x1u;

        const uint8_t transitionDurationQ4 = static_cast<uint8_t>(
            (std::max)(0.0f, (std::min)(255.0f, src.transitionDuration * 16.0f)));
        const int8_t timeOffsetQ4 = static_cast<int8_t>(
            (std::max)(-128.0f, (std::min)(127.0f, src.timeOffset * 16.0f)));
        const int8_t musicIndex = static_cast<int8_t>((std::max)(-1, (std::min)(127, src.musicIndex)));

        // Row v2 (9 bytes): rowId, sceneIndex, transition, flags, transitionDurationQ4, timeOffsetQ4, musicIndex
        appendI16(rowId);
        appendI16(sceneIndex);
        appendU8(transition);
        appendU8(flags);
        appendU8(transitionDurationQ4);
        appendI8(timeOffsetQ4);
        appendI8(musicIndex);
    }

    return true;
}

bool WriteCompactTrackBinary(const ProjectData& project, const fs::path& outputPath, std::string& outError);
bool EmbedCompactTrackIntoExecutable(const ProjectData& project, const fs::path& exePath, std::string& outError);

bool WriteCompactTrackBinary(const DemoTrack& track, const fs::path& outputPath, std::string& outError) {
    ProjectData project;
    project.track = track;
    return WriteCompactTrackBinary(project, outputPath, outError);
}

bool WriteCompactTrackBinary(const ProjectData& project, const fs::path& outputPath, std::string& outError) {
    std::vector<uint8_t> data;
    if (!BuildCompactTrackBinary(project, data)) {
        outError = "Failed to build compact track binary.";
        return false;
    }
    return WriteBinaryFile(outputPath, data, outError);
}

bool EmbedCompactTrackIntoExecutable(const DemoTrack& track, const fs::path& exePath, std::string& outError) {
    ProjectData project;
    project.track = track;
    return EmbedCompactTrackIntoExecutable(project, exePath, outError);
}

bool EmbedCompactTrackIntoExecutable(const ProjectData& project, const fs::path& exePath, std::string& outError) {
    std::vector<uint8_t> payload;
    if (!BuildCompactTrackBinary(project, payload)) {
        outError = "Failed to build compact track payload.";
        return false;
    }

    EmbeddedTrackFooter footer;
    footer.payloadSize = static_cast<uint32_t>(payload.size());

    std::ofstream file(exePath, std::ios::binary | std::ios::app);
    if (!file.is_open()) {
        outError = "Failed to open executable for embedded track write: " + exePath.string();
        return false;
    }

    file.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
    file.write(reinterpret_cast<const char*>(&footer), static_cast<std::streamsize>(sizeof(footer)));
    if (!file.good()) {
        outError = "Failed to append embedded track payload to executable.";
        return false;
    }

    return true;
}

std::string GetTransitionShaderSourceForBuild(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionShaderStem(transitionPresetStem);
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
    return common + R"(
    return lerp(colA, colB, t);
}
)";
}

const char* GetTransitionPackedPath(const std::string& transitionPresetStem) {
    const std::string canonicalStem = CanonicalTransitionShaderStem(transitionPresetStem);
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

const char* GetPackedVertexShaderPath() {
    return "assets/shaders/vertex.cso";
}

std::string MinifyShaderTextForPack(const std::string& input) {
    std::string noComments;
    noComments.reserve(input.size());

    bool inLineComment = false;
    bool inBlockComment = false;
    for (size_t i = 0; i < input.size(); ++i) {
        const char ch = input[i];
        const char next = (i + 1 < input.size()) ? input[i + 1] : '\0';

        if (inLineComment) {
            if (ch == '\n') {
                inLineComment = false;
                noComments.push_back(ch);
            }
            continue;
        }

        if (inBlockComment) {
            if (ch == '*' && next == '/') {
                inBlockComment = false;
                ++i;
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            inLineComment = true;
            ++i;
            continue;
        }

        if (ch == '/' && next == '*') {
            inBlockComment = true;
            ++i;
            continue;
        }

        noComments.push_back(ch);
    }

    std::string out;
    out.reserve(noComments.size());
    bool prevSpace = false;
    for (char ch : noComments) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (std::isspace(uch)) {
            if (!prevSpace) {
                out.push_back(' ');
                prevSpace = true;
            }
            continue;
        }
        out.push_back(ch);
        prevSpace = false;
    }
    return TrimString(out);
}

std::string BuildMicroUbershaderSource(const TinyModuleMap& map) {
    std::string source;
    for (const auto& moduleCode : map.modules) {
        source += moduleCode;
        source.push_back('\n');
    }

    std::ostringstream out;
    out << "U:" << source.size() << '\n';
    out << source;
    return out.str();
}

bool RunCommand(const std::string& command, const std::function<void(const std::string&)>& log);

fs::path GetCleanSolutionDirectoryPath(const BuildRequest& request) {
    fs::path outputPath(request.targetExePath);
    fs::path parent;
    if (!request.cleanSolutionRootPath.empty()) {
        parent = fs::path(request.cleanSolutionRootPath);
    } else {
        parent = outputPath.parent_path();
    }
    if (parent.empty()) {
        parent = fs::path(request.appRoot) / "build_exports";
    }

    std::string stem = outputPath.stem().string();
    if (stem.empty()) {
        stem = "ShaderLabDemo";
    }

    return parent / (stem + "_solution");
}

std::string BuildTimestampTag() {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    char buffer[32] = {};
    sprintf_s(buffer, "%04u%02u%02u_%02u%02u%02u",
        st.wYear, st.wMonth, st.wDay,
        st.wHour, st.wMinute, st.wSecond);
    return std::string(buffer);
}

bool PrepareCleanBuildRoot(const fs::path& root, const std::function<void(const std::string&)>& log, std::string& outError) {
    if (root.empty()) {
        outError = "Build root path is empty.";
        return false;
    }

    fs::path normalizedRoot = root.lexically_normal();
    if (normalizedRoot.empty()) {
        outError = "Build root path is invalid.";
        return false;
    }

    fs::path renameSource = normalizedRoot;
    while (!renameSource.empty() && renameSource.filename().empty()) {
        renameSource = renameSource.parent_path();
    }
    if (renameSource.empty() || renameSource.root_path() == renameSource) {
        outError = "Build root path is invalid for versioning: " + normalizedRoot.string();
        return false;
    }

    std::error_code ec;
    if (fs::exists(renameSource, ec) && !ec) {
        fs::path rolledBase = renameSource.parent_path() /
            (renameSource.filename().string() + "_prev_" + BuildTimestampTag());
        fs::path rolled = rolledBase;
        int suffix = 1;
        while (fs::exists(rolled, ec) && !ec) {
            rolled = renameSource.parent_path() /
                (renameSource.filename().string() + "_prev_" + BuildTimestampTag() + "_" + std::to_string(suffix++));
        }

        fs::rename(renameSource, rolled, ec);
        if (ec) {
            outError = "Failed to version existing build root: " + renameSource.string() + " (" + ec.message() + ")";
            return false;
        }
        log("Existing build root moved to: " + rolled.string());
    }

    ec.clear();
    fs::create_directories(normalizedRoot, ec);
    if (ec) {
        outError = "Failed to create build root: " + normalizedRoot.string();
        return false;
    }
    return true;
}

bool CopyPathRecursive(const fs::path& source, const fs::path& destination, std::string& outError);

bool CreateIsolatedSdkSourceDirectory(
    const BuildRequest& request,
    const fs::path& buildRoot,
    fs::path& outSourceRoot,
    std::string& outError) {
    const fs::path sourceRoot = ResolveStandaloneSourceRoot(fs::path(request.appRoot));
    if (sourceRoot.empty()) {
        outError = "Missing standalone source tree for isolated SDK source (searched from app root): " + request.appRoot;
        return false;
    }

    outSourceRoot = buildRoot / "sdk_source";

    std::error_code ec;
    fs::remove_all(outSourceRoot, ec);
    ec.clear();
    fs::create_directories(outSourceRoot, ec);
    if (ec) {
        outError = "Failed to create isolated SDK source directory: " + outSourceRoot.string();
        return false;
    }

    const fs::path templateCmake = sourceRoot / "templates" / "Standalone_CMakeLists.txt";
    if (!FileExists(templateCmake)) {
        outError = "Missing Standalone CMake template: " + templateCmake.string();
        return false;
    }

    struct CopyEntry {
        fs::path source;
        fs::path destination;
    };

    const std::vector<CopyEntry> entries = {
        {templateCmake, outSourceRoot / "CMakeLists.txt"},
        {sourceRoot / "include", outSourceRoot / "include"},
        {sourceRoot / "src", outSourceRoot / "src"},
        {sourceRoot / "third_party", outSourceRoot / "third_party"}
    };

    for (const auto& entry : entries) {
        if (!CopyPathRecursive(entry.source, entry.destination, outError)) {
            return false;
        }
    }

    return true;
}

bool CopyPathRecursive(const fs::path& source, const fs::path& destination, std::string& outError) {
    std::error_code ec;
    if (!fs::exists(source, ec) || ec) {
        outError = "Missing source path: " + source.string();
        return false;
    }

    if (fs::is_directory(source, ec) && !ec) {
        fs::create_directories(destination, ec);
        if (ec) {
            outError = "Failed to create directory: " + destination.string();
            return false;
        }

        fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec) {
            outError = "Failed to copy directory: " + source.string() + " -> " + destination.string();
            return false;
        }
        return true;
    }

    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        outError = "Failed to create directory: " + destination.parent_path().string();
        return false;
    }

    fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        outError = "Failed to copy file: " + source.string() + " -> " + destination.string();
        return false;
    }
    return true;
}

bool ExportCleanSolutionDirectory(
    const BuildRequest& request,
    const fs::path& packRoot,
    bool useScreenSaver,
    bool useMicroPlayer,
    bool useCrinkler,
    bool staticRuntime,
    fs::path& outSolutionRoot,
    std::string& outError) {
    outSolutionRoot = GetCleanSolutionDirectoryPath(request);

    std::error_code ec;
    fs::remove_all(outSolutionRoot, ec);
    ec.clear();
    fs::create_directories(outSolutionRoot, ec);
    if (ec) {
        outError = "Failed to create clean solution directory: " + outSolutionRoot.string();
        return false;
    }

    const fs::path appRoot(request.appRoot);
    const fs::path sourceRoot = ResolveCleanSolutionSourceRoot(appRoot);
    if (sourceRoot.empty()) {
        outError = "Missing source tree for clean solution export.";
        return false;
    }

    const fs::path templateCmake = sourceRoot / "templates" / "Standalone_CMakeLists.txt";
    const fs::path fallbackCmake = sourceRoot / "CMakeLists.txt";

    if (FileExists(templateCmake)) {
        if (!CopyPathRecursive(templateCmake, outSolutionRoot / "CMakeLists.txt", outError)) {
            return false;
        }
    } else if (FileExists(fallbackCmake)) {
        if (!CopyPathRecursive(fallbackCmake, outSolutionRoot / "CMakeLists.txt", outError)) {
            return false;
        }
    } else {
        outError = "Missing CMake template for clean solution export.";
        return false;
    }

    struct CopyEntry {
        fs::path source;
        fs::path destination;
    };

    const std::vector<CopyEntry> entries = {
        {sourceRoot / "include", outSolutionRoot / "include"},
        {sourceRoot / "src" / "app" / "runtime", outSolutionRoot / "src" / "app" / "runtime"},
        {sourceRoot / "src" / "audio", outSolutionRoot / "src" / "audio"},
        {sourceRoot / "src" / "core", outSolutionRoot / "src" / "core"},
        {sourceRoot / "src" / "graphics", outSolutionRoot / "src" / "graphics"},
        {sourceRoot / "src" / "shader", outSolutionRoot / "src" / "shader"},
        {sourceRoot / "third_party", outSolutionRoot / "third_party"},
        {packRoot / "project.json", outSolutionRoot / "project.json"},
        {packRoot / "assets", outSolutionRoot / "assets"}
    };

    for (const auto& entry : entries) {
        if (!CopyPathRecursive(entry.source, entry.destination, outError)) {
            return false;
        }
    }

    ProjectData solutionProject;
    if (!Serializer::LoadProject(request.projectPath, solutionProject)) {
        if (!Serializer::LoadProject((outSolutionRoot / "project.json").string(), solutionProject)) {
            outError = "Failed to load project data for clean solution export.";
            return false;
        }
    }

    if (!Serializer::ConsolidateProject(solutionProject, outSolutionRoot.string())) {
        outError = "Failed to consolidate clean solution project assets.";
        return false;
    }

    const fs::path shaderSourceDir = outSolutionRoot / "assets" / "shaders" / "hlsl";
    fs::create_directories(shaderSourceDir, ec);
    if (ec) {
        outError = "Failed to create shader source directory: " + shaderSourceDir.string();
        return false;
    }

    for (size_t sceneIndex = 0; sceneIndex < solutionProject.scenes.size(); ++sceneIndex) {
        auto& scene = solutionProject.scenes[sceneIndex];
        const std::string sceneSourceRel = "assets/shaders/hlsl/scene_" + std::to_string(sceneIndex) + ".hlsl";
        if (!WriteTextFile(outSolutionRoot / sceneSourceRel, scene.shaderCode, outError)) {
            return false;
        }
        scene.shaderCodePath = sceneSourceRel;
        scene.shaderCode = "@file:" + sceneSourceRel;

        for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
            auto& fx = scene.postFxChain[fxIndex];
            const std::string fxSourceRel = "assets/shaders/hlsl/scene_" + std::to_string(sceneIndex) + "_fx_" + std::to_string(fxIndex) + ".hlsl";
            if (!WriteTextFile(outSolutionRoot / fxSourceRel, fx.shaderCode, outError)) {
                return false;
            }
            fx.shaderCodePath = fxSourceRel;
            fx.shaderCode = "@file:" + fxSourceRel;
        }
    }

    if (!Serializer::SaveProject(solutionProject, (outSolutionRoot / "project.json").string())) {
        outError = "Failed to write linked-shader clean solution project.json.";
        return false;
    }

    std::ofstream readme(outSolutionRoot / "README_SOLUTION.txt", std::ios::binary);
    if (!readme.is_open()) {
        outError = "Failed to create README_SOLUTION.txt in clean solution directory.";
        return false;
    }

    readme << "ShaderLab Clean Solution Directory\n";
    readme << "===============================\n\n";
    readme << "This folder is regenerated on each build/export to keep a clean, reproducible workspace.\n";
    readme << "It contains source, third_party dependencies, and packed project assets for standalone builds.\n\n";
    readme << "Suggested configure command (PowerShell in this folder):\n";
    readme << "  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release";
    if (useMicroPlayer) {
        readme << " -DSHADERLAB_BUILD_MICRO_PLAYER=ON -DSHADERLAB_BUILD_RUNTIME=OFF -DSHADERLAB_TINY_PLAYER=ON";
    } else {
        readme << " -DSHADERLAB_BUILD_MICRO_PLAYER=OFF -DSHADERLAB_BUILD_RUNTIME=ON -DSHADERLAB_TINY_PLAYER=OFF";
    }
    if (useCrinkler) {
        readme << " -DSHADERLAB_USE_CRINKLER=ON";
    }
    if (staticRuntime) {
        readme << " -DSHADERLAB_STATIC_RUNTIME=ON";
    }
    readme << " -DSHADERLAB_RUNTIME_IMGUI=OFF";
    readme << "\n\n";
    readme << "Then build:\n";
    if (useScreenSaver) {
        readme << "  cmake --build build --clean-first --target ShaderLabScreenSaver --config Release\n";
    } else if (useMicroPlayer) {
        readme << "  cmake --build build --clean-first --target ShaderLabMicroPlayer --config Release\n";
    } else {
        readme << "  cmake --build build --clean-first --target ShaderLabPlayer --config Release\n";
    }

    return true;
}

std::string EscapePowerShellLiteral(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        if (ch == '\'') {
            escaped += "''";
        } else {
            escaped.push_back(ch);
        }
    }
    return escaped;
}

bool CreatePackagedZip(
    const fs::path& packageDir,
    const fs::path& zipPath,
    const std::function<void(const std::string&)>& log,
    std::string& outError) {
    std::error_code ec;
    fs::create_directories(zipPath.parent_path(), ec);
    if (ec) {
        outError = "Failed to create zip output directory: " + zipPath.parent_path().string();
        return false;
    }

    if (fs::exists(zipPath, ec) && !ec) {
        fs::remove(zipPath, ec);
        if (ec) {
            outError = "Failed to replace existing zip: " + zipPath.string();
            return false;
        }
    }

    const std::string packageGlob = (packageDir / "*").string();
    const std::string psCommand =
        "powershell -NoProfile -ExecutionPolicy Bypass -Command \"Compress-Archive -Path '" +
        EscapePowerShellLiteral(packageGlob) +
        "' -DestinationPath '" +
        EscapePowerShellLiteral(zipPath.string()) +
        "' -Force\"";

    if (!RunCommand(psCommand, log)) {
        outError = "Compress-Archive failed for packaged demo zip.";
        return false;
    }

    if (!fs::exists(zipPath)) {
        outError = "Zip archive was not created: " + zipPath.string();
        return false;
    }

    return true;
}

bool RunCommand(const std::string& command, const std::function<void(const std::string&)>& log) {
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = NULL;
    HANDLE writePipe = NULL;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        log("Error: Failed to create pipe for command.");
        return false;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.dwFlags |= STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi = {};
    std::string cmdLine;
    fs::path tempBatchPath;

    const bool useBatchFile = true;
    if (useBatchFile) {
        std::error_code ec;
        tempBatchPath = fs::temp_directory_path(ec) / ("shaderlab_cmd_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(GetTickCount64()) + ".bat");
        std::ofstream batch(tempBatchPath, std::ios::binary);
        if (!batch.is_open()) {
            CloseHandle(readPipe);
            CloseHandle(writePipe);
            log("Error: Failed to create temporary command script.");
            return false;
        }
        batch << "@echo off\r\n";
        std::string normalized;
        normalized.reserve(command.size() + 16);
        for (size_t i = 0; i < command.size(); ++i) {
            const char c = command[i];
            if (c == '\r') {
                if (i + 1 < command.size() && command[i + 1] == '\n') {
                    ++i;
                }
                normalized += "\r\n";
            } else if (c == '\n') {
                normalized += "\r\n";
            } else {
                normalized.push_back(c);
            }
        }
        if (normalized.empty() || (normalized.size() < 2 || normalized.substr(normalized.size() - 2) != "\r\n")) {
            normalized += "\r\n";
        }
        batch << normalized;
        batch.close();
        cmdLine = "cmd.exe /C \"" + tempBatchPath.string() + "\"";
    } else {
        cmdLine = "cmd.exe /C " + command;
    }

    BOOL created = CreateProcessA(
        NULL,
        cmdLine.data(),
        NULL,
        NULL,
        TRUE,
        CREATE_NO_WINDOW,
        NULL,
        NULL,
        &si,
        &pi
    );

    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        if (!tempBatchPath.empty()) {
            std::error_code ec;
            fs::remove(tempBatchPath, ec);
        }
        log("Error: Failed to launch build command.");
        return false;
    }

    const DWORD heartbeatMs = 15000;
    ULONGLONG lastOutputTick = GetTickCount64();

    char buffer[512];
    auto readAvailableOutput = [&]() {
        DWORD available = 0;
        if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
            return;
        }

        while (available > 0) {
            DWORD bytesRead = 0;
            const DWORD toRead = (std::min)(available, static_cast<DWORD>(sizeof(buffer) - 1));
            if (!ReadFile(readPipe, buffer, toRead, &bytesRead, nullptr) || bytesRead == 0) {
                break;
            }
            buffer[bytesRead] = '\0';
            log(std::string(buffer));
            lastOutputTick = GetTickCount64();

            if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr)) {
                break;
            }
        }
    };

    while (true) {
        const DWORD waitResult = WaitForSingleObject(pi.hProcess, 100);
        readAvailableOutput();

        if (waitResult == WAIT_OBJECT_0) {
            break;
        }

        const ULONGLONG nowTick = GetTickCount64();
        if (nowTick - lastOutputTick >= heartbeatMs) {
            log("...build still running (long step in progress)...");
            lastOutputTick = nowTick;
        }
    }

    readAvailableOutput();

    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    if (!tempBatchPath.empty()) {
        std::error_code ec;
        fs::remove(tempBatchPath, ec);
    }

    return exitCode == 0;
}
} // namespace

BuildPrereqReport BuildPipeline::CheckPrereqs(const std::string& appRoot, BuildMode mode) {
    BuildPrereqReport report{};
    const fs::path effectiveAppRoot = ResolveBuildAppRoot(fs::path(appRoot));
    std::vector<std::string> missing;
    std::vector<std::string> guidance;
    std::vector<std::string> optionalMissing;

    std::string vcvarsPath;
    report.hasVisualStudioTools = FindVcVars(vcvarsPath);
    if (!report.hasVisualStudioTools) {
        missing.push_back("Visual Studio C++ Build Tools (vcvars64.bat)");
        guidance.push_back("Install Visual Studio 2022 with 'Desktop development with C++'.");
    }

    std::string sdkPath;
    report.hasWindowsSdk = HasWindowsSdk(sdkPath) || HasBundledWindowsSdk(effectiveAppRoot, sdkPath);
    if (!report.hasWindowsSdk) {
        missing.push_back("Windows SDK 10 (d3d12.h)");
        guidance.push_back("Install Windows 10/11 SDK via Visual Studio Installer.");
        guidance.push_back("Or create a local SDK bundle in third_party/windows_sdk_bundle (tools/bundle_windows_sdk.ps1).");
    }

    report.hasCMake = FindOnPath("cmake.exe") || FileExists("C:\\Program Files\\CMake\\bin\\cmake.exe");
    if (!report.hasCMake) {
        missing.push_back("CMake");
        guidance.push_back("Install CMake from https://cmake.org/download/ and add it to PATH.");
    }

    report.hasDxcRuntime = HasDxcRuntime(effectiveAppRoot);
    if (!report.hasDxcRuntime) {
        missing.push_back("DXC (dxcompiler.dll)");
        guidance.push_back("Install DirectX Shader Compiler or place dxcompiler.dll next to the editor executable.");
    }

    std::string crinklerPath;
    report.hasCrinkler = ResolveCrinklerPath(effectiveAppRoot, crinklerPath);
    report.hasNinja = FindOnPath("ninja.exe");
    if (mode == BuildMode::ReleaseCrinkled) {
        if (!report.hasCrinkler) {
            missing.push_back("Crinkler (crinkler.exe)");
            guidance.push_back("See README-CRINKLER.txt for setup instructions.");
            guidance.push_back("Set SHADERLAB_CRINKLER or CRINKLER_PATH to crinkler.exe (or its folder).");
        }
        if (!report.hasNinja) {
            missing.push_back("Ninja (ninja.exe)");
            guidance.push_back("Install Ninja and ensure ninja.exe is on PATH.");
        }
    } else if (!report.hasCrinkler) {
        optionalMissing.push_back("Crinkler (optional, for size-optimized builds)");
        guidance.push_back("Set SHADERLAB_CRINKLER or CRINKLER_PATH to the Crinkler folder or crinkler.exe.");
    }

    std::ostringstream message;
    if (!missing.empty()) {
        message << "Missing build prerequisites:\n";
        for (const auto& item : missing) {
            message << " - " << item << "\n";
        }

        if (!guidance.empty()) {
            message << "\nSetup guidance:\n";
            for (const auto& item : guidance) {
                message << " - " << item << "\n";
            }
            message << "\nAfter installing, restart the editor and try again.";
        }

        report.ok = false;
        report.message = message.str();
        return report;
    }

    if (!optionalMissing.empty()) {
        message << "Optional tools not found:\n";
        for (const auto& item : optionalMissing) {
            message << " - " << item << "\n";
        }
        if (!guidance.empty()) {
            message << "\nSetup guidance:\n";
            for (const auto& item : guidance) {
                message << " - " << item << "\n";
            }
        }
        message << "\nYou can still build, but output may be larger.";
        report.ok = true;
        report.message = message.str();
        return report;
    }

    report.ok = true;
    return report;
}

std::vector<MicroUbershaderConflict> BuildPipeline::AnalyzeMicroUbershaderConflicts(const std::string& projectPath) {
    std::vector<MicroUbershaderConflict> result;

    ProjectData project;
    if (!Serializer::LoadProject(projectPath, project)) {
        return result;
    }

    const TinyModuleMap map = BuildTinyModuleMap(project, false);
    const auto grouped = BuildMicroConflictBindings(map);
    for (const auto& [signatureKey, bindings] : grouped) {
        if (bindings.size() < 2) {
            continue;
        }

        MicroUbershaderConflict conflict;
        conflict.signatureKey = signatureKey;
        conflict.signatureDisplay = bindings.front().signature;
        conflict.options.reserve(bindings.size());

        for (const auto& binding : bindings) {
            MicroUbershaderConflictOption option;
            option.moduleEntrypoint = binding.moduleEntrypoint;
            option.moduleLabel = binding.moduleLabel;
            option.signature = binding.signature;
            option.snippet = binding.snippet;
            conflict.options.push_back(std::move(option));
        }

        result.push_back(std::move(conflict));
    }

    std::sort(result.begin(), result.end(), [](const MicroUbershaderConflict& a, const MicroUbershaderConflict& b) {
        return a.signatureDisplay < b.signatureDisplay;
    });

    return result;
}

bool BuildPipeline::GenerateMicroUbershaderSource(const std::string& projectPath,
                                                  std::string& outSource,
                                                  std::string& outError) {
    outSource.clear();
    outError.clear();

    if (projectPath.empty()) {
        outError = "Project path is empty.";
        return false;
    }

    ProjectData project;
    if (!Serializer::LoadProject(projectPath, project)) {
        outError = "Failed to load project: " + projectPath;
        return false;
    }

    const TinyModuleMap map = BuildTinyModuleMap(project, false);
    outSource = BuildMicroUbershaderSource(map);
    if (outSource.empty()) {
        outError = "Generated ubershader source is empty.";
        return false;
    }

    return true;
}

BuildResult BuildPipeline::BuildSelfContained(
    const BuildRequest& request,
    const std::function<void(const std::string&)>& log) {
    BuildResult result{};
    BuildRequest resolvedRequest = request;
    const fs::path requestedAppRoot(request.appRoot);
    const fs::path effectiveAppRoot = ResolveBuildAppRoot(requestedAppRoot);
    resolvedRequest.appRoot = effectiveAppRoot.string();

    const BuildTargetKind targetKind = request.targetKind;
    const bool budgetedBuild = IsTinySizeTarget(request.sizeTarget);
    const bool isPackagedDemo = targetKind == BuildTargetKind::PackagedDemo;
    const bool requestedScreenSaver = targetKind == BuildTargetKind::SelfContainedScreenSaver;
    const bool useMicroPlayer = targetKind == BuildTargetKind::MicroDemo || budgetedBuild;
    const bool useScreenSaver = requestedScreenSaver && !useMicroPlayer;
    const bool tinyProfile = useMicroPlayer || budgetedBuild;
    const bool microDeveloperBuild = useMicroPlayer && request.microDeveloperBuild;
    bool staticRuntime = !tinyProfile;

    if (request.cleanSolutionRootPath.empty()) {
        log("Error: Clean build root is required. Set 'Clean Solution Root' in Build Settings.");
        return result;
    }

    fs::path buildRootPath = fs::path(request.cleanSolutionRootPath);
    std::string cleanRootError;
    if (!PrepareCleanBuildRoot(buildRootPath, log, cleanRootError)) {
        log("Error: " + cleanRootError);
        return result;
    }
    log("[0/6] Build root prepared: " + buildRootPath.string());

    log(std::string("Build Target: ") + BuildTargetName(targetKind));
    log(std::string("Build Mode: ") + BuildModeName(request.mode));
    log(std::string("Size Target: ") + SizePresetName(request.sizeTarget));
    if (budgetedBuild && targetKind != BuildTargetKind::MicroDemo) {
        log("Budgeted build detected; forcing MicroPlayer runtime path.");
    }
    if (!request.appRoot.empty() && requestedAppRoot != effectiveAppRoot) {
        log("Application Root (requested): " + request.appRoot);
        log("Application Root (resolved): " + resolvedRequest.appRoot);
    } else {
        log("Application Root: " + resolvedRequest.appRoot);
    }
    log("Project Path: " + request.projectPath);
    log("Target Output: " + request.targetExePath);

    fs::path sourceDir = effectiveAppRoot / "dev_kit";
    const bool requireMicroSources = useMicroPlayer;
    bool isDevKit = HasDevKitSourceTree(sourceDir, requireMicroSources);

    if (isDevKit) {
        log("Using Dev Kit source: " + sourceDir.string());
    } else {
        if (fs::exists(sourceDir)) {
            if (requireMicroSources && !FileExists(sourceDir / "src" / "app" / "tiny")) {
                log("Dev Kit source missing micro runtime subtree (src/app/tiny): " + sourceDir.string());
            } else {
                log("Dev Kit source is incomplete: " + sourceDir.string());
            }
        } else {
            log("Dev Kit not found at " + (effectiveAppRoot / "dev_kit").string());
        }
        std::string sdkError;
        if (!CreateIsolatedSdkSourceDirectory(resolvedRequest, buildRootPath, sourceDir, sdkError)) {
            log("Error: Failed to create isolated SDK source directory.");
            log("Details: " + sdkError);
            return result;
        }
        log("Using isolated SDK source: " + sourceDir.string());
    }

    BundledWindowsSdkInfo bundledSdk;
    if (!ResolveBundledWindowsSdk(sourceDir, bundledSdk)) {
        ResolveBundledWindowsSdk(effectiveAppRoot, bundledSdk);
    }
    if (bundledSdk.found) {
        log("Windows SDK bundle: " + bundledSdk.root.string() + " (version " + bundledSdk.versionTag + ")");
    }

    const bool hasNinja = FindOnPath("ninja.exe");
    std::string ninjaExePath;
    if (hasNinja) {
        FindOnPathFull("ninja.exe", ninjaExePath);
    }
    std::string cmakeExePath;
    if (!FindOnPathFull("cmake.exe", cmakeExePath)) {
        fs::path defaultCmake = "C:\\Program Files\\CMake\\bin\\cmake.exe";
        if (FileExists(defaultCmake)) {
            cmakeExePath = defaultCmake.string();
        } else {
            cmakeExePath = "cmake";
        }
    }
    std::string crinklerPath;
    const bool hasCrinkler = ResolveCrinklerPath(effectiveAppRoot, crinklerPath);
    const bool wantCrinkler = request.mode == BuildMode::ReleaseCrinkled;
    bool useCrinkler = wantCrinkler && useMicroPlayer && hasNinja && hasCrinkler;
    bool useX86Target = useMicroPlayer;
    const bool useNinjaGenerator = useCrinkler;

    if (wantCrinkler && !useMicroPlayer) {
        log("Crinkled mode requested for non-micro runtime target.");
        log("Crinkler polish is skipped for compatibility; using standard release linker.");
    }

    if (useCrinkler && useX86Target) {
        fs::path currentCrinkler = fs::path(crinklerPath);
        fs::path parentDir = currentCrinkler.parent_path();
        std::string parentName = parentDir.filename().string();
        std::transform(parentName.begin(), parentName.end(), parentName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (parentName == "win64") {
            fs::path win32Candidate = parentDir.parent_path() / "Win32" / currentCrinkler.filename();
            if (FileExists(win32Candidate)) {
                crinklerPath = win32Candidate.string();
                log("Crinkler: switched to Win32 binary for x86 target (" + crinklerPath + ")");
            }
        }
    }

    if (tinyProfile) {
        log("Budget Runtime Profile: ON (64K-1024K budget target)");
        log("Budget Runtime CRT: dynamic CRT (/MD) for compatibility and size");
    } else {
        log("Budget Runtime Profile: OFF (full runtime profile)");
    }

    log(std::string("Target Architecture: ") + (useX86Target ? "x86" : "x64"));

    if (useMicroPlayer) {
        if (microDeveloperBuild) {
            log("MicroPlayer developer mode: ON (overlay diagnostics enabled)");
        }
        if (useCrinkler) {
            log("MicroPlayer mode: ON (budget runtime path, ShaderLabMicroPlayer + Crinkler)");
        } else {
            log("MicroPlayer mode: ON (budget runtime path, ShaderLabMicroPlayer)");
        }
    } else {
        log("MicroPlayer mode: OFF (full runtime path)");
    }

    if (useScreenSaver) {
        log("Runtime wrapper: ShaderLabScreenSaver (.scr contract)");
    }

    if (wantCrinkler && useMicroPlayer && !useCrinkler) {
        if (!hasCrinkler) {
            log("Error: Release Crinkled requires Crinkler, but crinkler.exe was not found.");
        }
        if (!hasNinja) {
            log("Error: Release Crinkled currently requires Ninja (ninja.exe on PATH).");
        }
        log("See README-CRINKLER.txt for setup instructions.");
        return result;
    }

    if (useCrinkler) {
        log("Crinkler: enabled (" + crinklerPath + ")");
        if (PathContainsTokenCaseInsensitive(fs::path(crinklerPath), "\\dev_kit\\third_party\\") ||
            PathContainsTokenCaseInsensitive(fs::path(crinklerPath), "\\third_party\\")) {
            log("Crinkler source: bundled with ShaderLab Dev Kit/repo (preferred).");
        }
    } else if (hasCrinkler && !hasNinja) {
        log("Crinkler detected but Ninja not found; skipping Crinkler.");
    } else {
        log("Crinkler: disabled (standard release linker)");
    }
    fs::path buildDir;
    if (useNinjaGenerator) {
        if (useMicroPlayer && useCrinkler) {
            buildDir = buildRootPath / "build_selfcontained_ninja_crinkled_micro_x86";
        } else if (useMicroPlayer) {
            buildDir = buildRootPath / (microDeveloperBuild ? "build_selfcontained_ninja_release_micro_dev_x86" : "build_selfcontained_ninja_release_micro_x86");
        } else {
            buildDir = buildRootPath /
                (useCrinkler ? "build_selfcontained_ninja_crinkled" : "build_selfcontained_ninja_release");
        }
    } else {
        if (useMicroPlayer) {
            buildDir = buildRootPath /
                (useCrinkler
                    ? "build_selfcontained_vs2022_crinkled_micro_x86"
                    : (microDeveloperBuild ? "build_selfcontained_vs2022_release_micro_dev_x86" : "build_selfcontained_vs2022_release_micro_x86"));
        } else {
            buildDir = buildRootPath /
                (useCrinkler ? "build_selfcontained_vs2022_crinkled" : "build_selfcontained_vs2022_release");
        }
    }
    std::error_code buildDirEc;
    fs::create_directories(buildDir, buildDirEc);

    fs::path cmakeCachePath = buildDir / "shaderlab_build_config.cmake";
    auto writeInitialCache = [&](bool enableCrinkler) -> bool {
        std::ofstream cmakeCacheFile(cmakeCachePath, std::ios::binary);
        if (!cmakeCacheFile.is_open()) {
            return false;
        }

        auto writeCacheBool = [&](const char* key, bool value) {
            cmakeCacheFile << "set(" << key << " " << (value ? "ON" : "OFF") << " CACHE BOOL \"\" FORCE)\n";
        };
        auto writeCacheString = [&](const char* key, const std::string& value) {
            std::string escaped = value;
            std::replace(escaped.begin(), escaped.end(), '\\', '/');
            cmakeCacheFile << "set(" << key << " \"" << escaped << "\" CACHE STRING \"\" FORCE)\n";
        };

        writeCacheBool("SHADERLAB_BUILD_RUNTIME", !useMicroPlayer);
        writeCacheBool("SHADERLAB_BUILD_EDITOR", false);
        writeCacheBool("SHADERLAB_BUILD_MICRO_PLAYER", useMicroPlayer);
        writeCacheBool("SHADERLAB_ENABLE_DXC", false);
        const bool tinyDevOverlay = tinyProfile && microDeveloperBuild;
        const bool enableRuntimeDebugUi = false;
        const bool enableRuntimeDebugLog = microDeveloperBuild || request.runtimeDebugLog;
        writeCacheBool("SHADERLAB_RUNTIME_IMGUI", enableRuntimeDebugUi);
        writeCacheBool("SHADERLAB_RUNTIME_DEBUG_LOG", enableRuntimeDebugLog);
        writeCacheBool("SHADERLAB_TINY_DEV_OVERLAY", tinyDevOverlay);
        writeCacheBool("SHADERLAB_COMPACT_TRACK_DEBUG", request.compactTrackDebugLog);
        const bool tinyRuntimeCompile = false;
        writeCacheBool("SHADERLAB_TINY_PLAYER", tinyProfile);
        writeCacheBool("SHADERLAB_TINY_RUNTIME_COMPILE", tinyRuntimeCompile);
        writeCacheBool("SHADERLAB_TINY_TRACE", tinyProfile && (microDeveloperBuild || request.runtimeDebugLog));
        writeCacheBool("SHADERLAB_STATIC_RUNTIME", staticRuntime);
        writeCacheBool("SHADERLAB_USE_CRINKLER", enableCrinkler);
        writeCacheBool("SHADERLAB_CRINKLER_TINYIMPORT", enableCrinkler && useMicroPlayer);

        if (enableCrinkler && useMicroPlayer) {
            writeCacheString("SHADERLAB_CRINKLER_COMPMODE", "SLOW");
            writeCacheString("SHADERLAB_CRINKLER_HASH_TRIES", "32");
            writeCacheString("SHADERLAB_CRINKLER_ORDER_TRIES", "120");
        }

        if (enableCrinkler) {
            writeCacheString("CRINKLER_PATH", crinklerPath);
            writeCacheString("CMAKE_LINKER", crinklerPath);
            writeCacheString("CMAKE_TRY_COMPILE_TARGET_TYPE", "STATIC_LIBRARY");
            writeCacheString("CMAKE_EXE_LINKER_FLAGS", "/MANIFEST:NO");
        } else {
            writeCacheString("CRINKLER_PATH", "");
            writeCacheString("CMAKE_LINKER", "link.exe");
            writeCacheString("CMAKE_EXE_LINKER_FLAGS", "");
        }

        if (useNinjaGenerator && !ninjaExePath.empty()) {
            writeCacheString("CMAKE_MAKE_PROGRAM", ninjaExePath);
        }

        cmakeCacheFile.close();
        return cmakeCacheFile.good();
    };

    if (!writeInitialCache(useCrinkler)) {
        log("Error: Failed to create CMake initial cache file: " + cmakeCachePath.string());
        return result;
    }

    std::string cmakeCmd = "\"" + cmakeExePath + "\"";
    std::string cmd = cmakeCmd + " -S \"" + sourceDir.string() + "\" -B \"" + buildDir.string() + "\" -C \"" + cmakeCachePath.string() + "\"";
    if (useNinjaGenerator) {
        cmd += " -G Ninja -DCMAKE_BUILD_TYPE=Release";
    } else {
        cmd += useX86Target ? " -G \"Visual Studio 17 2022\" -A Win32" : " -G \"Visual Studio 17 2022\" -A x64";
    }

    std::string vcvars;
    std::string vcvarsFallback;
    bool canUseVcvarsFallback = false;
    bool usingVcvarsFallback = false;
    std::string vcvarsBaseArgs;
    std::string vcvarsLabel;
    const bool preferX86Vcvars = useX86Target;
    bool foundVcvars = false;
    const bool requireVcvars = useNinjaGenerator;
    if (requireVcvars) {
        if (preferX86Vcvars) {
            foundVcvars = FindVcVars32(vcvars);
            canUseVcvarsFallback = FindVcVarsAll(vcvarsFallback);
            if (foundVcvars) {
                vcvarsLabel = "vcvars32";
                log("Using x86 MSVC environment (vcvars32).");
            } else if (canUseVcvarsFallback) {
                vcvars = vcvarsFallback;
                vcvarsBaseArgs = " x86";
                foundVcvars = true;
                vcvarsLabel = "vcvarsall x86";
                log("vcvars32.bat not found, falling back to vcvarsall x86.");
            }
        } else {
            foundVcvars = FindVcVars(vcvars);
            if (foundVcvars) {
                vcvarsLabel = "vcvars64";
            }
        }
    } else {
        foundVcvars = true;
    }

    if (!foundVcvars) {
        if (preferX86Vcvars) {
            log("Error: neither vcvars32.bat nor vcvarsall.bat were found. Install Visual Studio Build Tools 2022.");
        } else {
            log("Error: vcvars64.bat not found. Install Visual Studio Build Tools 2022.");
        }
        return result;
    }

    std::string vcvarsArgs;
    if (useCrinkler && HasMsvcToolsetPrefix(vcvars, "14.29")) {
        vcvarsArgs = " -vcvars_ver=14.29";
        log("Crinkler: using MSVC v142 toolset (14.29)");
    } else if (useCrinkler) {
        if (preferX86Vcvars) {
            log("Crinkler: using default x86 toolset with vcvars32.");
        } else {
            log("Crinkler: MSVC v142 toolset not found; using default toolset");
        }
    }

    if (!vcvarsArgs.empty()) {
        std::string scriptName = fs::path(vcvars).filename().string();
        std::transform(scriptName.begin(), scriptName.end(), scriptName.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (scriptName != "vcvarsall.bat") {
            std::string vcvarsAll;
            if (FindVcVarsAll(vcvarsAll)) {
                vcvars = vcvarsAll;
                vcvarsBaseArgs = preferX86Vcvars ? " x86" : " x64";
                vcvarsLabel = preferX86Vcvars ? "vcvarsall x86" : "vcvarsall x64";
                log("Crinkler: switching to " + vcvarsLabel + " for toolset pinning.");
            } else {
                log("Warning: vcvarsall.bat not found; pinned toolset may fail with current vcvars script.");
            }
        }
    }

    if (!vcvarsLabel.empty()) {
        log("MSVC environment: " + vcvarsLabel);
    } else if (!requireVcvars) {
        log("MSVC environment: Visual Studio generator (no vcvars bootstrap)");
    }

    auto WrapWithVcVars = [&](const std::string& innerCmd, const std::string& args) {
        if (!requireVcvars || vcvars.empty()) {
            return innerCmd;
        }

        auto normalizeForCmd = [](std::string value) {
            std::replace(value.begin(), value.end(), '/', '\\');
            return value;
        };

        auto appendEnvList = [&](std::string& script, const char* envName, const std::vector<fs::path>& paths) {
            std::vector<std::string> existing;
            for (const auto& path : paths) {
                if (!FileExists(path)) {
                    continue;
                }
                existing.push_back(normalizeForCmd(path.string()));
            }
            if (existing.empty()) {
                return;
            }

            std::string merged;
            for (size_t i = 0; i < existing.size(); ++i) {
                if (i > 0) {
                    merged += ';';
                }
                merged += existing[i];
            }
            merged += ";%";
            merged += envName;
            merged += "%";

            script += "set \"";
            script += envName;
            script += "=";
            script += merged;
            script += "\"\n";
        };

        std::string script;
        script += "set \"PATH=C:\\Windows\\System32;C:\\Windows\\System32\\downlevel;C:\\Windows;C:\\Windows\\System32\\Wbem;C:\\Windows\\System32\\WindowsPowerShell\\v1.0\"\n";
        script += "call \"" + vcvars + "\"" + args + " >nul\n";
        script += "if errorlevel 1 exit /b %errorlevel%\n";

        if (bundledSdk.found) {
            const std::string arch = useX86Target ? "x86" : "x64";
            const fs::path sdkDir = bundledSdk.root;

            std::string sdkDirValue = normalizeForCmd(sdkDir.string());
            if (!sdkDirValue.empty() && sdkDirValue.back() != '\\') {
                sdkDirValue.push_back('\\');
            }
            script += "set \"WindowsSdkDir=" + sdkDirValue + "\"\n";
            script += "set \"WindowsSDKVersion=" + bundledSdk.versionTag + "\\\"\n";

            appendEnvList(script, "INCLUDE", {
                bundledSdk.includeVersionDir / "ucrt",
                bundledSdk.includeVersionDir / "shared",
                bundledSdk.includeVersionDir / "um",
                bundledSdk.includeVersionDir / "winrt",
                bundledSdk.includeVersionDir / "cppwinrt"
            });

            appendEnvList(script, "LIB", {
                bundledSdk.libVersionDir / "ucrt" / arch,
                bundledSdk.libVersionDir / "um" / arch
            });

            appendEnvList(script, "PATH", {
                bundledSdk.binVersionDir / arch
            });
        }

        script += innerCmd + "\n";
        return script;
    };

    std::string activeVcvarsArgs = vcvarsBaseArgs + vcvarsArgs;
    const bool hasPinnedToolset = !vcvarsArgs.empty();
    std::string cmdWithEnv = WrapWithVcVars(cmd, activeVcvarsArgs);

    log("----------------------------------------");
    log("[1/6] Configure CMake");
    log("Command: " + cmdWithEnv);

    if (!RunCommand(cmdWithEnv, log)) {
        if (hasPinnedToolset) {
            log("CMake configure failed with pinned MSVC toolset; retrying with default toolset.");
            activeVcvarsArgs = vcvarsBaseArgs;
            cmdWithEnv = WrapWithVcVars(cmd, activeVcvarsArgs);
            log("Retry Command: " + cmdWithEnv);
            if (!RunCommand(cmdWithEnv, log)) {
                if (preferX86Vcvars && !usingVcvarsFallback && canUseVcvarsFallback) {
                    log("vcvars32 configure failed; retrying with vcvarsall.bat x86.");
                    vcvars = vcvarsFallback;
                    usingVcvarsFallback = true;
                    vcvarsBaseArgs = " x86";
                    vcvarsLabel = "vcvarsall x86";

                    std::string fallbackArgs;
                    if (useCrinkler && HasMsvcToolsetPrefix(vcvars, "14.29")) {
                        fallbackArgs = " -vcvars_ver=14.29";
                    }
                    activeVcvarsArgs = vcvarsBaseArgs + fallbackArgs;
                    cmdWithEnv = WrapWithVcVars(cmd, activeVcvarsArgs);
                    log("Fallback Command: " + cmdWithEnv);
                    if (!RunCommand(cmdWithEnv, log)) {
                        log("CMake Configuration Failed.");
                        return result;
                    }
                    log("CMake configure succeeded with vcvarsall.bat x86 fallback.");
                    log("MSVC environment: " + vcvarsLabel);
                } else {
                    log("CMake Configuration Failed.");
                    return result;
                }
            }
            log("CMake configure succeeded with default MSVC toolset.");
        } else {
            if (preferX86Vcvars && !usingVcvarsFallback && canUseVcvarsFallback) {
                log("vcvars32 configure failed; retrying with vcvarsall.bat x86.");
                vcvars = vcvarsFallback;
                usingVcvarsFallback = true;
                vcvarsBaseArgs = " x86";
                vcvarsLabel = "vcvarsall x86";
                std::string fallbackArgs;
                if (useCrinkler && HasMsvcToolsetPrefix(vcvars, "14.29")) {
                    fallbackArgs = " -vcvars_ver=14.29";
                }
                activeVcvarsArgs = vcvarsBaseArgs + fallbackArgs;
                cmdWithEnv = WrapWithVcVars(cmd, activeVcvarsArgs);
                log("Fallback Command: " + cmdWithEnv);
                if (!RunCommand(cmdWithEnv, log)) {
                    log("CMake Configuration Failed.");
                    return result;
                }
                log("CMake configure succeeded with vcvarsall.bat x86 fallback.");
                log("MSVC environment: " + vcvarsLabel);
            } else {
                log("CMake Configuration Failed.");
                return result;
            }
        }
    }

    log("----------------------------------------");
    log("[2/6] Build runtime target");
    const std::string targetName = useScreenSaver ? "ShaderLabScreenSaver" : (useMicroPlayer ? "ShaderLabMicroPlayer" : "ShaderLabPlayer");
    const std::string targetExtension = useScreenSaver ? ".scr" : ".exe";
    std::string buildCmd = cmakeCmd + " --build \"" + buildDir.string() + "\" --clean-first --target " + targetName + " --config Release";
    std::string buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
    log("Command: " + buildCmdWithEnv);

    bool releaseBuildOk = RunCommand(buildCmdWithEnv, log);
    if (!releaseBuildOk && hasPinnedToolset) {
        log("Build failed with pinned MSVC toolset; retrying with default toolset environment.");
        activeVcvarsArgs = vcvarsBaseArgs;
        buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
        log("Retry Build Command: " + buildCmdWithEnv);
        releaseBuildOk = RunCommand(buildCmdWithEnv, log);
        if (releaseBuildOk) {
            log("Build succeeded with default MSVC toolset environment.");
        }
    }

    if (!releaseBuildOk && useCrinkler) {
        auto resolveBundledCrinklerCandidate = [&]() -> std::string {
            std::vector<fs::path> roots;
            roots.push_back(sourceDir);

            const fs::path standaloneRoot = ResolveStandaloneSourceRoot(effectiveAppRoot);
            if (!standaloneRoot.empty()) {
                roots.push_back(standaloneRoot);
            }

            roots.push_back(effectiveAppRoot);

            for (const auto& root : roots) {
                if (root.empty()) {
                    continue;
                }

                std::vector<fs::path> candidates;
                if (useX86Target) {
                    candidates = {
                        root / "third_party" / "crinkler" / "Win32" / "crinkler.exe",
                        root / "third_party" / "Crinkler" / "Win32" / "crinkler.exe",
                        root / "third_party" / "Crinkler" / "Win32" / "Crinkler.exe",
                        root / "third_party" / "crinkler.exe",
                        root / "third_party" / "Crinkler.exe"
                    };
                } else {
                    candidates = {
                        root / "third_party" / "crinkler" / "Win64" / "crinkler.exe",
                        root / "third_party" / "Crinkler" / "Win64" / "crinkler.exe",
                        root / "third_party" / "Crinkler" / "Win64" / "Crinkler.exe",
                        root / "third_party" / "crinkler.exe",
                        root / "third_party" / "Crinkler.exe"
                    };
                }

                for (const auto& candidate : candidates) {
                    if (FileExists(candidate)) {
                        return candidate.string();
                    }
                }
            }

            return std::string();
        };

        const std::string bundledCrinkler = resolveBundledCrinklerCandidate();
        if (!bundledCrinkler.empty()) {
            fs::path current = fs::path(crinklerPath);
            fs::path bundled = fs::path(bundledCrinkler);
            std::error_code ecCurrent;
            std::error_code ecBundled;
            const fs::path currentNorm = fs::weakly_canonical(current, ecCurrent);
            const fs::path bundledNorm = fs::weakly_canonical(bundled, ecBundled);
            const bool sameBinary = (!ecCurrent && !ecBundled) ? (currentNorm == bundledNorm) : (current == bundled);

            if (!sameBinary) {
                log("Crinkler link failed. Retrying with bundled Crinkler binary for stability: " + bundledCrinkler);
                crinklerPath = bundledCrinkler;

                if (!writeInitialCache(true)) {
                    log("Error: Failed to update CMake initial cache for bundled Crinkler fallback.");
                    return result;
                }

                std::error_code resetEc;
                fs::remove(buildDir / "CMakeCache.txt", resetEc);
                resetEc.clear();
                fs::remove(buildDir / "build.ninja", resetEc);
                resetEc.clear();
                fs::remove_all(buildDir / "CMakeFiles", resetEc);

                std::string retryConfigureWithEnv = WrapWithVcVars(cmd, activeVcvarsArgs);
                log("Retry Configure Command: " + retryConfigureWithEnv);
                if (RunCommand(retryConfigureWithEnv, log)) {
                    buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
                    log("Retry Build Command: " + buildCmdWithEnv);
                    releaseBuildOk = RunCommand(buildCmdWithEnv, log);
                    if (releaseBuildOk) {
                        log("Crinkler stability fallback succeeded with bundled Crinkler.");
                    }
                }
            }
        }
    }

    if (!releaseBuildOk && useMicroPlayer && useCrinkler) {
        log("Crinkler link failed for MicroPlayer. Retrying with conservative Crinkler settings.");
        std::string stableCmd = cmd + " -DSHADERLAB_CRINKLER_TINYIMPORT=OFF";
        std::string stableCmdWithEnv = WrapWithVcVars(stableCmd, activeVcvarsArgs);
        log("Retry Configure Command: " + stableCmdWithEnv);

        if (RunCommand(stableCmdWithEnv, log)) {
            buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
            log("Retry Build Command: " + buildCmdWithEnv);
            releaseBuildOk = RunCommand(buildCmdWithEnv, log);
            if (releaseBuildOk) {
                log("Crinkler stability fallback succeeded (conservative settings).");
            }
        }
    }

    if (!releaseBuildOk && useCrinkler) {
        log("Crinkler link failed. Retrying with standard MSVC linker.");
        useCrinkler = false;
        if (!writeInitialCache(false)) {
            log("Error: Failed to update CMake initial cache for fallback build.");
            return result;
        }

        std::error_code fallbackResetEc;
        fs::remove(buildDir / "CMakeCache.txt", fallbackResetEc);
        fallbackResetEc.clear();
        fs::remove(buildDir / "build.ninja", fallbackResetEc);
        fallbackResetEc.clear();
        fs::remove_all(buildDir / "CMakeFiles", fallbackResetEc);

        std::string fallbackConfigure = cmd + " -DSHADERLAB_USE_CRINKLER=OFF -DSHADERLAB_CRINKLER_TINYIMPORT=OFF -DCRINKLER_PATH=\"\" -DCMAKE_LINKER=link.exe";
        std::string fallbackConfigureWithEnv = WrapWithVcVars(fallbackConfigure, activeVcvarsArgs);
        log("Fallback Configure Command: " + fallbackConfigureWithEnv);
        if (RunCommand(fallbackConfigureWithEnv, log)) {
            buildCmd = cmakeCmd + " --build \"" + buildDir.string() + "\" --target " + targetName + " --config Release";
            buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
            log("Fallback Build Command: " + buildCmdWithEnv);
            releaseBuildOk = RunCommand(buildCmdWithEnv, log);
            if (releaseBuildOk) {
                log("Fallback succeeded with standard MSVC linker.");
            }
        }
    }

    if (!releaseBuildOk && wantCrinkler) {
        log("Build Failed in Crinkled mode and fallback did not recover.");
        return result;
    }

    if (!releaseBuildOk) {
        log("Build Failed. Trying Debug Configuration...");
        buildCmd = "cmake --build \"" + buildDir.string() + "\" --clean-first --target " + targetName + " --config Debug";
        buildCmdWithEnv = WrapWithVcVars(buildCmd, activeVcvarsArgs);
        if (!RunCommand(buildCmdWithEnv, log)) {
            log("Build Failed.");
            return result;
        }
    }

    log("----------------------------------------");
    log("[3/6] Verify runtime artifact");

    std::vector<fs::path> candidateArtifacts = {
        buildDir / "bin" / (targetName + targetExtension),
        buildDir / "bin" / "Debug" / (targetName + targetExtension),
        buildDir / "bin" / "Release" / (targetName + targetExtension),
        buildDir / (targetName + targetExtension),
        buildDir / "Debug" / (targetName + targetExtension),
        buildDir / "Release" / (targetName + targetExtension)
    };

    if (useMicroPlayer) {
        candidateArtifacts.insert(candidateArtifacts.end(), {
            buildDir / "src" / "app" / "tiny" / "Release" / (targetName + targetExtension),
            buildDir / "src" / "app" / "tiny" / "Debug" / (targetName + targetExtension),
            buildDir / "src" / "app" / "tiny" / (targetName + targetExtension)
        });
    }

    fs::path playerExe;
    for (const auto& candidate : candidateArtifacts) {
        if (fs::exists(candidate)) {
            playerExe = candidate;
            break;
        }
    }

    if (playerExe.empty()) {
        log("Error: Could not find " + targetName + targetExtension + " in build artifacts.");
        log("Expected locations checked:");
        for (const auto& candidate : candidateArtifacts) {
            log(" - " + candidate.string());
        }
        return result;
    }

    log("Found runtime binary: " + playerExe.string());

    log("----------------------------------------");
    log("[4/6] Prepare packed project data");

    ProjectData project;
    if (!Serializer::LoadProject(request.projectPath, project)) {
        log("Error: Failed to load project for packaging.");
        return result;
    }

    std::vector<std::string> originalAudioPaths;
    originalAudioPaths.reserve(project.audioLibrary.size());
    for (const auto& clip : project.audioLibrary) {
        originalAudioPaths.push_back(clip.path);
    }

    struct FileBindingPathSnapshot {
        size_t sceneIndex = 0;
        size_t bindingIndex = 0;
        std::string path;
    };
    std::vector<FileBindingPathSnapshot> originalFileBindingPaths;
    for (size_t sceneIndex = 0; sceneIndex < project.scenes.size(); ++sceneIndex) {
        const auto& scene = project.scenes[sceneIndex];
        for (size_t bindingIndex = 0; bindingIndex < scene.bindings.size(); ++bindingIndex) {
            const auto& binding = scene.bindings[bindingIndex];
            if (binding.bindingType == BindingType::File && !binding.filePath.empty()) {
                originalFileBindingPaths.push_back({sceneIndex, bindingIndex, binding.filePath});
            }
        }
    }

    const fs::path projectRootDir = fs::path(request.projectPath).parent_path();
    auto rebaseRelativePath = [&](std::string& pathValue) {
        if (pathValue.empty()) {
            return;
        }

        fs::path sourcePath(pathValue);
        if (sourcePath.is_absolute()) {
            return;
        }

        std::error_code existsError;
        if (fs::exists(sourcePath, existsError)) {
            return;
        }

        fs::path projectRelative = projectRootDir / sourcePath;
        existsError.clear();
        if (fs::exists(projectRelative, existsError)) {
            pathValue = projectRelative.string();
        }
    };

    for (auto& clip : project.audioLibrary) {
        rebaseRelativePath(clip.path);
    }
    for (auto& scene : project.scenes) {
        for (auto& binding : scene.bindings) {
            if (binding.bindingType == BindingType::File) {
                rebaseRelativePath(binding.filePath);
            }
        }
    }

    fs::path packRoot = buildRootPath / "build_selfcontained_pack";
    std::error_code ec;
    fs::remove_all(packRoot, ec);
    fs::create_directories(packRoot, ec);

    if (!Serializer::ConsolidateProject(project, packRoot.string())) {
        log("Error: Failed to consolidate assets for packaging.");
        return result;
    }

    std::vector<Serializer::PackedExtraFile> extraFiles;

    log("Bundled asset staging summary:");
    size_t audioStagedCount = 0;
    size_t textureStagedCount = 0;
    const size_t audioTotalCount = project.audioLibrary.size();
    const size_t textureTotalCount = originalFileBindingPaths.size();
    if (project.audioLibrary.empty()) {
        log("  Audio: none");
    } else {
        for (size_t i = 0; i < project.audioLibrary.size(); ++i) {
            const std::string sourcePath = (i < originalAudioPaths.size()) ? originalAudioPaths[i] : std::string();
            const std::string stagedPath = project.audioLibrary[i].path;
            const fs::path stagedAbsolute = packRoot / fs::path(stagedPath);
            std::error_code existsEc;
            const bool stagedExists = fs::exists(stagedAbsolute, existsEc);
            if (stagedExists) {
                ++audioStagedCount;
                extraFiles.push_back({stagedAbsolute.string(), stagedPath});
            }
            log("  Audio[" + std::to_string(i) + "]: " + sourcePath + " -> " + stagedPath + (stagedExists ? " [staged]" : " [missing]"));
        }
    }

    if (originalFileBindingPaths.empty()) {
        log("  Textures: none");
    } else {
        for (const auto& snapshot : originalFileBindingPaths) {
            if (snapshot.sceneIndex >= project.scenes.size()) {
                continue;
            }
            const auto& scene = project.scenes[snapshot.sceneIndex];
            if (snapshot.bindingIndex >= scene.bindings.size()) {
                continue;
            }
            const auto& binding = scene.bindings[snapshot.bindingIndex];
            const fs::path stagedAbsolute = packRoot / fs::path(binding.filePath);
            std::error_code existsEc;
            const bool stagedExists = fs::exists(stagedAbsolute, existsEc);
            if (stagedExists) {
                ++textureStagedCount;
                extraFiles.push_back({stagedAbsolute.string(), binding.filePath});
            }
            log("  Texture[scene " + std::to_string(snapshot.sceneIndex) + ", ch " + std::to_string(binding.channelIndex) + "]: " +
                snapshot.path + " -> " + binding.filePath + (stagedExists ? " [staged]" : " [missing]"));
        }
    }
    log("  Summary: audio staged " + std::to_string(audioStagedCount) + "/" + std::to_string(audioTotalCount) +
        ", textures staged " + std::to_string(textureStagedCount) + "/" + std::to_string(textureTotalCount));
    if (audioStagedCount != audioTotalCount || textureStagedCount != textureTotalCount) {
        log("  WARNING: One or more assets were not staged. Check [missing] entries above.");
    }

    std::string writeError;

    if (useMicroPlayer) {
        log("Micro ubershader strategy: ON (single shared shader source for micro build)");
        ShaderCompiler compiler;
        if (!compiler.Initialize()) {
            log("Error: DXC not available. Cannot precompile micro ubershader modules.");
            return result;
        }

        auto logDiagnostics = [&](const std::vector<ShaderDiagnostic>& diags) {
            for (const auto& diag : diags) {
                log(diag.message);
            }
        };

        const std::string vertexShaderSource = ShaderBase::BuildFullscreenQuadVertexShaderSource();

        auto vsResult = compiler.CompileFromSource(vertexShaderSource, "main", "vs_6_0", L"vertex.hlsl", ShaderCompileMode::Build);
        if (!vsResult.success) {
            log("Error: Vertex shader precompile failed for micro build.");
            logDiagnostics(vsResult.diagnostics);
            return result;
        }

        fs::path vertexPath = packRoot / GetPackedVertexShaderPath();
        if (!WriteBinaryFile(vertexPath, vsResult.bytecode, writeError)) {
            log("Error: " + writeError);
            return result;
        }
        extraFiles.push_back({vertexPath.string(), GetPackedVertexShaderPath()});

        TinyModuleMap tinyModuleMap = BuildTinyModuleMap(project, false);
        std::unordered_set<std::string> preserveGlobalFunctionNames;

        if (!request.microUbershaderKeepEntrypointsBySignature.empty()) {
            const auto grouped = BuildMicroConflictBindings(tinyModuleMap);
            std::unordered_map<int, std::vector<std::pair<size_t, size_t>>> removalsByModule;

            for (const auto& [signatureKey, keepEntrypoints] : request.microUbershaderKeepEntrypointsBySignature) {
                const auto it = grouped.find(signatureKey);
                if (it == grouped.end()) {
                    continue;
                }

                std::unordered_set<std::string> keepSet(keepEntrypoints.begin(), keepEntrypoints.end());
                const auto& bindings = it->second;
                if (keepSet.size() == 1 && !bindings.empty()) {
                    preserveGlobalFunctionNames.insert(bindings.front().functionName);
                }

                for (const auto& binding : bindings) {
                    if (keepSet.find(binding.moduleEntrypoint) != keepSet.end()) {
                        continue;
                    }
                    removalsByModule[binding.moduleIndex].push_back({binding.signatureStart, binding.bodyEnd + 1});
                }
            }

            for (auto& [moduleIndex, ranges] : removalsByModule) {
                if (moduleIndex < 0 || static_cast<size_t>(moduleIndex) >= tinyModuleMap.modules.size()) {
                    continue;
                }
                auto& source = tinyModuleMap.modules[static_cast<size_t>(moduleIndex)];
                std::sort(ranges.begin(), ranges.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
                for (const auto& range : ranges) {
                    if (range.first >= source.size() || range.second > source.size() || range.second <= range.first) {
                        continue;
                    }
                    source.erase(range.first, range.second - range.first);
                }
            }
        }

        for (size_t moduleIndex = 0; moduleIndex < tinyModuleMap.modules.size(); ++moduleIndex) {
            const std::string entrypoint =
                (moduleIndex < tinyModuleMap.moduleEntrypoints.size()) ? tinyModuleMap.moduleEntrypoints[moduleIndex] : std::string();
            const std::string scoped = ScopeLocalFunctionsForModule(
                tinyModuleMap.modules[moduleIndex],
                entrypoint,
                preserveGlobalFunctionNames.empty() ? nullptr : &preserveGlobalFunctionNames);
            tinyModuleMap.modules[moduleIndex] = MinifyShaderTextForPack(scoped);
        }

        log("Micro module table (moduleId -> runtime entrypoint):");
        for (size_t moduleIndex = 0; moduleIndex < tinyModuleMap.modules.size(); ++moduleIndex) {
            const std::string entrypoint =
                (moduleIndex < tinyModuleMap.moduleEntrypoints.size() && !tinyModuleMap.moduleEntrypoints[moduleIndex].empty())
                    ? tinyModuleMap.moduleEntrypoints[moduleIndex]
                    : ("<unknown>");
            log("  [" + std::to_string(moduleIndex) + "] -> " + entrypoint);
        }

        log("Micro tracker module bindings:");
        for (size_t sceneIndex = 0; sceneIndex < tinyModuleMap.sceneModuleIndices.size(); ++sceneIndex) {
            log("  scene[" + std::to_string(sceneIndex) + "] -> module " + std::to_string(tinyModuleMap.sceneModuleIndices[sceneIndex]));
            if (sceneIndex < tinyModuleMap.postFxModuleIndices.size()) {
                const auto& fxModules = tinyModuleMap.postFxModuleIndices[sceneIndex];
                for (size_t fxIndex = 0; fxIndex < fxModules.size(); ++fxIndex) {
                    log("    postfx[" + std::to_string(fxIndex) + "] -> module " + std::to_string(fxModules[fxIndex]));
                }
            }
        }
        for (size_t transitionIndex = 0; transitionIndex < 6; ++transitionIndex) {
            log("  transition[" + std::to_string(transitionIndex) + "] -> module " + std::to_string(tinyModuleMap.transitionModuleIndices[transitionIndex]));
        }

        std::string ubershaderSource;
        for (const auto& moduleCode : tinyModuleMap.modules) {
            ubershaderSource += moduleCode;
            ubershaderSource.push_back('\n');
        }

        std::vector<std::vector<uint8_t>> microModuleBytecode;
        microModuleBytecode.resize(tinyModuleMap.modules.size());
        for (size_t moduleIndex = 0; moduleIndex < tinyModuleMap.modules.size(); ++moduleIndex) {
            const std::string entrypoint =
                (moduleIndex < tinyModuleMap.moduleEntrypoints.size() && !tinyModuleMap.moduleEntrypoints[moduleIndex].empty())
                    ? tinyModuleMap.moduleEntrypoints[moduleIndex]
                    : ("s" + std::to_string(moduleIndex));
            auto tryCompile = [&](const std::string& source, const wchar_t* sourceName, std::vector<uint8_t>& outBytecode) -> bool {
                auto psResult = compiler.CompileFromSource(source, "PSMain", "ps_6_0", sourceName, ShaderCompileMode::Build);
                if (!psResult.success) {
                    logDiagnostics(psResult.diagnostics);
                    return false;
                }
                outBytecode = std::move(psResult.bytecode);
                return true;
            };

            const std::string wrappedCombined = BuildPixelShaderSource(ubershaderSource, {}, false, entrypoint);
            if (tryCompile(wrappedCombined, L"micro_ubershader.hlsl", microModuleBytecode[moduleIndex])) {
                continue;
            }

            log("Warning: Combined ubershader compile failed at " + entrypoint + ". Retrying module-local compile.");
            const std::string wrappedModuleLocal = BuildPixelShaderSource(tinyModuleMap.modules[moduleIndex], {}, false, entrypoint);
            if (tryCompile(wrappedModuleLocal, L"micro_ubershader_module.hlsl", microModuleBytecode[moduleIndex])) {
                log("Info: Module-local fallback compile succeeded at " + entrypoint + ".");
                continue;
            }

            log("Warning: Module-local compile failed at " + entrypoint + ". Using placeholder shader module.");
            const std::string placeholderModule = ShaderBase::BuildPlaceholderFragmentModuleSource();
            const std::string wrappedPlaceholder = BuildPixelShaderSource(placeholderModule, {}, false, "main");
            if (tryCompile(wrappedPlaceholder, L"micro_ubershader_placeholder.hlsl", microModuleBytecode[moduleIndex])) {
                log("Info: Placeholder shader module emitted for " + entrypoint + ".");
                continue;
            }

            log("Error: Placeholder module compile also failed at " + entrypoint + ".");
            return result;
        }

        fs::path ubershaderBytecodePath = packRoot / "assets" / "shaders" / "ubershader.bin";
        if (!WriteMicroUbershaderBytecodeBlob(microModuleBytecode, ubershaderBytecodePath, writeError)) {
            log("Error: " + writeError);
            return result;
        }
        extraFiles.push_back({ubershaderBytecodePath.string(), "assets/shaders/ubershader.bin"});

        for (auto& scene : project.scenes) {
            scene.precompiledPath.clear();
            for (auto& fx : scene.postFxChain) {
                fx.precompiledPath.clear();
            }
        }
    } else {
        ShaderCompiler compiler;
        if (!compiler.Initialize()) {
            log("Error: DXC not available. Cannot precompile shaders for self-contained build.");
            return result;
        }

        auto logDiagnostics = [&](const std::vector<ShaderDiagnostic>& diags) {
            for (const auto& diag : diags) {
                log(diag.message);
            }
        };

        log("Precompiling vertex shader");
        const std::string vertexShaderSource = ShaderBase::BuildFullscreenQuadVertexShaderSource();

        auto vsResult = compiler.CompileFromSource(vertexShaderSource, "main", "vs_6_0", L"vertex.hlsl", ShaderCompileMode::Build);
        if (!vsResult.success) {
            log("Error: Vertex shader precompile failed.");
            logDiagnostics(vsResult.diagnostics);
            return result;
        }

        fs::path vertexPath = packRoot / GetPackedVertexShaderPath();
        if (!WriteBinaryFile(vertexPath, vsResult.bytecode, writeError)) {
            log("Error: " + writeError);
            return result;
        }
        extraFiles.push_back({vertexPath.string(), GetPackedVertexShaderPath()});

        bool usedTransitions[kTransitionSlotCount] = {};
        for (const auto& row : project.track.rows) {
            if (row.transitionPresetStem.empty()) {
                continue;
            }
            const int idx = TransitionSlotIndexFromStem(row.transitionPresetStem);
            if (idx >= 0 && idx < static_cast<int>(kTransitionSlotCount)) {
                usedTransitions[idx] = true;
            }
        }

        log("Precompiling used transitions");
        for (size_t transitionIdx = 0; transitionIdx < kTransitionSlotCount; ++transitionIdx) {
            if (!usedTransitions[transitionIdx]) {
                continue;
            }
            const std::string transitionStem = kTransitionSlotStems[transitionIdx];
            const char* packedPath = GetTransitionPackedPath(transitionStem);
            if (!packedPath || !*packedPath) continue;
            std::string shaderSource = GetTransitionShaderSourceForBuild(transitionStem);
            std::vector<ShaderBase::TextureBindingDecl> decls = { {0, "Texture2D"}, {1, "Texture2D"} };
            std::string wrapped = BuildPixelShaderSource(shaderSource, decls);
            auto psResult = compiler.CompileFromSource(wrapped, "PSMain", "ps_6_0", L"transition.hlsl", ShaderCompileMode::Build);
            if (!psResult.success) {
                log("Error: Transition shader precompile failed.");
                logDiagnostics(psResult.diagnostics);
                return result;
            }
            fs::path transitionPath = packRoot / packedPath;
            if (!WriteBinaryFile(transitionPath, psResult.bytecode, writeError)) {
                log("Error: " + writeError);
                return result;
            }
            extraFiles.push_back({transitionPath.string(), packedPath});
        }

        log("Precompiling scene and post FX shaders");
        for (size_t i = 0; i < project.scenes.size(); ++i) {
            auto& scene = project.scenes[i];
            log("Scene " + std::to_string(i) + ": " + scene.name);
            std::vector<ShaderBase::TextureBindingDecl> decls;
            for (const auto& b : scene.bindings) {
                if (!b.enabled) continue;
                ShaderBase::TextureBindingDecl decl;
                decl.slot = b.channelIndex;
                if (b.type == TextureType::TextureCube) decl.type = "TextureCube";
                else if (b.type == TextureType::Texture3D) decl.type = "Texture3D";
                else decl.type = "Texture2D";
                decls.push_back(decl);
            }

            std::string wrapped = BuildPixelShaderSource(scene.shaderCode, decls);
            log("  Compiling scene shader -> assets/shaders/scene_" + std::to_string(i) + ".cso");
            auto psResult = compiler.CompileFromSource(wrapped, "PSMain", "ps_6_0", L"scene.hlsl", ShaderCompileMode::Build);
            if (!psResult.success) {
                log("Error: Scene shader precompile failed: " + scene.name);
                logDiagnostics(psResult.diagnostics);
                return result;
            }

            std::string scenePackedPath = "assets/shaders/scene_" + std::to_string(i) + ".cso";
            fs::path scenePath = packRoot / scenePackedPath;
            if (!WriteBinaryFile(scenePath, psResult.bytecode, writeError)) {
                log("Error: " + writeError);
                return result;
            }
            scene.precompiledPath = scenePackedPath;
            extraFiles.push_back({scenePath.string(), scenePackedPath});
            log("  Packed scene shader: " + scenePackedPath);

            for (size_t fxIndex = 0; fxIndex < scene.postFxChain.size(); ++fxIndex) {
                auto& fx = scene.postFxChain[fxIndex];
                if (!fx.enabled) {
                    fx.precompiledPath.clear();
                    continue;
                }
                log("  Compiling post FX [" + std::to_string(fxIndex) + "] " + fx.name + " -> assets/shaders/scene_" + std::to_string(i) + "_fx_" + std::to_string(fxIndex) + ".cso");
                std::string fxWrapped = BuildPixelShaderSource(fx.shaderCode, {}, true);
                auto fxResult = compiler.CompileFromSource(fxWrapped, "PSMain", "ps_6_0", L"postfx.hlsl", ShaderCompileMode::Build);
                if (!fxResult.success) {
                    log("Error: Post FX precompile failed: " + fx.name);
                    logDiagnostics(fxResult.diagnostics);
                    return result;
                }

                std::string fxPackedPath = "assets/shaders/scene_" + std::to_string(i) + "_fx_" + std::to_string(fxIndex) + ".cso";
                fs::path fxPath = packRoot / fxPackedPath;
                if (!WriteBinaryFile(fxPath, fxResult.bytecode, writeError)) {
                    log("Error: " + writeError);
                    return result;
                }
                fx.precompiledPath = fxPackedPath;
                extraFiles.push_back({fxPath.string(), fxPackedPath});
                log("  Packed post FX shader: " + fxPackedPath);
            }
        }
    }

    fs::path packProjectPath = packRoot / "project.json";

    bool compactTrackPayloadQueued = false;
    const bool writeCompactTrackPayload = request.restrictedCompactTrack || useMicroPlayer;
    if (writeCompactTrackPayload) {
        if (useMicroPlayer) {
            log("MicroPlayer packaging: compact track binary enabled");
        } else {
            log("Restricted optimization: compact track binary enabled");
        }
        fs::path compactTrackPath = packRoot / "assets" / "track.bin";
        if (!WriteCompactTrackBinary(project, compactTrackPath, writeError)) {
            log("Error: " + writeError);
            return result;
        }
        extraFiles.push_back({ compactTrackPath.string(), "assets/track.bin" });
        compactTrackPayloadQueued = true;

        project.track.name.clear();
        project.track.rows.clear();
        project.track.currentBeat = 0;
        project.track.lastTriggeredBeat = -1;

        for (auto& scene : project.scenes) {
            scene.name.clear();
            scene.shaderCode.clear();
            for (auto& fx : scene.postFxChain) {
                fx.name.clear();
                fx.shaderCode.clear();
            }
        }
        for (auto& clip : project.audioLibrary) {
            clip.name.clear();
        }
    }

    if (useMicroPlayer && !compactTrackPayloadQueued) {
        log("Error: MicroPlayer packaging contract violated: compact track payload was not queued for embedding.");
        return result;
    }

    if (!Serializer::SaveProject(project, packProjectPath.string())) {
        log("Error: Failed to write packed project manifest.");
        return result;
    }

    log("----------------------------------------");
    log("[5/6] Export clean solution directory");
    fs::path cleanSolutionRoot;
    std::string cleanSolutionError;
    if (ExportCleanSolutionDirectory(resolvedRequest, packRoot, useScreenSaver, useMicroPlayer, useCrinkler, staticRuntime, cleanSolutionRoot, cleanSolutionError)) {
        log("Clean solution directory: " + cleanSolutionRoot.string());
    } else {
        log("Warning: Failed to export clean solution directory: " + cleanSolutionError);
    }

    log("----------------------------------------");
    log("[6/6] Create final artifact");
    bool artifactOk = false;
    fs::path finalArtifactPath = fs::path(request.targetExePath);

    std::string outputWritableError;
    if (!EnsureOutputArtifactWritable(finalArtifactPath, outputWritableError)) {
        log("Error: " + outputWritableError);
        return result;
    }

    if (isPackagedDemo) {
        fs::path packageDir = buildRootPath / "packaged_demo";
        std::error_code packageEc;
        fs::remove_all(packageDir, packageEc);
        packageEc.clear();
        fs::create_directories(packageDir, packageEc);
        if (packageEc) {
            log("Error: Failed to create packaged output directory: " + packageDir.string());
            return result;
        }

        const fs::path runtimeCopy = packageDir / playerExe.filename();
        std::string copyError;
        if (!CopyPathRecursive(playerExe, runtimeCopy, copyError)) {
            log("Error: " + copyError);
            return result;
        }

        if (!CopyPathRecursive(packProjectPath, packageDir / "project.json", copyError)) {
            log("Error: " + copyError);
            return result;
        }
        if (!CopyPathRecursive(packRoot / "assets", packageDir / "assets", copyError)) {
            log("Error: " + copyError);
            return result;
        }

        std::string ext = finalArtifactPath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".zip") {
            finalArtifactPath.replace_extension(".zip");
        }

        std::string zipError;
        artifactOk = CreatePackagedZip(packageDir, finalArtifactPath, log, zipError);
        if (!artifactOk) {
            log("Error: " + zipError);
            return result;
        }
    } else {
        artifactOk = Serializer::PackExecutable(playerExe.string(),
                                                finalArtifactPath.string(),
                                                packProjectPath.string(),
                                                extraFiles,
                                                !useMicroPlayer);
        if (!artifactOk) {
            log("Error: PackExecutable failed.");
            log("  sourceExe: " + playerExe.string());
            log("  output: " + finalArtifactPath.string());
            log("  manifest: " + packProjectPath.string());
            log("  extraFiles: " + std::to_string(extraFiles.size()));
        }
    }

    if (artifactOk) {
        std::error_code sizeEc;
        const uint64_t finalSize = fs::file_size(finalArtifactPath, sizeEc);
        if (!sizeEc) {
            result.finalExeBytes = finalSize;
            log("Final artifact size: " + std::to_string(finalSize) + " bytes");
        }

        const uint64_t budgetBytes = SizePresetToBytes(request.sizeTarget);
        result.budgetBytes = budgetBytes;
        if (budgetBytes > 0 && !sizeEc) {
            if (finalSize <= budgetBytes) {
                result.budgetHit = true;
                const uint64_t bytesLeft = budgetBytes - finalSize;
                result.report = "Size target " + std::string(SizePresetName(request.sizeTarget)) + " hit. " + std::to_string(bytesLeft) + " bytes left in budget.";
                log(result.report);
            } else {
                result.budgetHit = false;
                const uint64_t overshoot = finalSize - budgetBytes;
                result.report = "Size target " + std::string(SizePresetName(request.sizeTarget)) + " missed. Overshot by " + std::to_string(overshoot) + " bytes.";
                log(result.report);
            }
        }

        log("----------------------------------------");
        if (isPackagedDemo) {
            log("Runtime Target Path: Packaged Demo (.zip with runtime + assets)");
        } else if (useMicroPlayer) {
            log("Runtime Target Path: MicroPlayer (x86 budget runtime path)");
        } else {
            log("Runtime Target Path: Full Runtime Player (x64 open/free demo path)");
        }
        log("Output Artifact: " + finalArtifactPath.string());
        if (!cleanSolutionRoot.empty()) {
            log("Clean Solution Directory: " + cleanSolutionRoot.string());
        }
        log("BUILD SUCCESSFUL");
        result.success = true;
    } else {
        log("Error: Failed to create final artifact.");
    }

    return result;
}

} // namespace ShaderLab
