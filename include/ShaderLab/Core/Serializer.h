#pragma once

#include "ShaderLab/Core/ShaderLabData.h"
#include <string>

namespace ShaderLab {
namespace Serializer {

    struct PackedExtraFile {
        std::string sourcePath;
        std::string packedPath;
    };

    bool SaveProject(const ProjectData& project, const std::string& filepath);
    bool LoadProject(const std::string& filepath, ProjectData& outProject);
    bool LoadProjectFromJson(const std::string& jsonContent, ProjectData& outProject); // Helper

    // Asset packing helper: Copies referenced files to 'assets' subdir next to output file and rebases paths
    bool ExportProject(const ProjectData& project, const std::string& outputFile);

    // Consolidates assets into the project folder. Modifies paths in the project data to be relative.
    bool ConsolidateProject(ProjectData& project, const std::string& rootPath);
    
    // Packs an executable with assets
    bool PackExecutable(const std::string& sourceExe, const std::string& outputExe, const std::string& projectJsonPath);
    bool PackExecutable(const std::string& sourceExe, const std::string& outputExe, const std::string& projectJsonPath, const std::vector<PackedExtraFile>& extraFiles);
    bool PackExecutable(const std::string& sourceExe,
                        const std::string& outputExe,
                        const std::string& projectJsonPath,
                        const std::vector<PackedExtraFile>& extraFiles,
                        bool includeProjectManifest);

}
}
