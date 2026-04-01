#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/ShaderLabIDECore/ActionWidgets.h"
#include "ShaderLab/UI/OpenFontIcons.h"
#include "ShaderLab/Graphics/Device.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <commdlg.h>
#pragma comment(lib, "Comdlg32.lib")

namespace ShaderLab {

namespace {
using EditorActionWidgets::LabeledActionButton;

}

void ShaderLabIDE::ShowSceneTexturesAndChannels() {
    if (ImGui::Begin("Scene: Textures & Channels")) {
        const ImGuiViewport* viewport = ImGui::GetWindowViewport();
        const float dpiScale = (viewport && viewport->DpiScale > 0.0f) ? viewport->DpiScale : 1.0f;
        namespace fs = std::filesystem;

        ImGui::Text("Textures & Channel Routing");
        ImGui::Separator();

        if (m_activeSceneIndex >= 0 && m_activeSceneIndex < (int)m_scenes.size()) {
            auto& scene = m_scenes[m_activeSceneIndex];

            auto findNextChannel = [&]() -> int {
                bool used[8] = {};
                for (const auto& b : scene.bindings) {
                    if (b.channelIndex >= 0 && b.channelIndex < 8) {
                        used[b.channelIndex] = true;
                    }
                }
                for (int c = 0; c < 8; ++c) {
                    if (!used[c]) return c;
                }
                return (int)scene.bindings.size() % 8;
            };

            auto browseAndAssignFileTexture = [&](TextureBinding& binding) {
                OPENFILENAMEA ofn = {};
                char szFile[260] = {};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = m_hwnd ? m_hwnd : (HWND)ImGui::GetMainViewport()->PlatformHandleRaw;
                ofn.lpstrFile = szFile;
                ofn.nMaxFile = sizeof(szFile);
                ofn.lpstrFilter = "Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files\0*.*\0";
                ofn.nFilterIndex = 1;
                ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

                if (GetOpenFileNameA(&ofn)) {
                    binding.filePath = ImportAssetIntoProject(szFile);
                    LoadTextureFromFile(binding.filePath, binding.textureResource);
                    binding.fileTextureValid = binding.textureResource != nullptr;
                }
            };

            ImFont* compactFont = m_fontMenuSmall
                ? m_fontMenuSmall
                : (m_fontOrbitronText ? m_fontOrbitronText : ImGui::GetFont());
            float compactFontSize = 0.0f;
            if (m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]) {
                compactFontSize = m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]->LegacySize;
            } else if (compactFont) {
                compactFontSize = compactFont->LegacySize;
            }
            if (compactFont && compactFontSize > 0.0f) {
                ImGui::PushFont(compactFont, compactFontSize);
            }

            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float minButtonWidth = 140.0f * dpiScale;
            const bool singleRowButtons = availableWidth >= (minButtonWidth * 2.0f + spacing);
            const float buttonWidth = singleRowButtons
                ? (availableWidth - spacing) * 0.5f
                : availableWidth;

            if (LabeledActionButton("AddFileTexture", OpenFontIcons::kFilePlus, "Add File Texture", "Add file texture", ImVec2(buttonWidth, 0.0f))) {
                TextureBinding binding;
                binding.enabled = true;
                binding.bindingType = BindingType::File;
                binding.channelIndex = findNextChannel();
                binding.type = TextureType::Texture2D;
                browseAndAssignFileTexture(binding);

                scene.bindings.push_back(binding);
            }
            if (singleRowButtons) {
                ImGui::SameLine();
            }
            if (LabeledActionButton("AddSceneSampler", OpenFontIcons::kPlus, "Add Scene", "Add scene sampler", ImVec2(buttonWidth, 0.0f))) {
                TextureBinding binding;
                binding.enabled = true;
                binding.bindingType = BindingType::Scene;
                binding.channelIndex = findNextChannel();
                binding.type = TextureType::Texture2D;

                int defaultScene = -1;
                for (int i = 0; i < (int)m_scenes.size(); ++i) {
                    if (i != m_activeSceneIndex) {
                        defaultScene = i;
                        break;
                    }
                }
                binding.sourceSceneIndex = defaultScene;
                scene.bindings.push_back(binding);
            }

            if (compactFont && compactFontSize > 0.0f) {
                ImGui::PopFont();
            }

            ImGui::Separator();

            ImGui::Text("Active Scene: %s", scene.name.c_str());
            ImGui::Separator();

            UINT handleStep = 0;
            D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = {0};
            D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = {0};
            int thumbnailIdx = 2;

            if (m_deviceRef && m_srvHeap) {
                 handleStep = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                 cpuStart = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
                 gpuStart = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
            }

            const float thumbnailSize = 44.0f * dpiScale;

            ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX;
            const bool pushedTableFont = compactFont && compactFontSize > 0.0f;
            if (pushedTableFont) {
                ImGui::PushFont(compactFont, compactFontSize);
            }

            if (ImGui::BeginTable("TextureBindings", 5, tableFlags)) {
                ImGui::TableSetupColumn("Preview", ImGuiTableColumnFlags_WidthFixed, 68.0f * dpiScale);
                ImGui::TableSetupColumn("Ch", ImGuiTableColumnFlags_WidthFixed, 40.0f * dpiScale);
                ImGui::TableSetupColumn("Source", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 48.0f * dpiScale);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f * dpiScale);
                ImGui::TableHeadersRow();

                int bindingToRemove = -1;
                for(int i=0; i<(int)scene.bindings.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::PushID(i);
                    auto& binding = scene.bindings[i];
                    const bool isSelfSample =
                        (binding.bindingType == BindingType::Scene) &&
                        (binding.sourceSceneIndex == m_activeSceneIndex);

                    if (isSelfSample) {
                        ImVec4 warnBg = m_uiThemeColors.TrackerAccentBeatBackground;
                        warnBg.w = 0.28f;
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(warnBg));
                    }

                    ImTextureID texID = (ImTextureID)0;
                    if (binding.enabled && m_deviceRef && m_srvHeap) {
                        ID3D12Resource* res = nullptr;
                        if (binding.bindingType == BindingType::File) {
                            res = binding.textureResource.Get();
                        } else if (binding.bindingType == BindingType::Scene && binding.sourceSceneIndex != -1) {
                            if (binding.sourceSceneIndex >= 0 && binding.sourceSceneIndex < (int)m_scenes.size()) {
                                res = m_scenes[binding.sourceSceneIndex].texture.Get();
                            }
                        }

                        if (res) {
                            D3D12_CPU_DESCRIPTOR_HANDLE dest = cpuStart;
                            dest.ptr += thumbnailIdx * handleStep;

                            D3D12_RESOURCE_DESC desc = res->GetDesc();
                            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                            srvDesc.Format = desc.Format;
                            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

                            if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D) {
                                if (desc.DepthOrArraySize == 6) {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
                                    srvDesc.TextureCube.MipLevels = 1;
                                } else {
                                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                                    srvDesc.Texture2D.MipLevels = 1;
                                }
                            } else if (desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) {
                                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
                                srvDesc.Texture3D.MipLevels = 1;
                            }

                            m_deviceRef->GetDevice()->CreateShaderResourceView(res, &srvDesc, dest);
                            texID = (ImTextureID)(gpuStart.ptr + thumbnailIdx * handleStep);
                            thumbnailIdx++;
                        }
                    }

