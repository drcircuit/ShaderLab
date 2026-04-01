#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ShaderLab {

namespace {
namespace fs = std::filesystem;
using json = nlohmann::json;
using EditorActionWidgets::LabeledActionButton;

float Clamp01(float value) {
    return (std::max)(0.0f, (std::min)(1.0f, value));
}

ImVec4 EvaluateIqPaletteColor(float t,
                              const float* a,
                              const float* b,
                              const float* c,
                              const float* d) {
    constexpr float kTau = 6.28318530718f;
    std::array<float, 3> out = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 3; ++i) {
        const float wave = std::cos(kTau * (c[i] * t + d[i]));
        out[i] = Clamp01(a[i] + b[i] * wave);
    }
    return ImVec4(out[0], out[1], out[2], 1.0f);
}

struct PalettePreset {
    std::string name;
    std::array<float, 3> a;
    std::array<float, 3> b;
    std::array<float, 3> c;
    std::array<float, 3> d;
};

std::vector<PalettePreset> GetDefaultPresets() {
    return {
        { "Sunset",    { 0.50f, 0.38f, 0.26f }, { 0.50f, 0.35f, 0.25f }, { 1.00f, 1.00f, 1.00f }, { 0.00f, 0.12f, 0.25f } },
        { "Neon",      { 0.50f, 0.50f, 0.50f }, { 0.50f, 0.50f, 0.50f }, { 1.00f, 1.00f, 1.00f }, { 0.00f, 0.33f, 0.67f } },
        { "Ocean",     { 0.30f, 0.45f, 0.60f }, { 0.25f, 0.35f, 0.40f }, { 1.00f, 1.00f, 1.00f }, { 0.20f, 0.25f, 0.35f } },
        { "Forest",    { 0.28f, 0.45f, 0.25f }, { 0.20f, 0.30f, 0.20f }, { 1.00f, 1.00f, 1.00f }, { 0.08f, 0.18f, 0.24f } },
        { "Candy",     { 0.60f, 0.40f, 0.58f }, { 0.40f, 0.35f, 0.42f }, { 1.00f, 1.00f, 1.00f }, { 0.00f, 0.20f, 0.40f } },
        { "Ice",       { 0.60f, 0.72f, 0.78f }, { 0.20f, 0.25f, 0.30f }, { 1.00f, 1.00f, 1.00f }, { 0.18f, 0.28f, 0.38f } },
        { "Fire",      { 0.52f, 0.28f, 0.12f }, { 0.50f, 0.35f, 0.12f }, { 1.10f, 1.00f, 0.90f }, { 0.00f, 0.05f, 0.10f } },
        { "Grayscale", { 0.50f, 0.50f, 0.50f }, { 0.45f, 0.45f, 0.45f }, { 1.00f, 1.00f, 1.00f }, { 0.00f, 0.00f, 0.00f } }
    };
}

struct PalettePresetStore {
    bool loaded = false;
    fs::path path;
    std::vector<PalettePreset> presets;
};

PalettePresetStore g_paletteStore;

std::array<float, 3> ParseVec3(const json& value, const std::array<float, 3>& fallback) {
    std::array<float, 3> out = fallback;
    if (!value.is_array() || value.size() != 3) {
        return out;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (value[i].is_number()) {
            out[i] = value[i].get<float>();
        }
    }
    return out;
}

