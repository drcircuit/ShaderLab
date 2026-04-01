#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "ShaderLab/Core/ShaderLabData.h"

namespace ShaderLab {

struct ShaderPreset {
    std::string name;
    std::string code;
    std::string filePath;
    std::string stem;
};

extern const int kPostFxHistoryCount;
extern const int kMaxPostFxChain;
extern const int kAboutSrvIndex;

void InitializePresetService(const std::string& workspaceRoot, const std::string& appRoot);
void RefreshPresetService();

const std::vector<ShaderPreset>& GetScenePresets();
const std::vector<ShaderPreset>& GetPostFxPresets();
const std::vector<ShaderPreset>& GetComputePresets();
const std::vector<ShaderPreset>& GetTransitionPresets();

std::string GetScenePresetCodeByStem(const std::string& stem);
std::string GetPostFxPresetCodeByStem(const std::string& stem);
std::string GetComputePresetCodeByStem(const std::string& stem);
std::string GetTransitionPresetCodeByStem(const std::string& stem);

std::string GetTransitionDisplayNameByStem(const std::string& stem);

std::string GetEditorTransitionShaderSourceByStem(const std::string& stem);

} // namespace ShaderLab
