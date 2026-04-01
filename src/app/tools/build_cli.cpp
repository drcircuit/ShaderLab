#include "ShaderLab/DevKit/BuildPipeline.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string ResolveExecutableDirectory() {
    char exePath[MAX_PATH] = {};
    DWORD length = GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return fs::path(std::string(exePath, length)).parent_path().string();
}

ShaderLab::BuildMode ParseMode(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "releasecrinkled" || lower == "crinkled" || lower == "release-crinkled") {
        return ShaderLab::BuildMode::ReleaseCrinkled;
    }
    return ShaderLab::BuildMode::Release;
}

ShaderLab::SizeTargetPreset ParseSizePreset(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "1k" || lower == "2k" || lower == "4k" || lower == "16k" || lower == "32k") return ShaderLab::SizeTargetPreset::K64;
    if (lower == "64k") return ShaderLab::SizeTargetPreset::K64;
    if (lower == "128k") return ShaderLab::SizeTargetPreset::K128;
    if (lower == "256k") return ShaderLab::SizeTargetPreset::K256;
    if (lower == "512k") return ShaderLab::SizeTargetPreset::K512;
    if (lower == "1024k" || lower == "1m") return ShaderLab::SizeTargetPreset::K1024;
    return ShaderLab::SizeTargetPreset::None;
}

ShaderLab::BuildTargetKind ParseTarget(const std::string& value) {
    std::string lower = value;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "packaged" || lower == "packaged-demo") {
        return ShaderLab::BuildTargetKind::PackagedDemo;
    }
    if (lower == "screensaver" || lower == "selfcontained-screensaver") {
        return ShaderLab::BuildTargetKind::SelfContainedScreenSaver;
    }
    if (lower == "micro" || lower == "micro-demo") {
        return ShaderLab::BuildTargetKind::MicroDemo;
    }
    return ShaderLab::BuildTargetKind::SelfContainedDemo;
}

void PrintUsage() {
    std::cout
        << "ShaderLabBuildCli usage:\n"
        << "  --project <path>\n"
        << "  --output <path>\n"
        << "  [--app-root <path>]\n"
        << "  [--solution-root <path>]\n"
        << "  [--target selfcontained|packaged|screensaver|micro]\n"
        << "  [--mode release|crinkled]\n"
        << "  [--size none|64k|128k|256k|512k|1024k]\n"
        << "  [--restricted-compact-track]\n"
        << "  [--runtime-debug]\n"
        << "  [--compact-debug]\n"
        << "  [--micro-dev]\n";
}

} // namespace

int main(int argc, char** argv) {
    ShaderLab::BuildRequest request;
    request.targetKind = ShaderLab::BuildTargetKind::SelfContainedDemo;
    request.mode = ShaderLab::BuildMode::Release;
    request.sizeTarget = ShaderLab::SizeTargetPreset::None;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--project" && i + 1 < argc) {
            request.projectPath = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            request.targetExePath = argv[++i];
        } else if (arg == "--app-root" && i + 1 < argc) {
            request.appRoot = argv[++i];
        } else if (arg == "--solution-root" && i + 1 < argc) {
            request.cleanSolutionRootPath = argv[++i];
        } else if (arg == "--target" && i + 1 < argc) {
            request.targetKind = ParseTarget(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            request.mode = ParseMode(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            request.sizeTarget = ParseSizePreset(argv[++i]);
        } else if (arg == "--restricted-compact-track") {
            request.restrictedCompactTrack = true;
        } else if (arg == "--runtime-debug") {
            request.runtimeDebugLog = true;
        } else if (arg == "--compact-debug") {
            request.compactTrackDebugLog = true;
        } else if (arg == "--micro-dev") {
            request.microDeveloperBuild = true;
            request.runtimeDebugLog = true;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            return 0;
        }
    }

    if (request.appRoot.empty()) {
        request.appRoot = ResolveExecutableDirectory();
        if (request.appRoot.empty()) {
            request.appRoot = fs::current_path().string();
        }
    }

    if (request.projectPath.empty() || request.targetExePath.empty()) {
        PrintUsage();
        return 2;
    }

    auto result = ShaderLab::BuildPipeline::BuildSelfContained(request, [](const std::string& line) {
        std::cout << line << "\n";
    });

    if (!result.report.empty()) {
        std::cout << result.report << "\n";
    }

    return result.success ? 0 : 1;
}