                    ImGui::TableSetColumnIndex(0);
                    if (texID) {
                        ImGui::Image(texID, ImVec2(thumbnailSize, thumbnailSize));
                    } else {
                        ImGui::Dummy(ImVec2(thumbnailSize, thumbnailSize));
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    if (binding.enabled) {
                        PushNumericFont();
                        ImGui::Text("%d", binding.channelIndex);
                        PopNumericFont();
                    } else {
                        ImGui::TextDisabled("%d", binding.channelIndex);
                    }

                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    std::string sourceLabel;
                    if (binding.bindingType == BindingType::Scene) {
                        if (binding.sourceSceneIndex >= 0 && binding.sourceSceneIndex < (int)m_scenes.size()) {
                            sourceLabel = "Scene " + std::to_string(binding.sourceSceneIndex) + ": " + m_scenes[binding.sourceSceneIndex].name;
                        } else {
                            sourceLabel = "Scene: (None)";
                        }
                    } else {
                        if (!binding.filePath.empty()) {
                            sourceLabel = fs::path(binding.filePath).filename().string();
                        }
                        if (sourceLabel.empty()) {
                            sourceLabel = "File: (None)";
                        }
                    }
                    if (binding.enabled) {
                        ImGui::TextUnformatted(sourceLabel.c_str());
                    } else {
                        ImGui::TextDisabled("%s", sourceLabel.c_str());
                    }
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                        ImGui::OpenPopup("BindingMenu");
                    }
                    if (!sourceLabel.empty() && ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", sourceLabel.c_str());
                    }