void EnsurePalettePresetStoreLoaded(const std::string& workspaceRoot, const std::string& appRoot) {
    if (g_paletteStore.loaded) {
        return;
    }

    std::error_code ec;
    const fs::path workspacePath = fs::path(workspaceRoot) / "presets" / "palette" / "presets.json";
    const fs::path backupPath = fs::path(appRoot) / "editor_assets" / "presets" / "palette" / "presets.json";

    fs::create_directories(workspacePath.parent_path(), ec);
    if (!fs::exists(workspacePath, ec) && fs::exists(backupPath, ec)) {
        fs::copy_file(backupPath, workspacePath, fs::copy_options::none, ec);
    }

    g_paletteStore.path = workspacePath;
    g_paletteStore.presets = GetDefaultPresets();

    std::ifstream in(workspacePath);
    if (in.is_open()) {
        try {
            json root;
            in >> root;
            if (root.contains("presets") && root["presets"].is_array()) {
                std::vector<PalettePreset> loaded;
                for (const auto& item : root["presets"]) {
                    if (!item.is_object()) {
                        continue;
                    }
                    PalettePreset preset;
                    preset.name = item.value("name", std::string("Preset"));
                    if (preset.name.empty()) {
                        continue;
                    }
                    preset.a = ParseVec3(item["a"], { 0.5f, 0.5f, 0.5f });
                    preset.b = ParseVec3(item["b"], { 0.5f, 0.5f, 0.5f });
                    preset.c = ParseVec3(item["c"], { 1.0f, 1.0f, 1.0f });
                    preset.d = ParseVec3(item["d"], { 0.0f, 0.33f, 0.67f });
                    loaded.push_back(std::move(preset));
                }
                if (!loaded.empty()) {
                    g_paletteStore.presets = std::move(loaded);
                }
            }
        } catch (...) {
        }
    }

    g_paletteStore.loaded = true;
}

bool SavePalettePresetStore() {
    if (g_paletteStore.path.empty()) {
        return false;
    }

    json root;
    root["version"] = 1;
    root["presets"] = json::array();
    for (const auto& preset : g_paletteStore.presets) {
        root["presets"].push_back({
            { "name", preset.name },
            { "a", { preset.a[0], preset.a[1], preset.a[2] } },
            { "b", { preset.b[0], preset.b[1], preset.b[2] } },
            { "c", { preset.c[0], preset.c[1], preset.c[2] } },
            { "d", { preset.d[0], preset.d[1], preset.d[2] } }
        });
    }

    std::error_code ec;
    fs::create_directories(g_paletteStore.path.parent_path(), ec);
    std::ofstream out(g_paletteStore.path);
    if (!out.is_open()) {
        return false;
    }
    out << root.dump(2);
    return true;
}

void DrawPaletteStrip(ImDrawList* drawList,
                      const ImVec2& minPos,
                      const ImVec2& maxPos,
                      const float* a,
                      const float* b,
                      const float* c,
                      const float* d) {
    const int sampleCount = 96;
    for (int i = 0; i < sampleCount; ++i) {
        const float t0 = static_cast<float>(i) / static_cast<float>(sampleCount);
        const float t1 = static_cast<float>(i + 1) / static_cast<float>(sampleCount);
        const float x0 = minPos.x + t0 * (maxPos.x - minPos.x);
        const float x1 = minPos.x + t1 * (maxPos.x - minPos.x);
        const ImVec4 color0 = EvaluateIqPaletteColor(t0, a, b, c, d);
        drawList->AddRectFilled(ImVec2(x0, minPos.y), ImVec2(x1, maxPos.y), ImGui::ColorConvertFloat4ToU32(color0));
    }
    drawList->AddRect(minPos, maxPos, ImGui::GetColorU32(ImGuiCol_Border));
}

std::string BuildBakedPaletteFunction(const char* functionName,
                                      const float* a,
                                      const float* b,
                                      const float* c,
                                      const float* d) {
    const std::string safeName = (functionName && functionName[0] != '\0')
        ? std::string(functionName)
        : std::string("PaletteGenerated");

    char buffer[1024] = {};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "float3 %s(float t)\n"
        "{\n"
        "    const float3 a = float3(%.6ff, %.6ff, %.6ff);\n"
        "    const float3 b = float3(%.6ff, %.6ff, %.6ff);\n"
        "    const float3 c = float3(%.6ff, %.6ff, %.6ff);\n"
        "    const float3 d = float3(%.6ff, %.6ff, %.6ff);\n"
        "    return a + b * cos(6.2831853f * (c * t + d));\n"
        "}\n",
        safeName.c_str(),
        a[0], a[1], a[2],
        b[0], b[1], b[2],
        c[0], c[1], c[2],
        d[0], d[1], d[2]);
    return std::string(buffer);
}

} // namespace

