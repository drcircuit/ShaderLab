#include "ShaderLab/Core/Serializer.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <unordered_set>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace ShaderLab {

namespace {

constexpr char kPackedCompressedMagic[4] = {'S', 'L', 'Z', '1'};

#if defined(_WIN32)
typedef LONG (WINAPI *RtlGetCompressionWorkSpaceSizeFn)(USHORT, PULONG, PULONG);
typedef LONG (WINAPI *RtlCompressBufferFn)(USHORT, PUCHAR, ULONG, PUCHAR, ULONG, ULONG, PULONG, PVOID);

bool TryCompressPackedEntry(const std::vector<uint8_t>& input, std::vector<uint8_t>& output) {
    output.clear();

    if (input.size() < 256 || input.size() > static_cast<size_t>(UINT32_MAX)) {
        return false;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        ntdll = LoadLibraryW(L"ntdll.dll");
    }
    if (!ntdll) {
        return false;
    }

    const auto rtlGetCompressionWorkSpaceSize = reinterpret_cast<RtlGetCompressionWorkSpaceSizeFn>(
        GetProcAddress(ntdll, "RtlGetCompressionWorkSpaceSize"));
    const auto rtlCompressBuffer = reinterpret_cast<RtlCompressBufferFn>(
        GetProcAddress(ntdll, "RtlCompressBuffer"));
    if (!rtlGetCompressionWorkSpaceSize || !rtlCompressBuffer) {
        return false;
    }

    constexpr USHORT kFormatAndEngine = static_cast<USHORT>(2u | 0x0100u);
    ULONG workspaceSize = 0;
    ULONG fragmentWorkspaceSize = 0;
    if (rtlGetCompressionWorkSpaceSize(kFormatAndEngine, &workspaceSize, &fragmentWorkspaceSize) != 0 || workspaceSize == 0) {
        return false;
    }

    std::vector<uint8_t> workspace(static_cast<size_t>(workspaceSize));
    std::vector<uint8_t> compressed(input.size() + input.size() / 8 + 1024);

    ULONG compressedSize = 0;
    if (rtlCompressBuffer(
            kFormatAndEngine,
            reinterpret_cast<PUCHAR>(const_cast<uint8_t*>(input.data())),
            static_cast<ULONG>(input.size()),
            reinterpret_cast<PUCHAR>(compressed.data()),
            static_cast<ULONG>(compressed.size()),
            4096,
            &compressedSize,
            workspace.data()) != 0) {
        return false;
    }

    constexpr size_t kHeaderSize = 12;
    if (compressedSize == 0 || (kHeaderSize + static_cast<size_t>(compressedSize)) >= input.size()) {
        return false;
    }

    output.resize(kHeaderSize + static_cast<size_t>(compressedSize));
    std::memcpy(output.data(), kPackedCompressedMagic, 4);

    const uint32_t rawSize = static_cast<uint32_t>(input.size());
    const uint32_t storedCompressedSize = static_cast<uint32_t>(compressedSize);
    std::memcpy(output.data() + 4, &rawSize, sizeof(uint32_t));
    std::memcpy(output.data() + 8, &storedCompressedSize, sizeof(uint32_t));
    std::memcpy(output.data() + kHeaderSize, compressed.data(), compressedSize);
    return true;
}
#endif

std::string NormalizePathSlashes(std::string value) {
    std::replace(value.begin(), value.end(), '\\', '/');
    return value;
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool TryReadTextFile(const fs::path& filePath, std::string& outContent) {
    std::ifstream input(filePath, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    outContent.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    return true;
}

bool TryParseCodeLink(const std::string& codeField, std::string& outPath) {
    constexpr const char* kPrefix = "@file:";
    if (codeField.rfind(kPrefix, 0) != 0) {
        return false;
    }
    outPath = NormalizePathSlashes(codeField.substr(6));
    return !outPath.empty();
}

fs::path InferWorkspaceRootFromProjectDirectory(const fs::path& projectDirectory) {
    if (!projectDirectory.empty()) {
        const fs::path parent = projectDirectory.parent_path();
        if (!parent.empty() && parent.filename() == "projects") {
            return parent.parent_path();
        }
    }

    if (const char* workspaceEnv = std::getenv("SHADERLAB_WORKSPACE")) {
        if (*workspaceEnv) {
            return fs::path(workspaceEnv);
        }
    }

    if (const char* userProfile = std::getenv("USERPROFILE")) {
        if (*userProfile) {
            return fs::path(userProfile) / "ShaderLabs";
        }
    }

    return {};
}

fs::path ResolveProjectPath(const std::string& storedPath,
                           const fs::path& projectDirectory,
                           const fs::path& workspaceRoot) {
    fs::path sourcePath(storedPath);
    if (sourcePath.is_absolute()) {
        return sourcePath;
    }

    const fs::path projectRelative = (projectDirectory / sourcePath).lexically_normal();
    std::error_code ec;
    if (fs::exists(projectRelative, ec) && !ec) {
        return projectRelative;
    }

    if (!workspaceRoot.empty()) {
        const fs::path workspaceRelative = (workspaceRoot / sourcePath).lexically_normal();
        ec.clear();
        if (fs::exists(workspaceRelative, ec) && !ec) {
            return workspaceRelative;
        }
        return workspaceRelative;
    }

    return projectRelative;
}

void ResolveLinkedShaderCode(ProjectData& project, const fs::path& projectDirectory) {
    const fs::path workspaceRoot = InferWorkspaceRootFromProjectDirectory(projectDirectory);
    auto resolveShader = [&](std::string& shaderCode, std::string& shaderCodePath) {
        if (shaderCodePath.empty()) {
            std::string linkedPath;
            if (TryParseCodeLink(shaderCode, linkedPath)) {
                shaderCodePath = linkedPath;
            }
        }

        if (shaderCodePath.empty()) {
            return;
        }

        const fs::path sourcePath = ResolveProjectPath(shaderCodePath, projectDirectory, workspaceRoot);

        std::string fileContent;
        if (TryReadTextFile(sourcePath, fileContent)) {
            shaderCode = fileContent;
        }
    };

    for (auto& scene : project.scenes) {
        resolveShader(scene.shaderCode, scene.shaderCodePath);
        for (auto& fx : scene.postFxChain) {
            resolveShader(fx.shaderCode, fx.shaderCodePath);
        }
    }
}

void ResolveLinkedAssetPaths(ProjectData& project, const fs::path& projectDirectory) {
    const fs::path workspaceRoot = InferWorkspaceRootFromProjectDirectory(projectDirectory);

    auto resolvePath = [&](std::string& pathValue) {
        if (pathValue.empty()) {
            return;
        }
        const fs::path resolved = ResolveProjectPath(pathValue, projectDirectory, workspaceRoot);
        pathValue = resolved.lexically_normal().string();
    };

    for (auto& clip : project.audioLibrary) {
        resolvePath(clip.path);
    }

    for (auto& scene : project.scenes) {
        resolvePath(scene.precompiledPath);
        for (auto& bind : scene.bindings) {
            if (bind.bindingType == BindingType::File) {
                resolvePath(bind.filePath);
            }
        }
        for (auto& fx : scene.postFxChain) {
            resolvePath(fx.precompiledPath);
        }
    }
}

std::vector<uint8_t> ReadStreamBytes(std::istream& stream) {
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

struct PackedEntryInfo {
    std::string path;
    uint64_t offset;
    uint64_t size;
};

template <typename Visitor>
void ForEachProjectAssetPath(const ProjectData& project, Visitor&& visitor) {
    for (const auto& clip : project.audioLibrary) {
        visitor(clip.path);
    }

    for (const auto& scene : project.scenes) {
        for (const auto& bind : scene.bindings) {
            if (bind.bindingType == BindingType::File && !bind.filePath.empty()) {
                visitor(bind.filePath);
            }
        }

        visitor(scene.precompiledPath);

        for (const auto& fx : scene.postFxChain) {
            visitor(fx.precompiledPath);
        }
    }
}

struct ExecutablePackAccumulator {
    explicit ExecutablePackAccumulator(bool enableCompression)
        : compressEntries(enableCompression) {
    }

    bool compressEntries = false;
    std::vector<uint8_t> packBlob;
    std::vector<PackedEntryInfo> entries;
    std::unordered_set<std::string> packedPathIndex;

    bool HasPackedPath(const std::string& packedPath) const {
        return packedPathIndex.find(packedPath) != packedPathIndex.end();
    }

    void AddEntryFromData(const std::string& packedPath, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> compressedData;
        const std::vector<uint8_t>* payload = &data;
#if defined(_WIN32)
        if (compressEntries && TryCompressPackedEntry(data, compressedData)) {
            payload = &compressedData;
        }
#endif

        PackedEntryInfo info;
        info.path = packedPath;
        info.offset = packBlob.size();
        info.size = payload->size();
        entries.push_back(info);
        packedPathIndex.insert(packedPath);
        packBlob.insert(packBlob.end(), payload->begin(), payload->end());
    }

    void TryAddEntryFromDisk(const fs::path& diskPath, const std::string& packedPathRaw) {
        const std::string packedPath = NormalizePathSlashes(packedPathRaw);
        if (HasPackedPath(packedPath) || !fs::exists(diskPath)) {
            return;
        }

        std::ifstream file(diskPath, std::ios::binary);
        std::vector<uint8_t> data = ReadStreamBytes(file);
        AddEntryFromData(packedPath, data);
    }

    void TryAddProjectManifest(const std::string& projectJsonPath,
                               bool includeProjectManifest,
                               bool hasProjectJsonPath) {
        if (!includeProjectManifest || !hasProjectJsonPath) {
            return;
        }

        std::ifstream file(projectJsonPath, std::ios::binary);
        std::vector<uint8_t> data = ReadStreamBytes(file);
        AddEntryFromData("project.json", data);
    }

    void AddProjectAssets(const ProjectData& project, const fs::path& projectRoot) {
        ForEachProjectAssetPath(project, [&](const std::string& relativePath) {
            if (relativePath.empty()) {
                return;
            }
            TryAddEntryFromDisk(projectRoot / relativePath, relativePath);
        });
    }

    void AddExtraFiles(const std::vector<Serializer::PackedExtraFile>& extraFiles) {
        for (const auto& extra : extraFiles) {
            TryAddEntryFromDisk(extra.sourcePath, extra.packedPath);
        }
    }
};

std::vector<uint8_t> BuildDirectoryBlob(const std::vector<PackedEntryInfo>& packEntries,
                                        uint64_t exeSize) {
    std::vector<uint8_t> blob;
    uint32_t count = (uint32_t)packEntries.size();
    auto append = [&](const void* data, size_t size) {
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        blob.insert(blob.end(), bytes, bytes + size);
    };

    append(&count, sizeof(uint32_t));
    for (const auto& entry : packEntries) {
        uint32_t len = (uint32_t)entry.path.length();
        append(&len, sizeof(uint32_t));
        append(entry.path.data(), len);
        uint64_t absOffset = exeSize + entry.offset;
        append(&absOffset, sizeof(uint64_t));
        append(&entry.size, sizeof(uint64_t));
    }

    return blob;
}

bool EnsureOutputDirectory(const std::string& outputPathText) {
    std::error_code ec;
    fs::path outputPath(outputPathText);
    if (!outputPath.parent_path().empty()) {
        fs::create_directories(outputPath.parent_path(), ec);
        if (ec) {
            return false;
        }
    }
    return true;
}

bool WritePackedExecutable(const std::string& outputPathText,
                           const std::vector<uint8_t>& executableData,
                           const std::vector<uint8_t>& packedData,
                           const std::vector<uint8_t>& directoryData) {
    std::ofstream out(outputPathText, std::ios::binary);
    if (!out.is_open()) {
        return false;
    }

    out.write((const char*)executableData.data(), executableData.size());
    out.write((const char*)packedData.data(), packedData.size());

    uint64_t dirStartOffset = executableData.size() + packedData.size();
    out.write((const char*)directoryData.data(), directoryData.size());

    out.write((const char*)&dirStartOffset, sizeof(uint64_t));
    const char MAGIC[] = "SHADERLAB_PACK";
    out.write(MAGIC, 14);

    out.flush();
    return out.good();
}

} // namespace

    // Helper conversions - Moved to ShaderLab namespace for ADL
    NLOHMANN_JSON_SERIALIZE_ENUM(TextureType, {
        {TextureType::Texture2D, "Texture2D"},
        {TextureType::TextureCube, "TextureCube"},
        {TextureType::Texture3D, "Texture3D"}
    })

    NLOHMANN_JSON_SERIALIZE_ENUM(BindingType, {
        {BindingType::Scene, "Scene"},
        {BindingType::File, "File"}
    })

    NLOHMANN_JSON_SERIALIZE_ENUM(RenderAspectRatioPreset, {
        {RenderAspectRatioPreset::Ratio_16_9, "16:9"},
        {RenderAspectRatioPreset::Ratio_1_1, "1:1"},
        {RenderAspectRatioPreset::Ratio_16_10, "16:10"},
        {RenderAspectRatioPreset::Ratio_4_3, "4:3"}
    })

    NLOHMANN_JSON_SERIALIZE_ENUM(FullscreenRenderResolutionPreset, {
        {FullscreenRenderResolutionPreset::Full, "full"},
        {FullscreenRenderResolutionPreset::W4096, "4096"},
        {FullscreenRenderResolutionPreset::W2560, "2560"},
        {FullscreenRenderResolutionPreset::W2048, "2048"},
        {FullscreenRenderResolutionPreset::W1920, "1920"},
        {FullscreenRenderResolutionPreset::W1600, "1600"},
        {FullscreenRenderResolutionPreset::W1280, "1280"},
        {FullscreenRenderResolutionPreset::W1024, "1024"},
        {FullscreenRenderResolutionPreset::W800, "800"},
        {FullscreenRenderResolutionPreset::W640, "640"}
    })
    
    void to_json(json& j, const TextureBinding& b);
    void from_json(const json& j, TextureBinding& b);
    void to_json(json& j, const Scene::PostFXEffect& e);
    void from_json(const json& j, Scene::PostFXEffect& e);
    void to_json(json& j, const Scene& s);
    void from_json(const json& j, Scene& s);
    void to_json(json& j, const AudioClip& a);
    void from_json(const json& j, AudioClip& a);
    void to_json(json& j, const TrackerRow& r);
    void from_json(const json& j, TrackerRow& r);
    void to_json(json& j, const DemoTrack& t);
    void from_json(const json& j, DemoTrack& t);
    void to_json(json& j, const ProjectData& p);
    void from_json(const json& j, ProjectData& p);


    void to_json(json& j, const TextureBinding& b) {
        j = json{
            {"channel", b.channelIndex},
            {"enabled", b.enabled},
            {"bindType", b.bindingType},
            {"sourceIndex", b.sourceSceneIndex},
            {"path", b.filePath},
            {"type", b.type}
        };
    }

    void from_json(const json& j, TextureBinding& b) {
        j.at("channel").get_to(b.channelIndex);
        j.at("enabled").get_to(b.enabled);
        j.at("bindType").get_to(b.bindingType);
        j.at("sourceIndex").get_to(b.sourceSceneIndex);
        j.at("path").get_to(b.filePath);
        j.at("type").get_to(b.type);
    }

    void to_json(json& j, const Scene::PostFXEffect& e) {
        j = json{
            {"name", e.name},
            {"enabled", e.enabled}
        };
        if (e.shaderCodePath.empty()) {
            j["code"] = e.shaderCode;
        }
        if (!e.shaderCodePath.empty()) j["codePath"] = e.shaderCodePath;
        if (!e.precompiledPath.empty()) j["precompiled"] = e.precompiledPath;
    }

    void from_json(const json& j, Scene::PostFXEffect& e) {
        j.at("name").get_to(e.name);
        if (j.contains("code")) j.at("code").get_to(e.shaderCode);
        if (j.contains("codePath")) j.at("codePath").get_to(e.shaderCodePath);
        if (j.contains("enabled")) j.at("enabled").get_to(e.enabled);
        if (j.contains("precompiled")) j.at("precompiled").get_to(e.precompiledPath);
        e.isDirty = true;
        e.pipelineState = nullptr;
        e.historyIndex = 0;
        e.historyInitialized = false;
        e.historyTextures.clear();
    }

    void to_json(json& j, const Scene::ComputeEffect& e) {
        j = json{
            {"name", e.name},
            {"type", static_cast<int>(e.type)},
            {"enabled", e.enabled},
            {"threadGroupX", e.threadGroupX},
            {"threadGroupY", e.threadGroupY},
            {"param0", e.param0},
            {"param1", e.param1},
            {"param2", e.param2},
            {"param3", e.param3}
        };
        if (e.shaderCodePath.empty()) {
            j["code"] = e.shaderCode;
        }
        if (!e.shaderCodePath.empty()) j["codePath"] = e.shaderCodePath;
        if (!e.precompiledPath.empty()) j["precompiled"] = e.precompiledPath;
        if (e.entryPoint != "main") j["entryPoint"] = e.entryPoint;
        if (e.historyCount > 0) j["historyCount"] = e.historyCount;
    }

    void from_json(const json& j, Scene::ComputeEffect& e) {
        j.at("name").get_to(e.name);
        if (j.contains("type")) {
            int t; j.at("type").get_to(t);
            e.type = static_cast<Scene::ComputeEffect::Type>(t);
        }
        if (j.contains("code")) j.at("code").get_to(e.shaderCode);
        if (j.contains("codePath")) j.at("codePath").get_to(e.shaderCodePath);
        if (j.contains("precompiled")) j.at("precompiled").get_to(e.precompiledPath);
        if (j.contains("enabled")) j.at("enabled").get_to(e.enabled);
        if (j.contains("entryPoint")) j.at("entryPoint").get_to(e.entryPoint);
        if (j.contains("threadGroupX")) j.at("threadGroupX").get_to(e.threadGroupX);
        if (j.contains("threadGroupY")) j.at("threadGroupY").get_to(e.threadGroupY);
        if (j.contains("threadGroupZ")) j.at("threadGroupZ").get_to(e.threadGroupZ);
        if (j.contains("param0")) j.at("param0").get_to(e.param0);
        if (j.contains("param1")) j.at("param1").get_to(e.param1);
        if (j.contains("param2")) j.at("param2").get_to(e.param2);
        if (j.contains("param3")) j.at("param3").get_to(e.param3);
        if (j.contains("historyCount")) {
            j.at("historyCount").get_to(e.historyCount);
        } else {
            const std::string lowerCode = ToLower(e.shaderCode);
            const bool looksTemporal = (e.type == Scene::ComputeEffect::Type::Temporal) ||
                (lowerCode.find("historytexture") != std::string::npos) ||
                (lowerCode.find("register(t1)") != std::string::npos);
            e.historyCount = looksTemporal ? 1 : 0;
        }
        e.isDirty = true;
        e.pipelineState = nullptr;
        e.historyIndex = 0;
        e.historyInitialized = false;
        e.historyTextures.clear();
    }

    void to_json(json& j, const Scene& s) {
        j = json{
            {"name", s.name},
            {"bindings", s.bindings},
            {"outputType", s.outputType}
        };
        if (s.shaderCodePath.empty()) {
            j["code"] = s.shaderCode;
        }
        if (!s.description.empty()) j["description"] = s.description;
        if (!s.shaderCodePath.empty()) j["codePath"] = s.shaderCodePath;
        if (!s.postFxChain.empty()) j["postfx"] = s.postFxChain;
        if (!s.computeEffectChain.empty()) j["compute"] = s.computeEffectChain;
        if (!s.precompiledPath.empty()) j["precompiled"] = s.precompiledPath;
    }

    void from_json(const json& j, Scene& s) {
        j.at("name").get_to(s.name);
        if (j.contains("description")) j.at("description").get_to(s.description);
        if (j.contains("code")) j.at("code").get_to(s.shaderCode);
        if (j.contains("codePath")) j.at("codePath").get_to(s.shaderCodePath);
        j.at("bindings").get_to(s.bindings);
        if(j.contains("outputType")) j.at("outputType").get_to(s.outputType);
        if(j.contains("postfx")) j.at("postfx").get_to(s.postFxChain);
        if(j.contains("compute")) j.at("compute").get_to(s.computeEffectChain);
        if(j.contains("precompiled")) j.at("precompiled").get_to(s.precompiledPath);
    }

    void to_json(json& j, const AudioClip& a) {
        j = json{
            {"name", a.name},
            {"path", a.path},
            {"bpm", a.bpm},
            {"type", (int)a.type}
        };
    }

    void from_json(const json& j, AudioClip& a) {
        j.at("name").get_to(a.name);
        j.at("path").get_to(a.path);
        j.at("bpm").get_to(a.bpm);
        int t; j.at("type").get_to(t); a.type = (AudioType)t;
    }

    void to_json(json& j, const TrackerRow& r) {
        j = json{
            {"id", r.rowId},
            {"scene", r.sceneIndex},
            {"transStem", r.transitionPresetStem},
            {"dur", r.transitionDuration},
            {"offset", r.timeOffset},
            {"music", r.musicIndex},
            {"oneshot", r.oneShotIndex},
            {"stop", r.stop}
        };
        if (!r.transitionShaderPath.empty()) {
            j["transPath"] = r.transitionShaderPath;
        }
    }

    void from_json(const json& j, TrackerRow& r) {
        j.at("id").get_to(r.rowId);
        j.at("scene").get_to(r.sceneIndex);
        if (j.contains("transStem")) {
            j.at("transStem").get_to(r.transitionPresetStem);
        } else {
            r.transitionPresetStem.clear();
        }
        if (j.contains("transPath")) {
            j.at("transPath").get_to(r.transitionShaderPath);
        } else if (j.contains("transCode")) {
            r.transitionShaderPath.clear();
        } else {
            r.transitionShaderPath.clear();
        }
        j.at("dur").get_to(r.transitionDuration);
        if(j.contains("offset")) j.at("offset").get_to(r.timeOffset);
        j.at("music").get_to(r.musicIndex);
        j.at("oneshot").get_to(r.oneShotIndex);
        if(j.contains("stop")) j.at("stop").get_to(r.stop);
    }

    void to_json(json& j, const DemoTrack& t) {
        j = json{
            {"name", t.name},
            {"bpm", t.bpm},
            {"len", t.lengthBeats},
            {"rows", t.rows}
        };
    }

    void from_json(const json& j, DemoTrack& t) {
        j.at("name").get_to(t.name);
        j.at("bpm").get_to(t.bpm);
        j.at("len").get_to(t.lengthBeats);
        j.at("rows").get_to(t.rows);
    }

    void to_json(json& j, const ProjectData& p) {
        j = json{
            {"scenes", p.scenes},
            {"audio", p.audioLibrary},
            {"track", p.track},
            {"bpm", p.transport.bpm},
            {"renderAspect", p.renderAspectRatioPreset},
            {"fullscreenRenderResolution", p.fullscreenRenderResolutionPreset}
        };
        if (!p.demoTitle.empty()) j["demoTitle"] = p.demoTitle;
        if (!p.demoAuthor.empty()) j["demoAuthor"] = p.demoAuthor;
        if (!p.demoDescription.empty()) j["demoDescription"] = p.demoDescription;
    }
    
    void from_json(const json& j, ProjectData& p) {
        j.at("scenes").get_to(p.scenes);
        j.at("audio").get_to(p.audioLibrary);
        j.at("track").get_to(p.track);
        if(j.contains("bpm")) j.at("bpm").get_to(p.transport.bpm);
        if (j.contains("renderAspect")) {
            j.at("renderAspect").get_to(p.renderAspectRatioPreset);
        } else {
            p.renderAspectRatioPreset = RenderAspectRatioPreset::Ratio_16_9;
        }
        if (j.contains("fullscreenRenderResolution")) {
            j.at("fullscreenRenderResolution").get_to(p.fullscreenRenderResolutionPreset);
        } else {
            p.fullscreenRenderResolutionPreset = FullscreenRenderResolutionPreset::Full;
        }
        if(j.contains("demoTitle")) j.at("demoTitle").get_to(p.demoTitle);
        if(j.contains("demoAuthor")) j.at("demoAuthor").get_to(p.demoAuthor);
        if(j.contains("demoDescription")) j.at("demoDescription").get_to(p.demoDescription);
    }

namespace Serializer {

    bool SaveProject(const ProjectData& project, const std::string& filepath) {
        json j = project;
        std::ofstream o(filepath);
        if (!o.is_open()) return false;
        o << j.dump(4);
        return true;
    }

    bool LoadProjectFromJson(const std::string& jsonContent, ProjectData& outProject) {
        try {
            json j = json::parse(jsonContent);
            outProject = j.get<ProjectData>();
            return true;
        } catch (...) {
            return false;
        }
    }

    bool LoadProject(const std::string& filepath, ProjectData& outProject) {
        std::ifstream i(filepath);
        if (!i.is_open()) return false;
        try {
            json j;
            i >> j;
            outProject = j.get<ProjectData>();
            const fs::path projectDirectory = fs::path(filepath).parent_path();
            ResolveLinkedShaderCode(outProject, projectDirectory);
            ResolveLinkedAssetPaths(outProject, projectDirectory);
            return true;
        } catch (const std::exception& e) {
            std::cerr << "Serializer::LoadProject JSON error: " << e.what() << "\n";
            return false;
        } catch (...) {
            return false;
        }
    }

    bool ExportProject(const ProjectData& inputProject, const std::string& outputFile) {
        ProjectData p = inputProject; // Copy to modify paths
        
        fs::path outPath(outputFile);
        fs::path baseDir = outPath.parent_path();
        fs::path assetsDir = baseDir / "assets";

        if (!fs::exists(assetsDir)) {
            fs::create_directories(assetsDir);
        }

        // 1. Copy Audio
        for (auto& clip : p.audioLibrary) {
            if (fs::exists(clip.path)) {
                fs::path originalPath(clip.path);
                fs::path fileName = originalPath.filename();
                fs::path newPath = assetsDir / fileName;
                
                // Copy
                if (fs::exists(newPath)) fs::remove(newPath); // Overwrite
                fs::copy_file(clip.path, newPath);

                // Rebase
                clip.path = "assets/" + fileName.string();
            }
        }

        // 2. Copy Texture Bindings & Precompiled Shaders
        for (auto& scene : p.scenes) {
            // Textures
            for (auto& bind : scene.bindings) {
                if (bind.bindingType == BindingType::File && !bind.filePath.empty()) {
                    if (fs::exists(bind.filePath)) {
                         fs::path originalPath(bind.filePath);
                         fs::path fileName = originalPath.filename();
                         fs::path newPath = assetsDir / fileName;
                         
                         if(fs::exists(newPath)) fs::remove(newPath);
                         fs::copy_file(bind.filePath, newPath);
                         
                         bind.filePath = "assets/" + fileName.string();
                    }
                }
            }
            // Precompiled Shader
            if (!scene.precompiledPath.empty()) {
                if (fs::exists(scene.precompiledPath)) {
                    fs::path originalPath(scene.precompiledPath);
                    fs::path fileName = originalPath.filename();
                    fs::path newPath = assetsDir / fileName;
                    
                    if(fs::exists(newPath)) fs::remove(newPath);
                    fs::copy_file(scene.precompiledPath, newPath);
                    
                    scene.precompiledPath = "assets/" + fileName.string();
                }
            }

            // Precompiled Post FX
            for (auto& fx : scene.postFxChain) {
                if (!fx.precompiledPath.empty() && fs::exists(fx.precompiledPath)) {
                    fs::path originalPath(fx.precompiledPath);
                    fs::path fileName = originalPath.filename();
                    fs::path newPath = assetsDir / fileName;

                    if (fs::exists(newPath)) fs::remove(newPath);
                    fs::copy_file(fx.precompiledPath, newPath);

                    fx.precompiledPath = "assets/" + fileName.string();
                }
            }
        }

        return SaveProject(p, outputFile);
    }

    bool ConsolidateProject(ProjectData& project, const std::string& rootPath) {
        fs::path baseDir(rootPath);
        fs::path assetsDir = baseDir / "assets";

        if (!fs::exists(assetsDir)) {
            // Create assets folder if it doesn't exist
            // Also suggest creating subfolders for organization?
            fs::create_directories(assetsDir);
            fs::create_directories(assetsDir / "audio");
            fs::create_directories(assetsDir / "textures");
            fs::create_directories(assetsDir / "shaders"); // For precompiled
        } else {
             if(!fs::exists(assetsDir / "audio")) fs::create_directories(assetsDir / "audio");
             if(!fs::exists(assetsDir / "textures")) fs::create_directories(assetsDir / "textures");
             if(!fs::exists(assetsDir / "shaders")) fs::create_directories(assetsDir / "shaders");
        }

        auto normalizePath = [](const std::string& p) -> std::string {
             std::string s = p;
               return NormalizePathSlashes(s);
        };

        auto isInside = [&](const fs::path& p, const fs::path& root) {
            std::string pStr = normalizePath(fs::absolute(p).string());
            std::string rStr = normalizePath(fs::absolute(root).string());
            return pStr.find(rStr) == 0;
        };

        // 1. Audio
        for (auto& clip : project.audioLibrary) {
            fs::path p(clip.path);
            if (!p.is_absolute() && !fs::exists(p)) {
                // Try resolving relative to CWD if not found? 
                // Or assume assumes CWD is current run location
            }

            // Check if absolute and inside?
            // If absolute and NOT inside root, copy.
            // If relative, assume it's already inside (or we can't find it easily unless we know where it came from, 
            // but we assume current session has valid paths).
            
            // Logic: If path exists at absolute location and is NOT inside root -> Copy.
            if (fs::exists(p) && p.is_absolute()) {
                if (!isInside(p, baseDir)) {
                    fs::path fileName = p.filename();
                    fs::path newPath = assetsDir / "audio" / fileName;
                    
                    // Avoid self-overwrite if somehow resolved same
                    if (fs::absolute(p) != fs::absolute(newPath)) {
                        fs::copy_file(p, newPath, fs::copy_options::overwrite_existing);
                    }
                    clip.path = "assets/audio/" + fileName.string();
                } else {
                     // Inside root, ensure relative path stored
                     // fs::relative available in C++17
                     clip.path = fs::relative(p, baseDir).string();
                }
            }
             clip.path = normalizePath(clip.path);
        }

        // 2. Textures & Shaders
        for (auto& scene : project.scenes) {
            for (auto& bind : scene.bindings) {
                if (bind.bindingType == BindingType::File && !bind.filePath.empty()) {
                    fs::path p(bind.filePath);
                    if (fs::exists(p) && p.is_absolute()) {
                         if (!isInside(p, baseDir)) {
                             fs::path fileName = p.filename();
                             fs::path newPath = assetsDir / "textures" / fileName;
                             if (fs::absolute(p) != fs::absolute(newPath)) {
                                 fs::copy_file(p, newPath, fs::copy_options::overwrite_existing);
                             }
                             bind.filePath = "assets/textures/" + fileName.string();
                         } else {
                             bind.filePath = fs::relative(p, baseDir).string();
                         }
                    }
                    bind.filePath = normalizePath(bind.filePath);
                }
            }

            if (!scene.precompiledPath.empty()) {
                fs::path p(scene.precompiledPath);
                if (fs::exists(p) && p.is_absolute()) {
                     if (!isInside(p, baseDir)) {
                         fs::path fileName = p.filename();
                         fs::path newPath = assetsDir / "shaders" / fileName;
                          if (fs::absolute(p) != fs::absolute(newPath)) {
                                 fs::copy_file(p, newPath, fs::copy_options::overwrite_existing);
                          }
                         scene.precompiledPath = "assets/shaders/" + fileName.string();
                     } else {
                         scene.precompiledPath = fs::relative(p, baseDir).string();
                     }
                }
                scene.precompiledPath = normalizePath(scene.precompiledPath);
            }

            for (auto& fx : scene.postFxChain) {
                if (!fx.precompiledPath.empty()) {
                    fs::path p(fx.precompiledPath);
                    if (fs::exists(p) && p.is_absolute()) {
                        if (!isInside(p, baseDir)) {
                            fs::path fileName = p.filename();
                            fs::path newPath = assetsDir / "shaders" / fileName;
                            if (fs::absolute(p) != fs::absolute(newPath)) {
                                fs::copy_file(p, newPath, fs::copy_options::overwrite_existing);
                            }
                            fx.precompiledPath = "assets/shaders/" + fileName.string();
                        } else {
                            fx.precompiledPath = fs::relative(p, baseDir).string();
                        }
                    }
                    fx.precompiledPath = normalizePath(fx.precompiledPath);
                }
            }
        }
        
        return true;
    }
    
    bool PackExecutable(const std::string& sourceExe,
                        const std::string& outputExe,
                        const std::string& projectJsonPath,
                        const std::vector<PackedExtraFile>& extraFiles,
                        bool includeProjectManifest) {
        // 1. Read Source EXE
        std::ifstream src(sourceExe, std::ios::binary);
        if (!src.is_open()) return false;

        std::vector<uint8_t> exeData = ReadStreamBytes(src);

        // 2. Load Project to find assets
        ProjectData project;
        const bool hasProjectJsonPath = !projectJsonPath.empty();
        const bool loadProjectAssets = hasProjectJsonPath && includeProjectManifest;
        if (loadProjectAssets && !LoadProject(projectJsonPath, project)) return false;

        // 3. Prepare Pack Data
        const bool compressPackedEntries = !includeProjectManifest;
        ExecutablePackAccumulator packAccumulator(compressPackedEntries);
        packAccumulator.TryAddProjectManifest(projectJsonPath, includeProjectManifest, hasProjectJsonPath);

        const fs::path projectRoot = hasProjectJsonPath ? fs::path(projectJsonPath).parent_path() : fs::path();

        if (loadProjectAssets) {
            packAccumulator.AddProjectAssets(project, projectRoot);
        }
        packAccumulator.AddExtraFiles(extraFiles);

        // 4. Create Directory Blob
        std::vector<uint8_t> dirBlob = BuildDirectoryBlob(packAccumulator.entries, exeData.size());

        // 5. Write Output EXE
        // Layout: [EXE][PACK_DATA][DIRECTORY_BLOB][DIR_OFFSET_U64][MAGIC]
        if (!EnsureOutputDirectory(outputExe)) {
            return false;
        }

        return WritePackedExecutable(outputExe, exeData, packAccumulator.packBlob, dirBlob);
    }

    bool PackExecutable(const std::string& sourceExe, const std::string& outputExe, const std::string& projectJsonPath, const std::vector<PackedExtraFile>& extraFiles) {
        return PackExecutable(sourceExe, outputExe, projectJsonPath, extraFiles, true);
    }

    bool PackExecutable(const std::string& sourceExe, const std::string& outputExe, const std::string& projectJsonPath) {
        return PackExecutable(sourceExe, outputExe, projectJsonPath, {}, true);
    }


}
}