                    ImGui::TableSetColumnIndex(3);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(
                        binding.type == TextureType::TextureCube ? "Cube" :
                        binding.type == TextureType::Texture3D ? "3D" : "2D");

                    ImGui::TableSetColumnIndex(4);
                    if (ImGui::ArrowButton("##BindingMenuButton", ImGuiDir_Down)) {
                        ImGui::OpenPopup("BindingMenu");
                    }

                    if (ImGui::BeginPopup("BindingMenu") || ImGui::BeginPopupContextItem("BindingMenu")) {
                        ImGui::Checkbox("Enabled", &binding.enabled);

                        int channel = binding.channelIndex;
                        if (ImGui::SliderInt("Channel", &channel, 0, 7)) {
                            binding.channelIndex = channel;
                        }

                        const char* typeNames[] = { "2D", "Cube", "3D" };
                        int typeIndex = (binding.type == TextureType::TextureCube) ? 1 : (binding.type == TextureType::Texture3D ? 2 : 0);
                        if (ImGui::Combo("Texture Type", &typeIndex, typeNames, 3)) {
                            binding.type = (typeIndex == 1) ? TextureType::TextureCube : (typeIndex == 2 ? TextureType::Texture3D : TextureType::Texture2D);
                        }

                        const char* bindTypes[] = { "Scene", "File" };
                        int bindTypeIndex = (binding.bindingType == BindingType::Scene) ? 0 : 1;
                        if (ImGui::Combo("Binding Type", &bindTypeIndex, bindTypes, 2)) {
                            binding.bindingType = (bindTypeIndex == 0) ? BindingType::Scene : BindingType::File;
                        }

                        if (binding.bindingType == BindingType::Scene) {
                            std::vector<const char*> sceneNames;
                            sceneNames.reserve(m_scenes.size() + 1);
                            sceneNames.push_back("(None)");
                            for (const auto& s : m_scenes) {
                                sceneNames.push_back(s.name.c_str());
                            }

                            int sceneIndex = binding.sourceSceneIndex >= 0 ? binding.sourceSceneIndex + 1 : 0;
                            if (ImGui::Combo("Source Scene", &sceneIndex, sceneNames.data(), (int)sceneNames.size())) {
                                binding.sourceSceneIndex = sceneIndex - 1;
                            }
                        } else {
                            char pathBuf[260] = {};
                            strncpy_s(pathBuf, binding.filePath.c_str(), _TRUNCATE);
                            if (ImGui::InputText("File Path", pathBuf, sizeof(pathBuf))) {
                                binding.filePath = pathBuf;
                                if (!binding.filePath.empty()) {
                                    LoadTextureFromFile(binding.filePath, binding.textureResource);
                                    binding.fileTextureValid = binding.textureResource != nullptr;
                                }
                            }
                            if (LabeledActionButton("BrowseTexture", OpenFontIcons::kFolder, "Browse", "Browse texture file", ImVec2(120.0f * dpiScale, 0.0f))) {
                                browseAndAssignFileTexture(binding);
                            }
                        }

                        if (ImGui::Button("Remove", ImVec2(120.0f * dpiScale, 0.0f))) {
                            bindingToRemove = i;
                            ImGui::CloseCurrentPopup();
                        }

                        ImGui::EndPopup();
                    }

                    ImGui::PopID();
                }
                ImGui::EndTable();

                if (bindingToRemove >= 0 && bindingToRemove < (int)scene.bindings.size()) {
                    scene.bindings.erase(scene.bindings.begin() + bindingToRemove);
                }
            }

            if (pushedTableFont) {
                ImGui::PopFont();
            }
        } else {
            ImGui::TextDisabled("No active scene selected.");
        }
    }
    ImGui::End();
}

}
