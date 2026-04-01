#include "ShaderLab/DevKit/RuntimeExporter.h"

#include <filesystem>
#include <vector>

#include "ShaderLab/Core/Serializer.h"

namespace ShaderLab {

namespace fs = std::filesystem;

RuntimeExportResult RuntimeExporter::Export(const RuntimeExportRequest& request) {
    RuntimeExportResult result{};

    fs::path destExePath = request.destExePath;
    fs::path destDir = destExePath.parent_path();
    std::string destName = destExePath.stem().string();
    fs::path appRoot = request.appRoot;

    fs::path playerExe;
    if (fs::exists(appRoot / "ShaderLabPlayer.exe")) {
        playerExe = appRoot / "ShaderLabPlayer.exe";
    }

    if (!fs::exists(playerExe)) {
        fs::path buildReleaseBin = appRoot / "build_release" / "bin";
        fs::path buildDebugBin = appRoot / "build_debug" / "bin";
        fs::path buildBin = appRoot / "build" / "bin";
        if (fs::exists(buildReleaseBin / "ShaderLabPlayer.exe")) playerExe = buildReleaseBin / "ShaderLabPlayer.exe";
        else if (fs::exists(buildBin / "Release" / "ShaderLabPlayer.exe")) playerExe = buildBin / "Release" / "ShaderLabPlayer.exe";
        else if (fs::exists(buildBin / "ShaderLabPlayer.exe")) playerExe = buildBin / "ShaderLabPlayer.exe";
        else if (fs::exists(buildDebugBin / "ShaderLabPlayer.exe")) playerExe = buildDebugBin / "ShaderLabPlayer.exe";
        else if (fs::exists(buildBin / "Debug" / "ShaderLabPlayer.exe")) playerExe = buildBin / "Debug" / "ShaderLabPlayer.exe";
        else if (fs::exists(appRoot / "bin" / "ShaderLabPlayer.exe")) playerExe = appRoot / "bin" / "ShaderLabPlayer.exe";
    }

    if (!fs::exists(playerExe)) {
        result.message = "Could not locate ShaderLabPlayer.exe source.\nPlease ensure the project is built or you have the full component set.";
        return result;
    }

    try {
        fs::copy_file(playerExe, destExePath, fs::copy_options::overwrite_existing);

        std::vector<std::string> dlls = {"dxcompiler.dll", "dxil.dll"};
        for (const auto& dll : dlls) {
            fs::path src = playerExe.parent_path() / dll;
            if (!fs::exists(src)) src = appRoot / dll;
            if (!fs::exists(src)) src = appRoot / "build_release" / "bin" / dll;
            if (!fs::exists(src)) src = appRoot / "build" / "bin" / dll;

            if (fs::exists(src)) {
                fs::copy_file(src, destDir / dll, fs::copy_options::overwrite_existing);
            }
        }

        ProjectData data = request.data;
        if (Serializer::ConsolidateProject(data, destDir.string())) {
            fs::path jsonPath = destDir / (destName + ".json");
            Serializer::SaveProject(data, jsonPath.string());

            result.success = true;
            result.message = "Exported successfully to:\n" + destDir.string();
        } else {
            result.message = "Failed to consolidate assets.";
        }
    } catch (const std::exception& e) {
        result.message = e.what();
    }

    return result;
}

} // namespace ShaderLab
