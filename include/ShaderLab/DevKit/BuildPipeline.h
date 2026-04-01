#pragma once

#include <functional>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ShaderLab {

enum class BuildMode {
    Release,
    ReleaseCrinkled
};

enum class BuildTargetKind {
    PackagedDemo,
    SelfContainedDemo,
    SelfContainedScreenSaver,
    MicroDemo
};

enum class SizeTargetPreset {
    None,
    K64,
    K128,
    K256,
    K512,
    K1024
};

struct BuildPrereqReport {
    bool ok = false;
    std::string message;
    bool hasVisualStudioTools = false;
    bool hasWindowsSdk = false;
    bool hasCMake = false;
    bool hasDxcRuntime = false;
    bool hasCrinkler = false;
    bool hasNinja = false;
};

struct MicroUbershaderConflictOption {
    std::string moduleEntrypoint;
    std::string moduleLabel;
    std::string signature;
    std::string snippet;
};

struct MicroUbershaderConflict {
    std::string signatureKey;
    std::string signatureDisplay;
    std::vector<MicroUbershaderConflictOption> options;
};

struct BuildRequest {
    std::string appRoot;
    std::string projectPath;
    std::string targetExePath;
    std::string cleanSolutionRootPath;
    BuildTargetKind targetKind = BuildTargetKind::SelfContainedDemo;
    BuildMode mode = BuildMode::Release;
    SizeTargetPreset sizeTarget = SizeTargetPreset::None;
    bool restrictedCompactTrack = false;
    bool runtimeDebugLog = false;
    bool compactTrackDebugLog = false;
    bool microDeveloperBuild = false;
    std::unordered_map<std::string, std::vector<std::string>> microUbershaderKeepEntrypointsBySignature;
};

struct BuildResult {
    bool success = false;
    bool budgetHit = true;
    uint64_t finalExeBytes = 0;
    uint64_t budgetBytes = 0;
    std::string report;
};

class BuildPipeline {
public:
    static BuildPrereqReport CheckPrereqs(const std::string& appRoot, BuildMode mode = BuildMode::Release);
    static std::vector<MicroUbershaderConflict> AnalyzeMicroUbershaderConflicts(const std::string& projectPath);
    static bool GenerateMicroUbershaderSource(const std::string& projectPath,
                                              std::string& outSource,
                                              std::string& outError);
    static BuildResult BuildSelfContained(
        const BuildRequest& request,
        const std::function<void(const std::string&)>& log);
};

} // namespace ShaderLab