void ShaderLabIDE::ShowPaletteGeneratorPanel() {
    EnsurePalettePresetStoreLoaded(m_workspaceRootPath, m_appRoot);

    if (!ImGui::Begin("Palette Generator")) {
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("IQ Palette Function");
    ImGui::InputText("Function", m_paletteFunctionName, sizeof(m_paletteFunctionName));

    if (g_paletteStore.presets.empty()) {
        g_paletteStore.presets = GetDefaultPresets();
    }

    if (m_palettePresetIndex < 0 || m_palettePresetIndex >= (int)g_paletteStore.presets.size()) {
        m_palettePresetIndex = 0;
    }

    if (m_palettePresetName[0] == '\0') {
        std::snprintf(m_palettePresetName, sizeof(m_palettePresetName), "%s", g_paletteStore.presets[m_palettePresetIndex].name.c_str());
    }

    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Preset Name", m_palettePresetName, sizeof(m_palettePresetName));
    if (LabeledActionButton("AddPalettePreset", OpenFontIcons::kPlus, "Add Preset", "Save current A/B/C/D as preset", ImVec2(140.0f, 0.0f))) {
        std::string presetName = m_palettePresetName;
        presetName.erase(std::remove_if(presetName.begin(), presetName.end(), [](unsigned char ch) { return ch == '\r' || ch == '\n'; }), presetName.end());
        if (presetName.empty()) {
            m_paletteGeneratorStatus = "Preset name is required.";
        } else {
            PalettePreset newPreset;
            newPreset.name = presetName;
            newPreset.a = { m_paletteA[0], m_paletteA[1], m_paletteA[2] };
            newPreset.b = { m_paletteB[0], m_paletteB[1], m_paletteB[2] };
            newPreset.c = { m_paletteC[0], m_paletteC[1], m_paletteC[2] };
            newPreset.d = { m_paletteD[0], m_paletteD[1], m_paletteD[2] };

            int foundIndex = -1;
            for (int i = 0; i < (int)g_paletteStore.presets.size(); ++i) {
                if (g_paletteStore.presets[i].name == presetName) {
                    foundIndex = i;
                    break;
                }
            }

            if (foundIndex >= 0) {
                g_paletteStore.presets[foundIndex] = newPreset;
                m_palettePresetIndex = foundIndex;
            } else {
                g_paletteStore.presets.push_back(newPreset);
                m_palettePresetIndex = static_cast<int>(g_paletteStore.presets.size()) - 1;
            }

            if (SavePalettePresetStore()) {
                m_paletteGeneratorStatus = (foundIndex >= 0)
                    ? std::string("Updated preset: ") + presetName
                    : std::string("Added preset: ") + presetName;
            } else {
                m_paletteGeneratorStatus = "Failed to save preset file.";
            }
        }
    }

    ImGui::SameLine();
    if (LabeledActionButton("RenamePalettePreset", OpenFontIcons::kRefresh, "Rename Preset", "Rename selected preset to current Preset Name", ImVec2(150.0f, 0.0f))) {
        std::string presetName = m_palettePresetName;
        presetName.erase(std::remove_if(presetName.begin(), presetName.end(), [](unsigned char ch) { return ch == '\r' || ch == '\n'; }), presetName.end());

        if (presetName.empty()) {
            m_paletteGeneratorStatus = "Preset name is required.";
        } else if (m_palettePresetIndex < 0 || m_palettePresetIndex >= (int)g_paletteStore.presets.size()) {
            m_paletteGeneratorStatus = "No preset selected.";
        } else {
            int nameOwner = -1;
            for (int i = 0; i < (int)g_paletteStore.presets.size(); ++i) {
                if (g_paletteStore.presets[i].name == presetName) {
                    nameOwner = i;
                    break;
                }
            }

            if (nameOwner >= 0 && nameOwner != m_palettePresetIndex) {
                m_paletteGeneratorStatus = "Preset name already exists.";
            } else {
                g_paletteStore.presets[m_palettePresetIndex].name = presetName;
                if (SavePalettePresetStore()) {
                    m_paletteGeneratorStatus = std::string("Renamed preset to: ") + presetName;
                } else {
                    m_paletteGeneratorStatus = "Failed to save preset file.";
                }
            }
        }
    }

    ImGui::SameLine();
    if (LabeledActionButton("DeletePalettePreset", OpenFontIcons::kDelete, "Delete Preset", "Delete selected preset", ImVec2(145.0f, 0.0f))) {
        if (g_paletteStore.presets.size() <= 1) {
            m_paletteGeneratorStatus = "At least one preset must remain.";
        } else if (m_palettePresetIndex < 0 || m_palettePresetIndex >= (int)g_paletteStore.presets.size()) {
            m_paletteGeneratorStatus = "No preset selected.";
        } else {
            const std::string removedName = g_paletteStore.presets[m_palettePresetIndex].name;
            g_paletteStore.presets.erase(g_paletteStore.presets.begin() + m_palettePresetIndex);
            if (m_palettePresetIndex >= (int)g_paletteStore.presets.size()) {
                m_palettePresetIndex = static_cast<int>(g_paletteStore.presets.size()) - 1;
            }

            std::snprintf(
                m_palettePresetName,
                sizeof(m_palettePresetName),
                "%s",
                g_paletteStore.presets[m_palettePresetIndex].name.c_str());

            if (SavePalettePresetStore()) {
                m_paletteGeneratorStatus = std::string("Deleted preset: ") + removedName;
            } else {
                m_paletteGeneratorStatus = "Failed to save preset file.";
            }
        }
    }

    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("Palette Preset", g_paletteStore.presets[m_palettePresetIndex].name.c_str())) {
        for (int i = 0; i < (int)g_paletteStore.presets.size(); ++i) {
            const PalettePreset& preset = g_paletteStore.presets[i];
            const bool isSelected = (i == m_palettePresetIndex);

            if (ImGui::Selectable(preset.name.c_str(), isSelected)) {
                m_palettePresetIndex = i;
                for (int channel = 0; channel < 3; ++channel) {
                    m_paletteA[channel] = preset.a[channel];
                    m_paletteB[channel] = preset.b[channel];
                    m_paletteC[channel] = preset.c[channel];
                    m_paletteD[channel] = preset.d[channel];
                }
                std::snprintf(m_palettePresetName, sizeof(m_palettePresetName), "%s", preset.name.c_str());
                m_paletteGeneratorStatus = std::string("Loaded preset: ") + preset.name;
            }

            const ImVec2 miniSize(150.0f, 12.0f);
            const ImVec2 miniMin = ImGui::GetCursorScreenPos();
            ImGui::Dummy(miniSize);
            const ImVec2 miniMax = ImVec2(miniMin.x + miniSize.x, miniMin.y + miniSize.y);
            DrawPaletteStrip(ImGui::GetWindowDrawList(), miniMin, miniMax, preset.a.data(), preset.b.data(), preset.c.data(), preset.d.data());

            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("A => Base Color");
    ImGui::ColorEdit3("##PaletteA", m_paletteA, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Float);

    ImGui::TextUnformatted("B => Amplitude / Vibrance");
    ImGui::SliderFloat3("##PaletteB", m_paletteB, -2.0f, 2.0f, "%.3f");

    ImGui::TextUnformatted("C => Frequency / Complexity");
    ImGui::SliderFloat3("##PaletteC", m_paletteC, 0.0f, 4.0f, "%.3f");

    ImGui::TextUnformatted("D => Phase / Hue Rotation");
    ImGui::SliderFloat3("##PaletteD", m_paletteD, -1.0f, 1.0f, "%.3f");

    ImGui::Separator();
    ImGui::TextUnformatted("Preview");
    const ImVec2 gradientSize = ImVec2(-1.0f, 36.0f);
    ImGui::InvisibleButton("##PaletteGradient", gradientSize);
    DrawPaletteStrip(
        ImGui::GetWindowDrawList(),
        ImGui::GetItemRectMin(),
        ImGui::GetItemRectMax(),
        m_paletteA,
        m_paletteB,
        m_paletteC,
        m_paletteD);

    if (LabeledActionButton("GeneratePalette", OpenFontIcons::kPlus, "Generate Palette", "Insert generated palette function", ImVec2(180.0f, 0.0f))) {
        const std::string generated = BuildBakedPaletteFunction(
            m_paletteFunctionName,
            m_paletteA,
            m_paletteB,
            m_paletteC,
            m_paletteD);
        InsertSnippetIntoEditor(generated);
        m_paletteGeneratorStatus = "Inserted generated palette function.";
    }

    if (!m_paletteGeneratorStatus.empty()) {
        ImGui::SameLine();
        ImGui::TextUnformatted(m_paletteGeneratorStatus.c_str());
    }

    ImGui::End();
}

} // namespace ShaderLab
