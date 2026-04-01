#include "ShaderLab/UI/ShaderLabIDE.h"
#include "ShaderLab/UI/OpenFontIcons.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <shellapi.h>

#include "ShaderLab/Core/CompilationService.h"
#include "ShaderLab/Graphics/Device.h"
#include "ShaderLab/Graphics/Dx12ResourceService.h"
#include "ShaderLab/Graphics/PreviewRenderer.h"

namespace ShaderLab {

namespace {
namespace fs = std::filesystem;

constexpr UINT kTutsPreviewSrvIndex = 121;

std::string ReadAllText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return {};
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string HumanizeStem(const std::string& stem) {
    std::string out;
    out.reserve(stem.size());
    bool capitalize = true;
    for (char c : stem) {
        if (c == '_' || c == '-' || c == '.') {
            out.push_back(' ');
            capitalize = true;
            continue;
        }
        if (capitalize) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            capitalize = false;
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::stringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        lines.push_back(line);
    }
    return lines;
}

struct MarkdownLink {
    std::string label;
    std::string target;
};

bool IsNumberedListLine(const std::string& line, std::string& outText) {
    size_t i = 0;
    while (i < line.size() && std::isdigit(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    if (i == 0 || i + 1 >= line.size() || line[i] != '.' || line[i + 1] != ' ') {
        return false;
    }
    outText = line.substr(i + 2);
    return true;
}

std::vector<MarkdownLink> ExtractLinks(const std::string& line) {
    std::vector<MarkdownLink> links;
    size_t pos = 0;
    while (true) {
        const size_t openLabel = line.find('[', pos);
        if (openLabel == std::string::npos) {
            break;
        }
        const size_t closeLabel = line.find(']', openLabel + 1);
        if (closeLabel == std::string::npos || closeLabel + 1 >= line.size() || line[closeLabel + 1] != '(') {
            pos = openLabel + 1;
            continue;
        }
        const size_t closeTarget = line.find(')', closeLabel + 2);
        if (closeTarget == std::string::npos) {
            pos = closeLabel + 1;
            continue;
        }
        MarkdownLink link;
        link.label = line.substr(openLabel + 1, closeLabel - openLabel - 1);
        link.target = line.substr(closeLabel + 2, closeTarget - (closeLabel + 2));
        if (!link.label.empty() && !link.target.empty()) {
            links.push_back(std::move(link));
        }
        pos = closeTarget + 1;
    }
    return links;
}

void OpenExternalTarget(const std::string& target) {
    if (target.empty()) {
        return;
    }
    ShellExecuteA(nullptr, "open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void RenderInlineMarkdownText(const std::string& line) {
    size_t start = 0;
    bool inCode = false;
    while (start < line.size()) {
        const size_t tick = line.find('`', start);
        if (tick == std::string::npos) {
            const std::string tail = line.substr(start);
            if (!tail.empty()) {
                if (inCode) {
                    ImGui::TextDisabled("%s", tail.c_str());
                } else {
                    ImGui::TextWrapped("%s", tail.c_str());
                }
            }
            break;
        }

        if (tick > start) {
            const std::string chunk = line.substr(start, tick - start);
            if (inCode) {
                ImGui::TextDisabled("%s", chunk.c_str());
            } else {
                ImGui::TextWrapped("%s", chunk.c_str());
            }
        }
        inCode = !inCode;
        start = tick + 1;
    }
}

void RenderMarkdownLine(const std::string& line, const ImVec4& headingColor, float headingScale) {
    if (line.empty()) {
        ImGui::Spacing();
        return;
    }

    std::string numberedText;
    if (IsNumberedListLine(line, numberedText)) {
        ImGui::BulletText("%s", numberedText.c_str());
        return;
    }

    if (line.rfind("### ", 0) == 0) {
        ImGui::SetWindowFontScale(headingScale);
        ImGui::TextColored(headingColor, "%s", line.substr(4).c_str());
        ImGui::SetWindowFontScale(1.0f);
        return;
    }
    if (line.rfind("## ", 0) == 0) {
        ImGui::Separator();
        ImGui::SetWindowFontScale(headingScale);
        ImGui::TextColored(headingColor, "%s", line.substr(3).c_str());
        ImGui::SetWindowFontScale(1.0f);
        return;
    }
    if (line.rfind("# ", 0) == 0) {
        ImGui::Separator();
        ImGui::SetWindowFontScale(headingScale);
        ImGui::TextColored(headingColor, "%s", line.substr(2).c_str());
        ImGui::SetWindowFontScale(1.0f);
        ImGui::Separator();
        return;
    }
    if (line.rfind("- ", 0) == 0) {
        ImGui::BulletText("%s", line.substr(2).c_str());
        return;
    }

    const auto links = ExtractLinks(line);
    if (links.empty()) {
        RenderInlineMarkdownText(line);
        return;
    }

    ImGui::TextWrapped("%s", line.c_str());
    for (size_t i = 0; i < links.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(links[i].label.c_str())) {
            OpenExternalTarget(links[i].target);
        }
        if (i + 1 < links.size()) {
            ImGui::SameLine();
        }
        ImGui::PopID();
    }
}

bool LooksLikeSceneContract(const std::string& code) {
    if (code.find("float4 main") != std::string::npos &&
        code.find("fragCoord") != std::string::npos &&
        code.find("iResolution") != std::string::npos &&
        code.find("iTime") != std::string::npos) {
        return true;
    }
    if (code.find("mainImage") != std::string::npos) {
        return true;
    }
    return false;
}

void ComputeTutTiming(double timeSeconds,
                      float bpm,
                      float& outIBeat,
                      float& outIBar,
                      float& outFBeat,
                      float& outFBarBeat,
                      float& outFBarBeat16) {
    constexpr float kBeatsPerBar = 4.0f;
    constexpr float kSixteenthPerBeat = 4.0f;

    const float beatsPerSecond = bpm / 60.0f;
    float exactBeat = static_cast<float>(timeSeconds * static_cast<double>(beatsPerSecond));
    if (exactBeat < 0.0f) {
        exactBeat = 0.0f;
    }

    const float beat = std::floor(exactBeat);
    const float bar = std::floor(beat / kBeatsPerBar);
    const float beatInBar = exactBeat - std::floor(exactBeat / kBeatsPerBar) * kBeatsPerBar;
    float barBeat16 = std::floor(beatInBar * kSixteenthPerBeat);
    barBeat16 = (std::max)(0.0f, (std::min)(15.0f, barBeat16));

    outIBeat = beat;
    outIBar = bar;
    outFBeat = exactBeat;
    outFBarBeat = beatInBar;
    outFBarBeat16 = barBeat16;
}

} // namespace

void ShaderLabIDE::RefreshTutsCatalog(bool force) {
    const double now = ImGui::GetTime();
    if (!force && (now - m_tutsLastScanTime) < 1.0) {
        return;
    }
    m_tutsLastScanTime = now;

    m_tutTopics.clear();
    m_tutsMenuVisible = false;

    const fs::path tutsRoot = fs::path(m_appRoot) / "editor_assets" / "tuts";
    std::error_code ec;
    if (!fs::exists(tutsRoot, ec) || !fs::is_directory(tutsRoot, ec)) {
        return;
    }

    std::vector<fs::path> topicDirs;
    for (const auto& entry : fs::directory_iterator(tutsRoot, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory()) {
            topicDirs.push_back(entry.path());
        }
    }
    std::sort(topicDirs.begin(), topicDirs.end());

    for (const auto& topicPath : topicDirs) {
        TutTopic topic;
        topic.name = HumanizeStem(topicPath.filename().string());

        std::vector<fs::path> files;
        for (const auto& file : fs::directory_iterator(topicPath, ec)) {
            if (ec) {
                break;
            }
            if (file.is_regular_file() && file.path().extension() == ".md") {
                files.push_back(file.path());
            }
        }
        std::sort(files.begin(), files.end());

        for (const auto& filePath : files) {
            TutItem item;
            item.title = HumanizeStem(filePath.stem().string());
            item.filePath = filePath.string();
            topic.items.push_back(std::move(item));
        }

        if (!topic.items.empty()) {
            m_tutTopics.push_back(std::move(topic));
        }
    }

    m_tutsMenuVisible = !m_tutTopics.empty();
}

void ShaderLabIDE::ShowTutsMenu() {
    RefreshTutsCatalog(false);
    if (!m_tutsMenuVisible) {
        return;
    }

    if (ImGui::BeginMenu("Tuts")) {
        for (int topicIndex = 0; topicIndex < (int)m_tutTopics.size(); ++topicIndex) {
            const auto& topic = m_tutTopics[topicIndex];
            if (ImGui::BeginMenu(topic.name.c_str())) {
                for (int itemIndex = 0; itemIndex < (int)topic.items.size(); ++itemIndex) {
                    const auto& item = topic.items[itemIndex];
                    if (ImGui::MenuItem(item.title.c_str())) {
                        const std::string markdown = ReadAllText(item.filePath);
                        if (markdown.empty()) {
                            m_tutErrorMessage = "Failed to load tutorial markdown file.";
                            continue;
                        }

                        m_tutActiveDocument = {};
                        m_tutActiveDocument.title = item.title;
                        m_tutActiveDocument.sourceFilePath = item.filePath;
                        m_tutActiveDocument.markdown = markdown;

                        const auto lines = SplitLines(markdown);
                        bool inCode = false;
                        std::string code;
                        for (const auto& line : lines) {
                            const bool fence = line.rfind("```", 0) == 0;
                            if (fence && !inCode) {
                                inCode = true;
                                code.clear();
                                continue;
                            }
                            if (fence && inCode) {
                                TutCodeBlock block;
                                block.code = code;
                                block.looksLikeSceneContract = LooksLikeSceneContract(code);
                                m_tutActiveDocument.codeBlocks.push_back(std::move(block));
                                inCode = false;
                                code.clear();
                                continue;
                            }
                            if (inCode) {
                                code += line;
                                code.push_back('\n');
                            }
                        }

                        m_tutPopupTitle = topic.name + " / " + item.title;
                        m_tutActiveTopicIndex = topicIndex;
                        m_tutActiveItemIndex = itemIndex;
                        m_showTutsPopup = true;
                        m_tutErrorMessage.clear();

                        m_tutActiveCodeBlockIndex = -1;
                        m_tutDisplayedCodeBlockIndex = -1;
                        for (int i = 0; i < (int)m_tutActiveDocument.codeBlocks.size(); ++i) {
                            if (m_tutActiveDocument.codeBlocks[i].looksLikeSceneContract) {
                                m_tutActiveCodeBlockIndex = i;
                                m_tutPreviewSourceCode = m_tutActiveDocument.codeBlocks[i].code;
                                m_tutPreviewPso.Reset();
                                m_tutPreviewTimeSeconds = 0.0;
                                m_tutPreviewPlaying = true;
                                break;
                            }
                        }
                        ImGui::OpenPopup("Tutorial");
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
}

void ShaderLabIDE::ShowTutsPopup() {
    if (!m_showTutsPopup) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (viewport) {
        const ImVec2 workSize = viewport->WorkSize;
        const ImVec2 maxSize(
            (std::max)(420.0f, workSize.x - 24.0f),
            (std::max)(320.0f, workSize.y - 24.0f));
        const ImVec2 minSize(
            (std::min)(960.0f, maxSize.x),
            (std::min)(720.0f, maxSize.y));
        const ImVec2 targetSize(
            (std::clamp)(workSize.x * 0.78f, minSize.x, maxSize.x),
            (std::clamp)(workSize.y * 0.90f, minSize.y, maxSize.y));

        ImGui::SetNextWindowSizeConstraints(minSize, maxSize);
        ImGui::SetNextWindowSize(targetSize, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(
            ImVec2(
                viewport->WorkPos.x + (viewport->WorkSize.x - targetSize.x) * 0.5f,
                viewport->WorkPos.y + (viewport->WorkSize.y - targetSize.y) * 0.5f),
            ImGuiCond_Appearing);
    }

    if (!ImGui::IsPopupOpen("Tutorial")) {
        ImGui::OpenPopup("Tutorial");
    }

    bool open = m_showTutsPopup;
    if (ImGui::BeginPopupModal("Tutorial", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::TextUnformatted(m_tutPopupTitle.empty() ? "Tutorial" : m_tutPopupTitle.c_str());
        ImGui::SameLine();
        {
            int sourceBlockIndex = m_tutActiveCodeBlockIndex;
            if (sourceBlockIndex < 0 ||
                sourceBlockIndex >= static_cast<int>(m_tutActiveDocument.codeBlocks.size()) ||
                !m_tutActiveDocument.codeBlocks[sourceBlockIndex].looksLikeSceneContract) {
                sourceBlockIndex = -1;
                for (int i = 0; i < static_cast<int>(m_tutActiveDocument.codeBlocks.size()); ++i) {
                    if (m_tutActiveDocument.codeBlocks[i].looksLikeSceneContract) {
                        sourceBlockIndex = i;
                        break;
                    }
                }
            }

            const bool hasSceneSource = (sourceBlockIndex >= 0);
            if (!hasSceneSource) {
                ImGui::BeginDisabled();
            }
            if (ImGui::Button("Open Source")) {
                if (hasSceneSource) {
                    m_tutPendingCreateCodeBlockIndex = sourceBlockIndex;
                    m_tutConfirmCreateScene = true;
                    ImGui::OpenPopup("Create Scene From Tutorial");
                }
            }
            if (!hasSceneSource) {
                ImGui::EndDisabled();
            }
        }
        ImGui::Separator();

        if (!m_tutErrorMessage.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_tutErrorMessage.c_str());
            ImGui::Separator();
        }

        if (m_tutPreviewPlaying) {
            m_tutPreviewTimeSeconds += ImGui::GetIO().DeltaTime;
        }

        const auto lines = SplitLines(m_tutActiveDocument.markdown);
        bool inCode = false;
        std::string code;
        int codeIndex = -1;

        ImGui::BeginChild("##TutScroll", ImVec2(0.0f, -40.0f), false);
        for (const auto& line : lines) {
            const bool fence = line.rfind("```", 0) == 0;
            if (fence && !inCode) {
                inCode = true;
                code.clear();
                continue;
            }
            if (fence && inCode) {
                ++codeIndex;
                ImGui::PushID(codeIndex);
                ImGui::Separator();
                ImGui::TextColored(m_uiThemeColors.LogoFontColor, "%s", "Code Example");

                if (m_tutDisplayedCodeBlockIndex != codeIndex) {
                    m_tutCodeTextEditor.SetText(code);
                    m_tutDisplayedCodeBlockIndex = codeIndex;
                }
                m_tutCodeTextEditor.SetReadOnly(true);
                m_tutCodeTextEditor.SetHandleMouseInputs(true);
                m_tutCodeTextEditor.SetHandleKeyboardInputs(true);
                const float codeEditorHeight = (std::max)(230.0f, ImGui::GetContentRegionAvail().y * 0.32f);
                ImFont* snippetFont = ResolveCodeEditorFont(-1);
                if (snippetFont) {
                    ImGui::PushFont(snippetFont);
                }
                SyncEditorCommentFont(m_tutCodeTextEditor, snippetFont);
                m_tutCodeTextEditor.Render("##TutCodeEditor", ImVec2(-1.0f, codeEditorHeight), true);
                if (snippetFont) {
                    ImGui::PopFont();
                }

                if (codeIndex >= 0 && codeIndex < (int)m_tutActiveDocument.codeBlocks.size() &&
                    m_tutActiveDocument.codeBlocks[codeIndex].looksLikeSceneContract) {
                    const bool active = (m_tutActiveCodeBlockIndex == codeIndex);
                    if (!active) {
                        if (ImGui::Button("Activate Preview")) {
                            m_tutActiveCodeBlockIndex = codeIndex;
                            m_tutPreviewSourceCode = m_tutActiveDocument.codeBlocks[codeIndex].code;
                            m_tutPreviewPso.Reset();
                            m_tutPreviewTimeSeconds = 0.0;
                            m_tutDisplayedCodeBlockIndex = -1;
                        }
                    } else {
                        ImGui::TextColored(m_uiThemeColors.LogoFontColor, "%s", "Live Preview (120 BPM)");
                        const float availableWidth = ImGui::GetContentRegionAvail().x;
                        const float previewWidth = (std::min)(760.0f, availableWidth * 0.92f);
                        const float previewHeight = previewWidth * (9.0f / 16.0f);
                        if (m_tutPreviewSrvGpuHandle.ptr != 0) {
                            const float centeredOffset = (std::max)(0.0f, (availableWidth - previewWidth) * 0.5f);
                            if (centeredOffset > 0.0f) {
                                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + centeredOffset);
                            }
                            ImGui::Image((ImTextureID)m_tutPreviewSrvGpuHandle.ptr, ImVec2(previewWidth, previewHeight));
                            const ImVec2 imageMin = ImGui::GetItemRectMin();
                            const ImVec2 imageMax = ImGui::GetItemRectMax();
                            const ImVec2 buttonSize(86.0f, 32.0f);
                            ImGui::SetCursorScreenPos(ImVec2(imageMax.x - buttonSize.x - 8.0f, imageMax.y - buttonSize.y - 8.0f));
                            ImFont* playPauseFont = m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]
                                ? m_fontCodeSizes[static_cast<int>(CodeFontSize::XS)]
                                : m_fontMenuSmall;
                            if (playPauseFont) {
                                ImGui::PushFont(playPauseFont);
                            }
                            if (ImGui::Button(m_tutPreviewPlaying ? "Pause" : "Play", buttonSize)) {
                                m_tutPreviewPlaying = !m_tutPreviewPlaying;
                            }
                            if (playPauseFont) {
                                ImGui::PopFont();
                            }
                            ImGui::SetCursorScreenPos(ImVec2(imageMin.x, imageMax.y + 4.0f));
                        } else {
                            ImGui::TextDisabled("Preview unavailable.");
                        }
                    }
                }

                ImGui::Separator();
                ImGui::PopID();
                inCode = false;
                code.clear();
                continue;
            }

            if (inCode) {
                code += line;
                code.push_back('\n');
                continue;
            }

            RenderMarkdownLine(line, m_uiThemeColors.LogoFontColor, 1.08f);
        }
        ImGui::EndChild();

        if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
            open = false;
            ImGui::CloseCurrentPopup();
        }

        if (m_tutConfirmCreateScene) {
            if (ImGui::BeginPopupModal("Create Scene From Tutorial", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextWrapped("Create a new scene from this tutorial source? This will not modify existing scenes.");
                ImGui::Spacing();

                if (ImGui::Button("Create", ImVec2(120.0f, 0.0f))) {
                    const int sourceBlockIndex = m_tutPendingCreateCodeBlockIndex;
                    if (sourceBlockIndex >= 0 &&
                        sourceBlockIndex < static_cast<int>(m_tutActiveDocument.codeBlocks.size()) &&
                        m_tutActiveDocument.codeBlocks[sourceBlockIndex].looksLikeSceneContract) {
                        Scene newScene;
                        newScene.name = m_tutActiveDocument.title.empty()
                            ? std::string("Tutorial Scene")
                            : (m_tutActiveDocument.title + " (Tut)");
                        newScene.description = m_tutPopupTitle;
                        newScene.shaderCode = m_tutActiveDocument.codeBlocks[sourceBlockIndex].code;
                        newScene.isDirty = true;

                        m_scenes.push_back(std::move(newScene));
                        const int newSceneIndex = static_cast<int>(m_scenes.size()) - 1;

                        SetActiveScene(newSceneIndex);
                        m_editingSceneIndex = newSceneIndex;
                        m_activeSceneIndex = newSceneIndex;
                        m_shaderState.text = m_scenes[newSceneIndex].shaderCode;
                        m_textEditor.SetText(m_shaderState.text);
                        m_shaderState.status = CompileStatus::Dirty;
                        m_tutErrorMessage = "Created a new scene from tutorial source.";
                    }

                    m_tutPendingCreateCodeBlockIndex = -1;
                    m_tutConfirmCreateScene = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
                    m_tutPendingCreateCodeBlockIndex = -1;
                    m_tutConfirmCreateScene = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::EndPopup();
            }
        }

        ImGui::EndPopup();
    }

    m_showTutsPopup = open;
    if (!m_showTutsPopup) {
        m_tutDisplayedCodeBlockIndex = -1;
    }
}

void ShaderLabIDE::RenderTutsPreviewTexture(ID3D12GraphicsCommandList* commandList) {
    if (!m_showTutsPopup || m_tutActiveCodeBlockIndex < 0 || m_tutPreviewSourceCode.empty()) {
        return;
    }
    if (!m_previewRenderer || !m_compilationService || !m_deviceRef || !m_srvHeap || !commandList) {
        return;
    }

    if (!m_dummyTexture) {
        CreateDummyTexture();
    }

    if (!m_tutPreviewTexture) {
        Dx12ResourceService resourceService(m_deviceRef->GetDevice());
        TextureAllocationRequest textureRequest{};
        textureRequest.width = m_tutPreviewWidth;
        textureRequest.height = m_tutPreviewHeight;
        textureRequest.format = DXGI_FORMAT_R8G8B8A8_UNORM;
        textureRequest.flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        textureRequest.initialState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (!resourceService.AllocateTexture2D(textureRequest, m_tutPreviewTexture)) {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc{};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 1;
        m_deviceRef->GetDevice()->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&m_tutPreviewRtvHeap));
        m_tutPreviewRtvHandle = m_tutPreviewRtvHeap->GetCPUDescriptorHandleForHeapStart();
        m_deviceRef->GetDevice()->CreateRenderTargetView(m_tutPreviewTexture.Get(), nullptr, m_tutPreviewRtvHandle);

        const UINT descriptorSize = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE srvCpu = m_srvHeap->GetCPUDescriptorHandleForHeapStart();
        srvCpu.ptr += static_cast<SIZE_T>(kTutsPreviewSrvIndex) * descriptorSize;

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        m_deviceRef->GetDevice()->CreateShaderResourceView(m_tutPreviewTexture.Get(), &srvDesc, srvCpu);

        m_tutPreviewSrvGpuHandle = m_srvHeap->GetGPUDescriptorHandleForHeapStart();
        m_tutPreviewSrvGpuHandle.ptr += static_cast<SIZE_T>(kTutsPreviewSrvIndex) * descriptorSize;
    }

    if (!m_tutPreviewInputSrvHeap) {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.NumDescriptors = 8;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        m_deviceRef->GetDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_tutPreviewInputSrvHeap));
    }

    {
        const UINT step = m_deviceRef->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        D3D12_CPU_DESCRIPTOR_HANDLE cpu = m_tutPreviewInputSrvHeap->GetCPUDescriptorHandleForHeapStart();
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        for (int slot = 0; slot < 8; ++slot) {
            D3D12_CPU_DESCRIPTOR_HANDLE slotCpu = cpu;
            slotCpu.ptr += static_cast<SIZE_T>(slot) * step;
            m_deviceRef->GetDevice()->CreateShaderResourceView(m_dummyTexture.Get(), &srvDesc, slotCpu);
        }
    }

    if (!m_tutPreviewPso) {
        std::vector<CompilationTextureBinding> bindings;
        const ShaderCompileResult compileResult = m_compilationService->CompilePreviewShader(
            m_tutPreviewSourceCode,
            bindings,
            false,
            "main",
            L"tutorial_example.hlsl",
            ShaderCompileMode::Live);

        if (!compileResult.success) {
            m_tutErrorMessage = "Tutorial example compile failed.";
            return;
        }

        m_tutPreviewPso = m_previewRenderer->CreatePSOFromBytecode(compileResult.bytecode);
        if (!m_tutPreviewPso) {
            m_tutErrorMessage = "Failed creating preview pipeline for tutorial example.";
            return;
        }
        m_tutErrorMessage.clear();
    }

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_tutPreviewTexture.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    ID3D12DescriptorHeap* heaps[] = { m_tutPreviewInputSrvHeap.Get() };
    commandList->SetDescriptorHeaps(1, heaps);

    float iBeat = 0.0f;
    float iBar = 0.0f;
    float fBeat = 0.0f;
    float fBarBeat = 0.0f;
    float fBarBeat16 = 0.0f;
    ComputeTutTiming(m_tutPreviewTimeSeconds, 120.0f, iBeat, iBar, fBeat, fBarBeat, fBarBeat16);

    m_previewRenderer->Render(
        commandList,
        m_tutPreviewPso.Get(),
        m_tutPreviewTexture.Get(),
        m_tutPreviewRtvHandle,
        m_tutPreviewInputSrvHeap->GetGPUDescriptorHandleForHeapStart(),
        m_tutPreviewWidth,
        m_tutPreviewHeight,
        static_cast<float>(m_tutPreviewTimeSeconds),
        iBeat,
        iBar,
        fBarBeat16,
        fBeat,
        fBarBeat);

    std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    commandList->ResourceBarrier(1, &barrier);
}

} // namespace ShaderLab
